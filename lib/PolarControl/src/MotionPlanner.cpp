#include "MotionPlanner.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>
#ifndef NATIVE_BUILD
#include <Arduino.h>
#include <esp_timer.h>
#include <driver/timer.h>
#include <rom/ets_sys.h>
#include "Logger.hpp"
#else
#include "esp32_mock.hpp"
#define LOG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#endif

static uint64_t plannerMicros() {
#ifdef NATIVE_BUILD
    return micros64();
#else
    return static_cast<uint64_t>(esp_timer_get_time());
#endif
}

// Step pulse width in microseconds
static constexpr uint32_t STEP_PULSE_WIDTH_US = 2;

// Direction setup time before step pulse
static constexpr uint32_t DIR_SETUP_TIME_US = 1;

static constexpr uint8_t STOP_MASK = 0x80;

MotionPlanner::MotionPlanner()
    : m_stepsPerMmR(100)
    , m_stepsPerRadT(3000)
    , m_maxRho(450.0f)
    , m_rMaxVel(30.0f)
    , m_rMaxAccel(20.0f)
    , m_rMaxJerk(100.0f)
    , m_tMaxVel(1.0f)
    , m_tMaxAccel(2.0f)
    , m_tMaxJerk(10.0f)
    , m_speedMultiplier(1.0f)
    , m_segmentHead(0)
    , m_segmentTail(0)
    , m_genSegmentIdx(0)
    , m_genSegmentStartTime(0)
    , m_queuedTSteps(0)
    , m_queuedRSteps(0)
    , m_executedTSteps(0)
    , m_executedRSteps(0)
    , m_targetTheta(0.0f)
    , m_targetRho(0.0f)
    , m_stepQueueHead(0)
    , m_stepQueueTail(0)
    , m_segmentStartTime(0)
    , m_segmentElapsed(0.0f)
    , m_running(false)
    , m_endOfPattern(false)
    , m_completedCount(0)
#ifdef NATIVE_BUILD
    , m_timerHandle(nullptr)
#endif
{
    // Initialize segments
    for (int i = 0; i < SEGMENT_BUFFER_SIZE; i++) {
        m_segments[i].calculated = false;
        m_segments[i].executing = false;
        m_segments[i].generationComplete = false;
    }
}

MotionPlanner::~MotionPlanner() {
    stop();
#ifndef NATIVE_BUILD
    if (m_normalHardwareTimerReady) {
        timer_isr_callback_remove(TIMER_GROUP_1, TIMER_0);
        timer_deinit(TIMER_GROUP_1, TIMER_0);
    }
    if (m_homingHardwareTimerReady) {
        timer_isr_callback_remove(TIMER_GROUP_1, TIMER_1);
        timer_deinit(TIMER_GROUP_1, TIMER_1);
    }
#endif
}

void MotionPlanner::init(double stepsPerMmR, double stepsPerRadT, float maxRho, float rMaxVel,
                         float rMaxAccel, float rMaxJerk, float tMaxVel, float tMaxAccel,
                         float tMaxJerk, bool resetPosition) {
    m_stepsPerMmR = stepsPerMmR;
    m_stepsPerRadT = stepsPerRadT;
    m_maxRho = maxRho;

    m_rMaxVel = rMaxVel;
    m_rMaxAccel = rMaxAccel;
    m_rMaxJerk = rMaxJerk;

    m_tMaxVel = tMaxVel;
    m_tMaxAccel = tMaxAccel;
    m_tMaxJerk = tMaxJerk;

    // Setup GPIO pins
    pinMode(T_STEP_PIN, OUTPUT);
    pinMode(T_DIR_PIN, OUTPUT);
    pinMode(R_STEP_PIN, OUTPUT);
    pinMode(R_DIR_PIN, OUTPUT);

    FastGPIO::setLow(T_STEP_PIN);
    FastGPIO::setLow(R_STEP_PIN);

    // Reset positions only if requested
    if (resetPosition) {
        m_queuedTSteps = 0;
        m_queuedRSteps = 0;
        m_executedTSteps = 0;
        m_executedRSteps = 0;
        m_targetTheta = 0.0f;
        m_targetRho = 0.0f;
    }

    discardResume();
    m_evaluationSegment = nullptr;

    // Reset buffer
    m_segmentHead = 0;
    m_segmentTail = 0;
    m_genSegmentIdx = 0;
    m_genSegmentStartTime = 0;
    m_stepQueueHead = 0;
    m_stepQueueTail = 0;
    m_completedCount = 0;
    m_stopEventQueued = false;
    m_underrunCount.store(0);
    m_consecutiveUnderruns.store(0);
    m_maxConsecutiveUnderruns.store(0);
    m_minQueueDepth = 0xFFFFFFFFu;
}

void MotionPlanner::updateSegmentTarget(Segment& seg, PathPoint target) {
    seg.targetTheta = target.theta;
    seg.targetRho = target.rho;
    seg.theta.targetSteps = thetaToSteps(target.theta);
    seg.rho.targetSteps = rhoToSteps(target.rho);
}

bool MotionPlanner::addSegment(double theta, double rho) {
    if (!hasSpace() || !std::isfinite(theta) || !std::isfinite(rho) ||
        !std::isfinite(m_stepsPerRadT) || !std::isfinite(m_stepsPerMmR) || m_stepsPerRadT <= 0 ||
        m_stepsPerMmR <= 0 || !std::isfinite(m_maxRho) || m_maxRho <= 0)
        return false;
    if (!m_thetaAvailable.load())
        theta = m_targetTheta;
    if (!m_rhoAvailable.load())
        rho = m_targetRho;
    rho = std::clamp(rho, 0.0, double(m_maxRho));
    const double ts = std::round(theta * m_stepsPerRadT), rs = std::round(rho * m_stepsPerMmR);
    if (ts < INT32_MIN || ts > INT32_MAX || rs < 0 || rs > INT32_MAX ||
        std::abs(ts - m_queuedTSteps.load()) > INT32_MAX ||
        std::abs(rs - m_queuedRSteps.load()) > INT32_MAX)
        return false;
    if (int32_t(ts) == m_queuedTSteps.load() && int32_t(rs) == m_queuedRSteps.load()) {
        // Keep the last planned geometric endpoint: substep points must not
        // change the start of a later move without emitting that movement.
        return true;
    }
    Segment& seg = m_segments[m_segmentHead];
    seg = Segment{};
    if (m_evaluationSegment == &seg)
        m_evaluationSegment = nullptr;
    seg.theta.startSteps = m_queuedTSteps.load();
    seg.rho.startSteps = m_queuedRSteps.load();
    seg.lastGenThetaSteps = seg.theta.startSteps;
    seg.lastGenRhoSteps = seg.rho.startSteps;
    const PathPoint target{theta, rho};
    seg.path.line({m_targetTheta, m_targetRho}, target, m_maxRho);
    seg.endDistance = seg.path.length;
    if (m_resumeReady) {
        if (std::abs(theta - m_resumePath.end.theta) < 1e-10 &&
            std::abs(rho - m_resumePath.end.rho) < 1e-10) {
            seg.path = m_resumePath;
            seg.startDistance = m_resumeStartDistance;
            seg.endDistance = seg.path.length;
            seg.geometryLocked = true;
        }
        m_resumeReady = false;
        m_resumeTargetCount = 0;
        m_resumeCaptured = false;
    }
    m_resumeCaptured = false;
    m_resumeTargetCount = 0;
    updateSegmentTarget(seg, target);
    m_targetTheta = theta;
    m_targetRho = rho;
    m_queuedTSteps.store(seg.theta.targetSteps);
    m_queuedRSteps.store(seg.rho.targetSteps);
    m_segmentHead = (m_segmentHead + 1) % SEGMENT_BUFFER_SIZE;
    m_stopEventQueued = false;
    return true;
}

void MotionPlanner::setAxisAvailability(bool thetaAvailable, bool rhoAvailable) {
    m_thetaAvailable.store(thetaAvailable);
    m_rhoAvailable.store(rhoAvailable);
}

void MotionPlanner::setPathLimits(double velocity, double acceleration, double tolerance) {
    if (m_running.load() || m_segmentHead != m_segmentTail || !std::isfinite(velocity) ||
        !std::isfinite(acceleration) || !std::isfinite(tolerance) || velocity < 0 ||
        acceleration < 0 || tolerance < 0)
        return;
    m_ballMaxVelocity = velocity;
    m_ballMaxAcceleration = acceleration;
    m_cornerTolerance = tolerance;
    for (int i = m_segmentTail; i != m_segmentHead; i = (i + 1) % SEGMENT_BUFFER_SIZE)
        m_segments[i].limitsCalculated = false;
}

void MotionPlanner::calculatePathLimits(Segment& seg) {
    PathPoint d1, d2, d3;
    seg.path.derivativeBounds(d1, d2, d3);
    double v = (seg.endDistance - seg.startDistance) / MIN_SEGMENT_DURATION;
    double a = 1e12, j = 1e12;
    auto axis = [&](double first, double second, double third, double vmax, double amax,
                    double jmax) {
        if (first < 1e-15)
            return;
        v = std::min(v, vmax * m_speedMultiplier / first);
        if (second > 1e-14 || third > 1e-14) {
            if (second > 1e-14)
                v = std::min(v, std::sqrt(amax / (2 * second)));
            if (third > 1e-14)
                v = std::min(v, std::cbrt(jmax / (3 * third)));
            a = std::min(a, amax / (2 * first));
            j = std::min(j, jmax / (3 * first));
        } else {
            a = std::min(a, amax / first);
            j = std::min(j, jmax / first);
        }
    };
    axis(d1.theta, d2.theta, d3.theta, m_tMaxVel, m_tMaxAccel, m_tMaxJerk);
    axis(d1.rho, d2.rho, d3.rho, m_rMaxVel, m_rMaxAccel, m_rMaxJerk);
    // Bound Cartesian derivatives, including centripetal and Coriolis terms.
    const double r = std::max(seg.path.start.rho, seg.path.end.rho);
    const double cartFirst = std::hypot(d1.rho, r * d1.theta);
    const double cartSecond =
        d2.rho + r * d2.theta + 2 * d1.rho * d1.theta + r * d1.theta * d1.theta;
    if (m_ballMaxVelocity > 0 && cartFirst > 1e-15)
        v = std::min(v, m_ballMaxVelocity * m_speedMultiplier / cartFirst);
    if (m_ballMaxAcceleration > 0 && cartFirst > 1e-15) {
        if (cartSecond > 1e-14) {
            v = std::min(v, std::sqrt(m_ballMaxAcceleration / (2 * cartSecond)));
            a = std::min(a, m_ballMaxAcceleration / (2 * cartFirst));
        } else
            a = std::min(a, m_ballMaxAcceleration / cartFirst);
    }
    // Remaining jerk budget for 3*q''*v*a, after q'*j and q'''*v^3.
    if (d2.theta > 1e-14)
        a = std::min(a, m_tMaxJerk / (9 * d2.theta * v));
    if (d2.rho > 1e-14)
        a = std::min(a, m_rMaxJerk / (9 * d2.rho * v));
    seg.maxVelocity = v;
    seg.maxAcceleration = a;
    seg.maxJerk = j;
    seg.limitsCalculated = true;
}

void MotionPlanner::calculateSegmentProfile(Segment& seg) {
    SCurve::Profile profile{};
    const bool valid =
        SCurve::calculate(seg.endDistance - seg.startDistance, seg.entryVelocity, seg.exitVelocity,
                          seg.maxVelocity, seg.maxAcceleration, seg.maxJerk, profile);
    if (!valid) {
        // A failed solve must never publish a discontinuous rest-to-rest
        // fallback. This is an invariant violation, not a valid trajectory.
        LOG("ERROR: infeasible shared path profile\n");
        seg.calculated = false;
        return;
    }
    seg.profile = SCurve::CompactProfile(profile);
    if (m_evaluationSegment == &seg)
        m_evaluationSegment = nullptr;
    seg.duration = profile.totalTime;
    seg.durationUs = static_cast<uint64_t>(std::ceil(seg.duration * 1000000.0));
    seg.calculated = true;
}

void MotionPlanner::recalculate() {
    if (m_segmentHead == m_segmentTail)
        return;
    auto mutableProfile = [](const Segment& s) {
        return !s.executing && s.nextSampleUs == 0 && !s.generationComplete;
    };
    int indices[SEGMENT_BUFFER_SIZE];
    int count = 0;
    for (int i = m_segmentTail; i != m_segmentHead; i = (i + 1) % SEGMENT_BUFFER_SIZE)
        indices[count++] = i;
    // Only the former lookahead tail acquires a new outgoing junction. Its
    // provisional entry is zero, so a committed predecessor remains feasible.
    for (int k = 0; k + 1 < count; ++k) {
        Segment& a = m_segments[indices[k]];
        Segment& b = m_segments[indices[k + 1]];
        if (!mutableProfile(a) || !mutableProfile(b))
            continue;
        if (!a.geometryLocked && !b.geometryLocked) {
            PathPoint tangent = boundedJunction(a.path, b.path, m_maxRho, m_cornerTolerance);
            if (PolarPath::norm(tangent, m_maxRho) > 1e-12) {
                if (a.path.exit.theta != tangent.theta || a.path.exit.rho != tangent.rho) {
                    a.path.exit = tangent;
                    a.path.rebuild();
                    a.limitsCalculated = false;
                }
                if (b.path.entry.theta != tangent.theta || b.path.entry.rho != tangent.rho) {
                    b.path.entry = tangent;
                    b.path.rebuild();
                    b.limitsCalculated = false;
                }
            }

        } else if (a.geometryLocked && !b.geometryLocked) {
            if (b.path.entry.theta != a.path.exit.theta || b.path.entry.rho != a.path.exit.rho) {
                b.path.entry = a.path.exit;
                b.path.rebuild();
                b.limitsCalculated = false;
            }
        }
    }
    for (int k = 0; k < count; ++k) {
        Segment& s = m_segments[indices[k]];
        if (mutableProfile(s) && !s.limitsCalculated)
            calculatePathLimits(s);
    }
    double boundary[SEGMENT_BUFFER_SIZE + 1]{};
    for (int k = 1; k < count; ++k) {
        const Segment& prev = m_segments[indices[k - 1]];
        const Segment& next = m_segments[indices[k]];
        const PathPoint p = prev.path.tangent(prev.endDistance),
                        n = next.path.tangent(next.startDistance);
        const bool compatible = PolarPath::norm(p - n, m_maxRho) < 1e-9;
        boundary[k] = compatible ? std::min(prev.maxVelocity, next.maxVelocity) : 0;
        if (!mutableProfile(prev))
            boundary[k] = prev.exitVelocity;
    }
    // Tail geometry is provisional until its successor is known. Leave room
    // to alter it without demanding a slower entry from committed motion.
    if (m_cornerTolerance > 0 && !m_endOfPattern && count > 1 &&
        mutableProfile(m_segments[indices[count - 2]]))
        boundary[count - 1] = 0;
    for (int k = count - 1; k >= 0; --k) {
        const Segment& s = m_segments[indices[k]];
        if (!mutableProfile(s))
            continue;
        double allowed =
            SCurve::maxAchievableEntryVelocity(s.endDistance - s.startDistance, boundary[k + 1],
                                               s.maxVelocity, s.maxAcceleration, s.maxJerk);
        if (k == 0 || mutableProfile(m_segments[indices[k - 1]]))
            boundary[k] = std::min(boundary[k], allowed);
    }
    for (int k = 0; k < count; ++k) {
        Segment& s = m_segments[indices[k]];
        if (!mutableProfile(s))
            continue;
        boundary[k + 1] = std::min(
            boundary[k + 1],
            SCurve::maxAchievableExitVelocity(s.endDistance - s.startDistance, boundary[k],
                                              s.maxVelocity, s.maxAcceleration, s.maxJerk));
        s.entryVelocity = boundary[k];
        s.exitVelocity = boundary[k + 1];
        calculateSegmentProfile(s);
    }
}

const SCurve::Profile& MotionPlanner::evaluationProfile(const Segment& seg) const {
    if (m_evaluationSegment != &seg) {
        m_evaluationProfile = seg.profile.expand();
        m_evaluationSegment = &seg;
    }
    return m_evaluationProfile;
}

double MotionPlanner::segmentDistance(const Segment& s, double t) const {
    if (s.braking && t >= m_brakeStartTime)
        return m_brakeStartDistance + SCurve::getPosition(m_brakeProfile, t - m_brakeStartTime);
    return s.startDistance + SCurve::getPosition(evaluationProfile(s), t);
}

double MotionPlanner::segmentSpeed(const Segment& s, double t) const {
    if (s.braking && t >= m_brakeStartTime)
        return SCurve::getVelocity(m_brakeProfile, t - m_brakeStartTime);
    return SCurve::getVelocity(evaluationProfile(s), t);
}

void MotionPlanner::stopGracefully(bool preserveForResume) {
    if (!m_running || isIdle()) {
        stop();
        return;
    }
    m_resumeReady = false;
    m_resumeTargetCount = 0;
    m_resumeCaptured = preserveForResume;
    if (m_genSegmentIdx == m_segmentHead) {
        m_endOfPattern = true;
        if (!m_stopEventQueued &&
            queueStepEvent(static_cast<uint32_t>(m_genSegmentStartTime), STOP_MASK, 0))
            m_stopEventQueued = true;
        return;
    }
    // Keep queued pulses and the source geometry. Find the earliest future
    // zero-acceleration point that permits a jerk-limited stop on that curve.
    // If braking spans a waypoint, retain the existing feasible profile until
    // a following segment provides enough stopping distance.
    for (int i = m_genSegmentIdx; i != m_segmentHead; i = (i + 1) % SEGMENT_BUFFER_SIZE) {
        Segment& s = m_segments[i];
        const double earliest = s.nextSampleUs / 1000000.0;
        const auto profile = s.profile.expand();
        double candidates[4] = {earliest, profile.tEnd[2], profile.tEnd[3], profile.totalTime};
        for (double time : candidates) {
            if (time + 1e-10 < earliest || time > s.duration + 1e-10 ||
                std::abs(SCurve::getAcceleration(profile, time)) > 1e-9)
                continue;
            const double position = segmentDistance(s, time), speed = segmentSpeed(s, time);
            const double distance =
                SCurve::decelerationDistance(speed, 0, s.maxAcceleration, s.maxJerk);
            if (position + distance > s.endDistance + 1e-9)
                continue;
            SCurve::Profile brake{};
            if (!SCurve::calculate(distance, speed, 0, std::max(speed, s.maxVelocity),
                                   s.maxAcceleration, s.maxJerk, brake))
                continue;
            const double finish = std::min(s.endDistance, position + distance);
            if (preserveForResume) {
                int first = i;
                if (finish >= s.endDistance - 1e-9)
                    first = (i + 1) % SEGMENT_BUFFER_SIZE;
                else {
                    m_resumePath = s.path;
                    m_resumeStartDistance = finish;
                    m_resumeReady = true;
                }
                for (int n = first; n != m_segmentHead; n = (n + 1) % SEGMENT_BUFFER_SIZE)
                    m_resumeTargets[m_resumeTargetCount++] = {m_segments[n].targetTheta,
                                                              m_segments[n].targetRho};
            }
            m_brakeProfile = brake;
            m_brakeStartTime = time;
            m_brakeStartDistance = position;
            s.braking = true;
            s.geometryLocked = true;
            s.endDistance = finish;
            s.exitVelocity = 0;
            s.duration = time + brake.totalTime;
            s.durationUs = static_cast<uint64_t>(std::ceil(s.duration * 1000000));
            updateSegmentTarget(s, s.path.position(finish));
            m_segmentHead = (i + 1) % SEGMENT_BUFFER_SIZE;
            m_queuedTSteps.store(s.theta.targetSteps);
            m_queuedRSteps.store(s.rho.targetSteps);
            m_targetTheta = s.targetTheta;
            m_targetRho = s.targetRho;
            m_endOfPattern = true;
            m_stopEventQueued = false;
            return;
        }
    }
    // The existing lookahead already ends at zero. A numerically marginal
    // boundary must not provoke an off-path or discontinuous replacement.
    m_endOfPattern = true;
}

void MotionPlanner::start() {
    if (m_running.load() ||
        m_homingRhoActive.load(std::memory_order_acquire)) return;

    if (!ensureStepTimer()) return;

    m_timingRunSerial.fetch_add(1, std::memory_order_relaxed);
    m_running.store(true);
    m_segmentStartTime = plannerMicros();
    m_segmentElapsed = 0.0f;

    m_startupHoldoff = true;
    m_stopEventQueued = false;

    // Initialize generation state
    m_genSegmentIdx = m_segmentTail;
    m_genSegmentStartTime = m_segmentStartTime;

    // Mark first segment as executing
    if (m_segmentHead != m_segmentTail) {
        m_segments[m_segmentTail].executing = true;
    }
}

bool MotionPlanner::ensureStepTimer() {
#ifdef NATIVE_BUILD
    if (m_timerHandle != nullptr) return true;

    esp_timer_create_args_t timerArgs{};
    timerArgs.callback = stepTimerISR;
    timerArgs.arg = this;
    timerArgs.dispatch_method = ESP_TIMER_TASK;
    timerArgs.name = "step_timer";
    timerArgs.skip_unhandled_events = true;
    return esp_timer_create(&timerArgs,
                            (esp_timer_handle_t*)&m_timerHandle) == 0;
#else
    if (m_normalHardwareTimerReady) return true;
    // TG1/T0 is reserved for normal motion; qualified homing keeps TG1/T1.
    // The ESP-IDF esp_timer uses the separate TG0 LAC timer. No Arduino
    // timerBegin/tone consumer is used by this firmware.
    timer_config_t config{};
    config.alarm_en = TIMER_ALARM_DIS;
    config.counter_en = TIMER_PAUSE;
    config.intr_type = TIMER_INTR_LEVEL;
    config.counter_dir = TIMER_COUNT_UP;
    config.auto_reload = TIMER_AUTORELOAD_EN;
    config.divider = 80;
    if (timer_init(TIMER_GROUP_1, TIMER_0, &config) != ESP_OK) return false;
    if (timer_isr_callback_add(TIMER_GROUP_1, TIMER_0, normalHardwareISR,
                              this, ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3) != ESP_OK) {
        timer_deinit(TIMER_GROUP_1, TIMER_0);
        return false;
    }
    m_normalHardwareTimerReady = true;
    return true;
#endif
}

bool MotionPlanner::startNormalStepTimer() {
#ifdef NATIVE_BUILD
    const bool started = esp_timer_start_periodic(
        (esp_timer_handle_t)m_timerHandle, STEP_TIMER_PERIOD_US) == 0;
    m_timerActive.store(started, std::memory_order_release);
    return started;
#else
    if (!m_normalHardwareTimerReady) return false;
    pauseNormalStepTimer();
    if (timer_set_counter_value(TIMER_GROUP_1, TIMER_0, 0) != ESP_OK ||
        timer_set_alarm_value(TIMER_GROUP_1, TIMER_0, STEP_TIMER_PERIOD_US) != ESP_OK ||
        timer_set_alarm(TIMER_GROUP_1, TIMER_0, TIMER_ALARM_EN) != ESP_OK) return false;
    // The driver ISR holds this same group lock across our callback. Clear
    // pending status before publishing ownership for a new queue execution.
    // IDF 4.4 deprecates explicit locks for ordinary ISR users (its callback
    // wrapper locks already). This is a task-side restart, where that public
    // group lock is still needed; keep the exception local to these two calls.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    timer_spinlock_take(TIMER_GROUP_1);
    timer_group_clr_intr_status_in_isr(TIMER_GROUP_1, TIMER_0);
    timer_spinlock_give(TIMER_GROUP_1);
#pragma GCC diagnostic pop
    m_timerActive.store(true, std::memory_order_release);
    m_normalHardwareActive.store(true, std::memory_order_release);
    if (timer_start(TIMER_GROUP_1, TIMER_0) == ESP_OK) return true;
    pauseNormalStepTimer();
    return false;
#endif
}

void MotionPlanner::pauseNormalStepTimer() {
#ifdef NATIVE_BUILD
    if (m_timerHandle != nullptr) esp_timer_stop((esp_timer_handle_t)m_timerHandle);
#else
    m_normalHardwareActive.store(false, std::memory_order_release);
    // timer_pause takes the group lock also held by timer_isr_default around
    // its callback. Returning is therefore an in-flight callback barrier.
    // Any pending callback after this sees normal ownership false. Do not
    // reset/reuse queue state before this barrier, and do not call from ISR.
    if (m_normalHardwareTimerReady) timer_pause(TIMER_GROUP_1, TIMER_0);
#endif
    m_timerActive.store(false, std::memory_order_release);
}

#ifndef NATIVE_BUILD
bool IRAM_ATTR MotionPlanner::normalHardwareISR(void* arg) {
    auto* planner = static_cast<MotionPlanner*>(arg);
    if (planner->m_normalHardwareActive.load(std::memory_order_acquire)) {
        planner->handleStepTimer();
    }
    return false;
}

// Reserve TG1/T1 for homing only. Register from the motor task so UART and
// Wi-Fi task scheduling do not dispatch these STEP pulses.
bool MotionPlanner::ensureHomingHardwareTimer() {
    if (m_homingHardwareTimerReady) return true;
    timer_config_t config{};
    config.alarm_en = TIMER_ALARM_DIS;
    config.counter_en = TIMER_PAUSE;
    config.intr_type = TIMER_INTR_LEVEL;
    config.counter_dir = TIMER_COUNT_UP;
    config.auto_reload = TIMER_AUTORELOAD_EN;
    config.divider = 80; // 80 MHz APB clock -> 1 microsecond ticks.
    if (timer_init(TIMER_GROUP_1, TIMER_1, &config) != ESP_OK) return false;
    if (timer_isr_callback_add(TIMER_GROUP_1, TIMER_1, homingHardwareISR,
                              this, ESP_INTR_FLAG_IRAM) != ESP_OK) {
        timer_deinit(TIMER_GROUP_1, TIMER_1);
        return false;
    }
    m_homingHardwareTimerReady = true;
    return true;
}

bool MotionPlanner::setHomingHardwareInterval(uint32_t intervalUs) {
    // Rate changes occur only on the acceleration ramp. Pause/reset prevents
    // shortening the alarm behind an already advanced counter.
    if (timer_pause(TIMER_GROUP_1, TIMER_1) != ESP_OK ||
        timer_set_counter_value(TIMER_GROUP_1, TIMER_1, 0) != ESP_OK ||
        timer_set_alarm_value(TIMER_GROUP_1, TIMER_1, intervalUs) != ESP_OK ||
        timer_set_alarm(TIMER_GROUP_1, TIMER_1, TIMER_ALARM_EN) != ESP_OK ||
        timer_start(TIMER_GROUP_1, TIMER_1) != ESP_OK) {
        m_homingRhoActive.store(false, std::memory_order_release);
        m_timerActive.store(false, std::memory_order_release);
        timer_pause(TIMER_GROUP_1, TIMER_1);
        return false;
    }
    return true;
}

bool IRAM_ATTR MotionPlanner::homingHardwareISR(void* arg) {
    auto* planner = static_cast<MotionPlanner*>(arg);
    if (!planner->m_homingRhoActive.load(std::memory_order_acquire)) {
        timer_group_set_counter_enable_in_isr(TIMER_GROUP_1, TIMER_1, TIMER_PAUSE);
        return false;
    }
    FastGPIO::setHigh(R_STEP_PIN);
    // Arduino's delayMicroseconds is in flash in this build. The hardware
    // ISR must remain executable while the flash cache is unavailable.
    ets_delay_us(STEP_PULSE_WIDTH_US);
    FastGPIO::setLow(R_STEP_PIN);
    const uint32_t count = planner->m_homingRhoStepCount.fetch_add(1,
        std::memory_order_release) + 1U;
    const uint32_t limit = planner->m_homingRhoStepLimit.load(std::memory_order_acquire);
    if (limit > 0 && count >= limit) {
        planner->m_homingRhoActive.store(false, std::memory_order_release);
        planner->m_timerActive.store(false, std::memory_order_release);
        timer_group_set_counter_enable_in_isr(TIMER_GROUP_1, TIMER_1, TIMER_PAUSE);
    }
    return false;
}
#endif

bool MotionPlanner::startRhoHoming(int8_t direction,
                                   uint32_t stepsPerSecond,
                                   uint32_t maxSteps) {
    if (direction == 0 || m_running.load() || m_homingRhoActive.load()) {
        return false;
    }
#ifdef NATIVE_BUILD
    if (!ensureStepTimer()) return false;
#else
    pauseNormalStepTimer();
    if (!ensureHomingHardwareTimer()) return false;
#endif

    const uint32_t maxRate = 1000000U / STEP_TIMER_PERIOD_US;
    if (stepsPerSecond == 0 || stepsPerSecond > maxRate) return false;

    const uint32_t intervalUs = std::max<uint32_t>(
        STEP_TIMER_PERIOD_US,
        (1000000U + stepsPerSecond / 2U) / stepsPerSecond);
    FastGPIO::setLow(R_STEP_PIN);
    FastGPIO::write(R_DIR_PIN, direction > 0);
#ifndef NATIVE_BUILD
    delayMicroseconds(DIR_SETUP_TIME_US);
#endif

    m_homingRhoStepCount.store(0, std::memory_order_relaxed);
    m_homingRhoStepLimit.store(maxSteps, std::memory_order_relaxed);
    m_homingRhoIntervalUs.store(intervalUs, std::memory_order_relaxed);
    m_homingRhoNextStepUs.store(micros() + intervalUs,
                                std::memory_order_relaxed);
    m_timerActive.store(true, std::memory_order_release);
    m_homingRhoActive.store(true, std::memory_order_release);

#ifdef NATIVE_BUILD
    if (esp_timer_start_periodic((esp_timer_handle_t)m_timerHandle,
                                 STEP_TIMER_PERIOD_US) != 0) {
#else
    if (!setHomingHardwareInterval(intervalUs)) {
#endif
        m_homingRhoActive.store(false, std::memory_order_release);
        m_timerActive.store(false, std::memory_order_release);
        FastGPIO::setLow(R_STEP_PIN);
        return false;
    }
    return true;
}

bool MotionPlanner::setRhoHomingStepRate(uint32_t stepsPerSecond) {
    const uint32_t maxRate = 1000000U / STEP_TIMER_PERIOD_US;
    if (!m_homingRhoActive.load(std::memory_order_acquire) ||
        stepsPerSecond == 0 || stepsPerSecond > maxRate) {
        return false;
    }

    const uint32_t intervalUs = std::max<uint32_t>(
        STEP_TIMER_PERIOD_US,
        (1000000U + stepsPerSecond / 2U) / stepsPerSecond);
    m_homingRhoIntervalUs.store(intervalUs, std::memory_order_release);
#ifndef NATIVE_BUILD
    if (!setHomingHardwareInterval(intervalUs)) return false;
#endif
    return true;
}

void MotionPlanner::stopRhoHoming() {
    const bool wasHoming = m_homingRhoActive.exchange(
        false, std::memory_order_acq_rel);
#ifndef NATIVE_BUILD
    if (m_homingHardwareTimerReady) timer_pause(TIMER_GROUP_1, TIMER_1);
#else
    if (wasHoming && m_timerHandle != nullptr) {
        esp_timer_stop((esp_timer_handle_t)m_timerHandle);
    }
#endif
    if (wasHoming) {
        m_timerActive.store(false, std::memory_order_release);
        FastGPIO::setLow(R_STEP_PIN);
    }
}

void MotionPlanner::stop(bool clearResume) {
    if (clearResume)
        discardResume();
    m_homingRhoActive.store(false, std::memory_order_release);
#ifndef NATIVE_BUILD
    if (m_homingHardwareTimerReady) timer_pause(TIMER_GROUP_1, TIMER_1);
#endif
    pauseNormalStepTimer();

    m_running.store(false);
    m_timerActive.store(false);
    if (m_stepMotionActive.exchange(false, std::memory_order_acq_rel)) {
        m_lastStepMotionStopUs.store(micros(), std::memory_order_release);
    }
    m_startupHoldoff = false;

    // Clear step queue
    m_stepQueueHead.store(0);
    m_stepQueueTail.store(0);

    // Clear segment buffer
    m_segmentHead = 0;
    m_segmentTail = 0;
    m_genSegmentIdx = 0;
    m_genSegmentStartTime = 0;

    // Reset pattern state
    m_endOfPattern = false;
    m_stopEventQueued = false;

    // Sync queued positions with executed positions so next pattern
    // starts from current actual position
    m_queuedTSteps.store(m_executedTSteps.load());
    m_queuedRSteps.store(m_executedRSteps.load());

    // Update target positions to match
    m_targetTheta = stepsToTheta(m_executedTSteps);
    m_targetRho = stepsToRho(m_executedRSteps);

    m_consecutiveUnderruns.store(0);
    m_maxConsecutiveUnderruns.store(0);
    m_minQueueDepth = 0xFFFFFFFFu;
    m_lastQueuedEventTime = 0;
    m_lastQueuedEventTimeValid = false;
}

void MotionPlanner::process() {
    uint32_t now = micros();
    if (m_lastProcessTime > 0) {
        m_intervalProfiler.addSample(now - m_lastProcessTime);
    }
    m_lastProcessTime = now;

    // If manually stopped (running=false and no segments), do nothing
    if (!m_running.load() && m_segmentHead == m_segmentTail) return;

    // 1. Generation Loop (only if running)
    int queueDepth = STEP_QUEUE_SIZE - 1 - getStepQueueSpace();
    if (queueDepth < 0) queueDepth = 0;

    FillStopReason lastStopReason = FillStopReason::None;

    if (m_running.load() && m_genSegmentIdx != m_segmentHead) {
        lastStopReason = fillStepQueue(STEP_QUEUE_HORIZON_US);
        queueDepth = STEP_QUEUE_SIZE - 1 - getStepQueueSpace();
        if (queueDepth < 0) queueDepth = 0;
    }

    // If we have segments but none are executing, start the first one
    if (m_running.load() && m_segmentHead != m_segmentTail && !m_segments[m_segmentTail].executing) {
        // First segment start - use current time
        // Note: m_segmentStartTime was already set in start(), but if we added
        // segments after start() while idle, we need to update it.
        // If we are recovering from idle, sync gen time too.
        if (m_segmentTail == m_genSegmentIdx) { // Only if we haven't generated ahead
            m_segmentStartTime = plannerMicros();
            m_genSegmentStartTime = m_segmentStartTime;
        }

        m_segmentElapsed = 0.0f;
        m_segments[m_segmentTail].executing = true;
    }

    if (m_startupHoldoff) {
        bool queueFull = (queueDepth >= (STEP_QUEUE_SIZE - 1));
        bool horizonReached = (lastStopReason == FillStopReason::Horizon);
        bool hasData = (queueDepth > 0);
        bool generationFinished = (m_genSegmentIdx == m_segmentHead);

        if (queueFull || (hasData && (horizonReached || generationFinished))) {
            m_startupHoldoff = false;
        }
    }

    // Auto-start timer if buffer has any data and startup holdoff is cleared
    if (!m_timerActive.load() && !m_startupHoldoff && m_running.load()) {
        if (queueDepth > 0) {
            if (!startNormalStepTimer()) {
                stop(); // Fail closed; never leave queued movement awaiting a timer.
                return;
            }
        }
    }

    uint32_t depth = static_cast<uint32_t>(queueDepth);
    if (m_minQueueDepth == 0xFFFFFFFFu || depth < m_minQueueDepth) {
        m_minQueueDepth = depth;
    }

    // 2. Execution Completion Check
    if (m_segmentHead != m_segmentTail) {
        Segment& current = m_segments[m_segmentTail];

        // Calculate elapsed time in current segment
        uint64_t segmentNow = plannerMicros();
        // Handle timer wraparound for elapsed calculation
        uint64_t diff = segmentNow - m_segmentStartTime;
        double elapsed = diff / 1000000.0;

        // Check if current segment is complete
        // It's complete if time has elapsed AND we've generated all steps for it
        if (current.executing && elapsed >= current.duration && current.generationComplete) {

            // Segment complete
            current.executing = false;
            m_completedCount++;

            // Move to next segment
            m_segmentTail = (m_segmentTail + 1) % SEGMENT_BUFFER_SIZE;

            // Update start time deterministically
            m_segmentStartTime += current.durationUs;
            m_segmentElapsed = 0.0f;

            // Start next segment immediately if available
            // If stopped by sentinel (m_running=false), we don't start new segments unless manual restart
            if (m_segmentHead != m_segmentTail && m_running.load()) {
                m_segments[m_segmentTail].executing = true;

                // If we fell behind in generation (underrun recovery),
                // snap generation to execution
                if (m_genSegmentIdx == m_segmentTail && !m_segments[m_segmentTail].generationComplete) {
                     // We are generating the segment we just started executing.
                     // Ensure the timestamps are aligned.
                     m_genSegmentStartTime = m_segmentStartTime;
                }
            }
        }
    }

    if (m_running.load() && m_endOfPattern &&
        m_genSegmentIdx == m_segmentHead && !m_stopEventQueued) {
        if (queueStepEvent(m_genSegmentStartTime, STOP_MASK, 0)) {
            m_stopEventQueued = true;
        }
    }

    // If no more segments, we're idle
    if (m_segmentHead == m_segmentTail && m_endOfPattern && !m_running.load()) {
        stop(false);
    }

    m_processProfiler.addSample(micros() - now);
}

FillStopReason MotionPlanner::fillStepQueue(uint32_t horizonUs) {
    const uint32_t budgetStart = micros();
    const uint64_t horizon = plannerMicros() + horizonUs;
    auto finish = [&](FillStopReason reason) {
        m_genProfiler.addSample(micros() - budgetStart);
        return reason;
    };
    while (m_genSegmentIdx != m_segmentHead) {
        Segment& s = m_segments[m_genSegmentIdx];
        if (!s.calculated)
            return finish(FillStopReason::None);
        if (m_genSegmentStartTime > horizon) {
            queueHorizonMarker(static_cast<uint32_t>(horizon));
            return finish(FillStopReason::Horizon);
        }
        const uint64_t end = std::min(s.durationUs, horizon - m_genSegmentStartTime);
        while (s.nextSampleUs <= end) {
            if (micros() - budgetStart >= STEP_QUEUE_MAX_PROCESS_US)
                return finish(FillStopReason::TimeBudget);
            int32_t targetT, targetR;
            if (s.nextSampleUs == s.durationUs) {
                targetT = s.theta.targetSteps;
                targetR = s.rho.targetSteps;
            } else {
                const PathPoint point =
                    s.path.position(segmentDistance(s, s.nextSampleUs / 1000000.0));
                targetT = thetaToSteps(point.theta);
                targetR = rhoToSteps(point.rho);
            }
            const uint32_t eventTime =
                static_cast<uint32_t>(m_genSegmentStartTime + s.nextSampleUs);
            while (s.lastGenThetaSteps != targetT || s.lastGenRhoSteps != targetR) {
                uint8_t mask = 0, dir = 0;
                if (s.lastGenThetaSteps != targetT) {
                    mask |= 1;
                    if (targetT > s.lastGenThetaSteps)
                        dir |= 1;
                }
                if (s.lastGenRhoSteps != targetR) {
                    mask |= 2;
                    if (targetR > s.lastGenRhoSteps)
                        dir |= 2;
                }
                if (!queueStepEvent(eventTime, mask, dir))
                    return finish(FillStopReason::QueueFull);
                if (mask & 1)
                    s.lastGenThetaSteps += (dir & 1) ? 1 : -1;
                if (mask & 2)
                    s.lastGenRhoSteps += (dir & 2) ? 1 : -1;
            }
            // Advancing the clock does not mean the endpoint was evaluated.
            // Only completion of its actual pulse batch can finish a segment.
            if (s.nextSampleUs == s.durationUs) {
                s.generationComplete = true;
                m_genSegmentStartTime += s.durationUs;
                m_genSegmentIdx = (m_genSegmentIdx + 1) % SEGMENT_BUFFER_SIZE;
                break;
            }
            s.nextSampleUs = std::min(s.durationUs, s.nextSampleUs + STEP_TIMER_PERIOD_US);
        }
        if (!s.generationComplete) {
            queueHorizonMarker(static_cast<uint32_t>(m_genSegmentStartTime + end));
            return finish(FillStopReason::Horizon);
        }
    }
    if (m_endOfPattern && !m_stopEventQueued &&
        queueStepEvent(static_cast<uint32_t>(m_genSegmentStartTime), STOP_MASK, 0))
        m_stopEventQueued = true;
    return finish(FillStopReason::None);
}

int MotionPlanner::getStepQueueSpace() const {
    int head = m_stepQueueHead.load(std::memory_order_acquire);
    int tail = m_stepQueueTail.load(std::memory_order_acquire);

    if (head >= tail) {
        return STEP_QUEUE_SIZE - (head - tail) - 1;
    } else {
        return tail - head - 1;
    }
}

void MotionPlanner::queueHorizonMarker(uint32_t time) {
    // process() may run many times inside one timer period. Appending a no-op
    // on every Horizon return can fill 511 slots with near-identical times;
    // the ISR still spends a callback on each, delaying real STEP by ms.
    // Suppress closely spaced markers relative to ANY previously queued event.
    // Never edit a published slot: the consumer may already own it.
    if ((!m_lastQueuedEventTimeValid ||
         static_cast<int32_t>(time - m_lastQueuedEventTime) >=
             static_cast<int32_t>(STEP_HORIZON_MARKER_INTERVAL_US)) &&
        getStepQueueSpace() > 0) {
        queueStepEvent(time, 0, 0);
    }
}

bool MotionPlanner::queueStepEvent(uint32_t time, uint8_t stepMask, uint8_t dirMask) {
    const int head = m_stepQueueHead.load(std::memory_order_relaxed);
    const int nextHead = (head + 1) % STEP_QUEUE_SIZE;
    if (nextHead == m_stepQueueTail.load(std::memory_order_acquire)) {
        return false;  // Queue full
    }

    m_stepQueue[head].executeTime = time;
    m_stepQueue[head].stepMask = stepMask;
    m_stepQueue[head].dirMask = dirMask;
    m_stepQueueHead.store(nextHead, std::memory_order_release);
    m_lastQueuedEventTime = time;
    m_lastQueuedEventTimeValid = true;

    return true;
}

bool MotionPlanner::hasSpace() const {
    return ((m_segmentHead + 1) % SEGMENT_BUFFER_SIZE) != m_segmentTail;
}

bool MotionPlanner::isRunning() const {
    return m_running.load();
}

bool MotionPlanner::isIdle() const {
    bool queueEmpty = (m_stepQueueHead.load() == m_stepQueueTail.load());
    return !m_running.load() && (m_segmentHead == m_segmentTail) && queueEmpty;
}

void MotionPlanner::getCurrentPosition(double& theta, double& rho) const {
    theta = stepsToTheta(m_executedTSteps.load());
    rho = stepsToRho(m_executedRSteps.load());
}

void MotionPlanner::getCurrentVelocity(float& theta, float& rho) const {
    theta = rho = 0;
    if (!m_running.load() || m_segmentTail == m_segmentHead)
        return;
    const Segment& s = m_segments[m_segmentTail];
    if (!s.calculated)
        return;
    const double time = std::min(s.duration, (plannerMicros() - m_segmentStartTime) / 1000000.0);
    const PathPoint direction = s.path.tangent(segmentDistance(s, time));
    const double speed = segmentSpeed(s, time);
    theta = direction.theta * speed;
    rho = direction.rho * speed;
}

void MotionPlanner::resetTheta() {
    if (!isIdle())
        return;
    m_executedTSteps.store(0);
    m_queuedTSteps.store(0);
    m_targetTheta = 0;
    m_resumeReady = false;
    m_resumeTargetCount = 0;
}

void MotionPlanner::resetPosition(double theta, double rho) {
    stop();
    rho = std::clamp(rho, 0.0, double(m_maxRho));
    const double t = std::round(theta * m_stepsPerRadT), r = std::round(rho * m_stepsPerMmR);
    if (!std::isfinite(t) || !std::isfinite(r) || t < INT32_MIN || t > INT32_MAX || r > INT32_MAX)
        return;
    m_executedTSteps.store(int32_t(t));
    m_queuedTSteps.store(int32_t(t));
    m_executedRSteps.store(int32_t(r));
    m_queuedRSteps.store(int32_t(r));
    m_targetTheta = stepsToTheta(int32_t(t));
    m_targetRho = stepsToRho(int32_t(r));
}

size_t MotionPlanner::copyPendingTargets(double* theta, double* rho, size_t capacity) const {
    if (!theta || !rho)
        return 0;
    if (m_resumeCaptured) {
        const size_t count = std::min(capacity, m_resumeTargetCount);
        for (size_t i = 0; i < count; ++i) {
            theta[i] = m_resumeTargets[i].theta;
            rho[i] = m_resumeTargets[i].rho;
        }
        return count;
    }
    size_t count = 0;
    for (int i = m_genSegmentIdx; i != m_segmentHead && count < capacity;
         i = (i + 1) % SEGMENT_BUFFER_SIZE) {
        theta[count] = m_segments[i].targetTheta;
        rho[count] = m_segments[i].targetRho;
        ++count;
    }
    return count;
}

void MotionPlanner::setMotionLimits(float rMaxVel, float rMaxAccel, float rMaxJerk,
                                     float tMaxVel, float tMaxAccel, float tMaxJerk) {
    m_rMaxVel = rMaxVel;
    m_rMaxAccel = rMaxAccel;
    m_rMaxJerk = rMaxJerk;
    m_tMaxVel = tMaxVel;
    m_tMaxAccel = tMaxAccel;
    m_tMaxJerk = tMaxJerk;

    // Mark all non-executing segments as needing recalculation
    int idx = m_segmentTail;
    while (idx != m_segmentHead) {
        Segment& seg = m_segments[idx];
        // Only allow modifying segments that haven't started generating steps
        if (!seg.executing && seg.nextSampleUs == 0) {
            seg.calculated = false;
            seg.limitsCalculated = false;
            seg.generationComplete = false;
        }
        idx = (idx + 1) % SEGMENT_BUFFER_SIZE;
    }

    // Recalculate all pending segments with new limits
    if (m_segmentHead != m_segmentTail) {
        recalculate();
    }

    // Reset generation index to tail (it will fast-forward in process() if needed)
    m_genSegmentIdx = m_segmentTail;
    m_genSegmentStartTime = m_segmentStartTime;
}

void MotionPlanner::setSpeedMultiplier(float mult) {
    float newMult = std::max(0.1f, std::min(1.0f, mult));

    // Only recalculate if multiplier actually changed
    if (fabsf(newMult - m_speedMultiplier) < 0.001f) {
        return;
    }

    m_speedMultiplier = newMult;

    // Mark all non-executing segments as needing recalculation
    int idx = m_segmentTail;
    while (idx != m_segmentHead) {
        Segment& seg = m_segments[idx];
        // Only allow modifying segments that haven't started generating steps
        if (!seg.executing && seg.nextSampleUs == 0) {
            seg.calculated = false;
            seg.limitsCalculated = false;
            seg.generationComplete = false;
        }
        idx = (idx + 1) % SEGMENT_BUFFER_SIZE;
    }

    // Recalculate all pending segments with new speed
    recalculate();

    // Reset generation index to tail
    m_genSegmentIdx = m_segmentTail;
    m_genSegmentStartTime = m_segmentStartTime;
}

void MotionPlanner::setEndOfPattern(bool ending) {
    m_endOfPattern = ending;
}

void MotionPlanner::getDiagnostics(uint32_t& queueDepth, uint32_t& underruns) const {
    queueDepth = STEP_QUEUE_SIZE - 1 - getStepQueueSpace();
    underruns = m_underrunCount.load();
}

void MotionPlanner::getProfileData(uint32_t& maxProcessUs, uint32_t& maxIntervalUs, uint32_t& avgGenUs) {
    maxProcessUs = m_processProfiler.getMax();
    maxIntervalUs = m_intervalProfiler.getMax();
    avgGenUs = m_genProfiler.getAvg();

    // Reset max values after reading to capture transient spikes
    m_processProfiler.reset();
    m_intervalProfiler.reset();
    m_genProfiler.reset();
}

void MotionPlanner::getTelemetry(PlannerTelemetry& out) {
    int queueDepth = STEP_QUEUE_SIZE - 1 - getStepQueueSpace();
    if (queueDepth < 0) queueDepth = 0;
    out.queueDepth = static_cast<uint32_t>(queueDepth);
    out.minQueueDepth = (m_minQueueDepth == 0xFFFFFFFFu) ? out.queueDepth : m_minQueueDepth;
    out.underruns = m_underrunCount.load();
    uint32_t currentConsecutive = m_consecutiveUnderruns.load();
    out.maxConsecutiveUnderruns = m_maxConsecutiveUnderruns.exchange(currentConsecutive);
    out.completedCount = m_completedCount;
    out.stepMotionEpoch = m_stepMotionEpoch.load(std::memory_order_acquire);
    out.lastStepMotionStartUs =
        m_lastStepMotionStartUs.load(std::memory_order_acquire);
    out.lastStepMotionStopUs =
        m_lastStepMotionStopUs.load(std::memory_order_acquire);
    out.timerActive = m_timerActive.load();
    out.running = m_running.load();
    out.stepMotionActive = m_stepMotionActive.load(std::memory_order_acquire);
    snapshotAxisStepTiming(m_thetaStepTiming, out.thetaStepTiming);
    snapshotAxisStepTiming(m_rhoStepTiming, out.rhoStepTiming);
    out.callbackTimingValid = false;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        const uint32_t version = m_callbackTimingVersion.load(std::memory_order_seq_cst);
        if (version & 1U) continue;
        out.callbackGapCount = m_callbackGapCount.load(std::memory_order_seq_cst);
        out.maxCallbackGapUs = m_maxCallbackGapUs.load(std::memory_order_seq_cst);
        out.lastCallbackGapMicros = m_lastCallbackGapMicros.load(std::memory_order_seq_cst);
        out.lastCallbackGapUs = m_lastCallbackGapUs.load(std::memory_order_seq_cst);
        if (version == m_callbackTimingVersion.load(std::memory_order_seq_cst)) {
            out.callbackTimingValid = true;
            break;
        }
    }

    m_minQueueDepth = out.queueDepth;
}

void IRAM_ATTR MotionPlanner::recordNormalCallbackTiming(uint32_t now) {
    const uint32_t run = m_timingRunSerial.load(std::memory_order_relaxed);
    if (!m_previousCallbackValid || run != m_callbackTimingRunSerial) {
        m_callbackTimingRunSerial = run;
        m_previousCallbackValid = true;
    } else {
        const uint32_t gap = now - m_previousCallbackUs;
        if (gap > 2U * STEP_TIMER_PERIOD_US) {
            m_callbackTimingVersion.fetch_add(1, std::memory_order_seq_cst);
            m_callbackGapCount.fetch_add(1, std::memory_order_seq_cst);
            if (gap > m_maxCallbackGapUs.load(std::memory_order_seq_cst)) {
                m_maxCallbackGapUs.store(gap, std::memory_order_seq_cst);
            }
            m_lastCallbackGapMicros.store(now, std::memory_order_seq_cst);
            m_lastCallbackGapUs.store(gap, std::memory_order_seq_cst);
            m_callbackTimingVersion.fetch_add(1, std::memory_order_seq_cst);
        }
    }
    m_previousCallbackUs = now;
}

void IRAM_ATTR MotionPlanner::recordAxisStepTiming(AxisStepTimingState& timing,
                                       uint32_t epoch, uint32_t scheduledUs,
                                       uint32_t actualUs) {
    const int32_t lateness = static_cast<int32_t>(actualUs - scheduledUs);
    const bool intervalValid = timing.previousValid && timing.previousEpoch == epoch;
    const uint32_t plannedInterval = intervalValid ? scheduledUs - timing.previousScheduledUs : 0;
    const uint32_t actualInterval = intervalValid ? actualUs - timing.previousActualUs : 0;
    const uint32_t intervalError = actualInterval > plannedInterval
        ? actualInterval - plannedInterval : plannedInterval - actualInterval;
    const uint32_t threshold = 2U * STEP_TIMER_PERIOD_US;
    // Scheduled queue events are <half a micros wrap away. Any early edge is
    // anomalous. First STEP in an epoch deliberately has no interval metric.
    const bool outlier = lateness < 0 || static_cast<uint32_t>(lateness) > threshold
        || (intervalValid && intervalError > threshold);
    timing.version.fetch_add(1, std::memory_order_seq_cst);
    timing.stepCount.fetch_add(1, std::memory_order_seq_cst);
    if (lateness > 0 && static_cast<uint32_t>(lateness) > timing.maxLatenessUs.load(std::memory_order_seq_cst)) {
        timing.maxLatenessUs.store(static_cast<uint32_t>(lateness), std::memory_order_seq_cst);
    }
    if (intervalError > timing.maxAbsIntervalErrorUs.load(std::memory_order_seq_cst)) {
        timing.maxAbsIntervalErrorUs.store(intervalError, std::memory_order_seq_cst);
    }
    if (outlier) {
        timing.outlierCount.fetch_add(1, std::memory_order_seq_cst);
        timing.outlierEpoch.store(epoch, std::memory_order_seq_cst);
        timing.outlierScheduledMicros.store(scheduledUs, std::memory_order_seq_cst);
        timing.outlierActualMicros.store(actualUs, std::memory_order_seq_cst);
        timing.outlierPlannedIntervalUs.store(plannedInterval, std::memory_order_seq_cst);
        timing.outlierActualIntervalUs.store(actualInterval, std::memory_order_seq_cst);
        timing.outlierIntervalValid.store(intervalValid, std::memory_order_seq_cst);
    }
    timing.version.fetch_add(1, std::memory_order_seq_cst);
    timing.previousValid = true;
    timing.previousEpoch = epoch;
    timing.previousScheduledUs = scheduledUs;
    timing.previousActualUs = actualUs;
}

void MotionPlanner::snapshotAxisStepTiming(const AxisStepTimingState& timing,
                                         AxisStepTimingTelemetry& out) {
    out.valid = false;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        const uint32_t version = timing.version.load(std::memory_order_seq_cst);
        if (version & 1U) continue;
        out.stepCount = timing.stepCount.load(std::memory_order_seq_cst);
        out.outlierCount = timing.outlierCount.load(std::memory_order_seq_cst);
        out.maxLatenessUs = timing.maxLatenessUs.load(std::memory_order_seq_cst);
        out.maxAbsIntervalErrorUs = timing.maxAbsIntervalErrorUs.load(std::memory_order_seq_cst);
        out.outlierEpoch = timing.outlierEpoch.load(std::memory_order_seq_cst);
        out.outlierScheduledMicros = timing.outlierScheduledMicros.load(std::memory_order_seq_cst);
        out.outlierActualMicros = timing.outlierActualMicros.load(std::memory_order_seq_cst);
        out.outlierPlannedIntervalUs = timing.outlierPlannedIntervalUs.load(std::memory_order_seq_cst);
        out.outlierActualIntervalUs = timing.outlierActualIntervalUs.load(std::memory_order_seq_cst);
        out.outlierIntervalValid = timing.outlierIntervalValid.load(std::memory_order_seq_cst);
        if (version == timing.version.load(std::memory_order_seq_cst)) {
            out.valid = true;
            break;
        }
    }
}

float MotionPlanner::getMaxBoundaryVelocityDiscontinuity() const {
    double error = 0;
    for (int i = m_segmentTail; i != m_segmentHead; i = (i + 1) % SEGMENT_BUFFER_SIZE) {
        const int n = (i + 1) % SEGMENT_BUFFER_SIZE;
        if (n == m_segmentHead)
            break;
        const auto& a = m_segments[i];
        const auto& b = m_segments[n];
        if (a.calculated && b.calculated) {
            const PathPoint av = a.path.tangent(a.endDistance) * a.exitVelocity;
            const PathPoint bv = b.path.tangent(b.startDistance) * b.entryVelocity;
            error =
                std::max(error, std::max(std::abs(av.theta - bv.theta), std::abs(av.rho - bv.rho)));
        }
    }
    return static_cast<float>(error);
}

int32_t MotionPlanner::thetaToSteps(double theta) const {
    return static_cast<int32_t>(std::llround(theta * m_stepsPerRadT));
}
int32_t MotionPlanner::rhoToSteps(double rho) const {
    return static_cast<int32_t>(std::llround(rho * m_stepsPerMmR));
}
double MotionPlanner::stepsToTheta(int32_t steps) const { return steps / m_stepsPerRadT; }
double MotionPlanner::stepsToRho(int32_t steps) const { return steps / m_stepsPerMmR; }

// Native mock callback. Firmware uses the dedicated hardware ISR above.
void IRAM_ATTR MotionPlanner::stepTimerISR(void* arg) {
    MotionPlanner* planner = static_cast<MotionPlanner*>(arg);
    planner->handleStepTimer();
}

void IRAM_ATTR MotionPlanner::handleStepTimer() {
#ifdef NATIVE_BUILD
    if (m_homingRhoActive.load(std::memory_order_acquire)) {
        const uint32_t now = micros();
        const uint32_t nextStep =
            m_homingRhoNextStepUs.load(std::memory_order_relaxed);
        if (static_cast<int32_t>(now - nextStep) >= 0) {
            FastGPIO::setHigh(R_STEP_PIN);
            // Homing needs the same guaranteed pulse width as normal motion.
            // An instruction loop is not a calibrated time delay.
#ifndef NATIVE_BUILD
            delayMicroseconds(STEP_PULSE_WIDTH_US);
#endif
            FastGPIO::setLow(R_STEP_PIN);
            const uint32_t stepCount =
                m_homingRhoStepCount.fetch_add(
                    1, std::memory_order_release) + 1U;
            const uint32_t stepLimit =
                m_homingRhoStepLimit.load(std::memory_order_acquire);
            if (stepLimit > 0 && stepCount >= stepLimit) {
                m_homingRhoActive.store(false, std::memory_order_release);
                m_timerActive.store(false, std::memory_order_release);
                if (m_timerHandle != nullptr) {
                    esp_timer_stop((esp_timer_handle_t)m_timerHandle);
                }
                return;
            }

            const uint32_t interval =
                m_homingRhoIntervalUs.load(std::memory_order_acquire);
            uint32_t following = nextStep + interval;
            if (static_cast<int32_t>(now - following) >= 0) {
                following = now + interval;
            }
            m_homingRhoNextStepUs.store(following,
                                        std::memory_order_relaxed);
        }
        return;
    }
#endif

    // Normal-motion diagnostics do not observe or modify the homing ISR.
#ifdef NATIVE_BUILD
    uint32_t now = micros();
#else
    // Arduino micros()/delayMicroseconds() wrappers are flash-resident in this
    // SDK. Use the IRAM esp_timer clock and ROM delay directly in the ISR.
    uint32_t now = static_cast<uint32_t>(esp_timer_get_time());
#endif
    if (m_running.load(std::memory_order_relaxed)) recordNormalCallbackTiming(now);

    // Check if there's a step event ready to execute
    const int tail = m_stepQueueTail.load(std::memory_order_relaxed);
    if (m_stepQueueHead.load(std::memory_order_acquire) == tail) {
        if (m_running.load(std::memory_order_relaxed)) {
            m_underrunCount++;
            uint32_t consecutive = m_consecutiveUnderruns.fetch_add(1) + 1;
            uint32_t prevMax = m_maxConsecutiveUnderruns.load();
            while (consecutive > prevMax &&
                   !m_maxConsecutiveUnderruns.compare_exchange_weak(prevMax, consecutive)) {
            }
        }
        return;  // Queue empty
    }

    StepEvent& event = m_stepQueue[tail];

    // Check if it's time to execute this event
    // Handle wraparound by checking if we're within a reasonable window
    int32_t timeDiff = (int32_t)(event.executeTime - now);
    if (timeDiff > 0 && timeDiff < 1000000) {
        return;  // Not yet time
    }

    // Check for STOP sentinel
    if (event.stepMask & STOP_MASK) {
        if (m_stepMotionActive.exchange(false, std::memory_order_acq_rel)) {
            m_lastStepMotionStopUs.store(now, std::memory_order_release);
        }
        m_running.store(false, std::memory_order_relaxed);
        m_timerActive.store(false, std::memory_order_relaxed);
#ifdef NATIVE_BUILD
        if (m_timerHandle != nullptr) {
            esp_timer_stop((esp_timer_handle_t)m_timerHandle);
        }
#else
        m_normalHardwareActive.store(false, std::memory_order_release);
        timer_group_set_counter_enable_in_isr(TIMER_GROUP_1, TIMER_0, TIMER_PAUSE);
#endif
        // Consume event
        m_stepQueueTail.store((tail + 1) % STEP_QUEUE_SIZE, std::memory_order_release);
        return;
    }

    if ((event.stepMask & 0x03) != 0
            && !m_stepMotionActive.exchange(true, std::memory_order_acq_rel)) {
        m_lastStepMotionStartUs.store(now, std::memory_order_release);
        m_stepMotionEpoch.fetch_add(1, std::memory_order_acq_rel);
    }

    // Set direction pins first
    if (event.stepMask & 0x01) {
        FastGPIO::write(T_DIR_PIN, (event.dirMask & 0x01));
    }
    if (event.stepMask & 0x02) {
        FastGPIO::write(R_DIR_PIN, (event.dirMask & 0x02));
    }

    // The TMC2209 samples DIR before the rising STEP edge. Use the ESP32's
    // ISR-safe microsecond delay instead of an uncalibrated instruction loop;
    // the latter can become shorter than the driver's setup time depending on
    // compiler optimization and CPU frequency.
#ifndef NATIVE_BUILD
    ets_delay_us(DIR_SETUP_TIME_US);
#endif

    // Timestamp immediately before the rising writes, AFTER DIR setup. Keep
    // bookkeeping after STEP falls so it cannot extend setup or pulse width.
#ifdef NATIVE_BUILD
    const uint32_t preRiseUs = (event.stepMask & 0x03) ? micros() : 0;
#else
    const uint32_t preRiseUs = (event.stepMask & 0x03)
        ? static_cast<uint32_t>(esp_timer_get_time()) : 0;
#endif

    // Generate step pulses
    if (event.stepMask & 0x01) {
        FastGPIO::setHigh(T_STEP_PIN);
    }
    if (event.stepMask & 0x02) {
        FastGPIO::setHigh(R_STEP_PIN);
    }

    // Hold STEP high for the declared pulse width. This matters especially
    // after a direction transition, where marginal pulses can otherwise be
    // rejected by every driver sharing the STEP/DIR pair.
#ifndef NATIVE_BUILD
    ets_delay_us(STEP_PULSE_WIDTH_US);
#endif

    // End step pulses
    if (event.stepMask & 0x01) {
        FastGPIO::setLow(T_STEP_PIN);
        // Update executed position
        if (event.dirMask & 0x01) {
            m_executedTSteps++;
        } else {
            m_executedTSteps--;
        }
    }
    if (event.stepMask & 0x02) {
        FastGPIO::setLow(R_STEP_PIN);
        if (event.dirMask & 0x02) {
            m_executedRSteps++;
        } else {
            m_executedRSteps--;
        }
    }

    // Advance queue tail
    const uint8_t executedMask = event.stepMask;
    const uint32_t scheduledUs = event.executeTime;
    m_stepQueueTail.store((tail + 1) % STEP_QUEUE_SIZE, std::memory_order_release);
    m_consecutiveUnderruns.store(0);
    const uint32_t epoch = m_stepMotionEpoch.load(std::memory_order_relaxed);
    if (executedMask & 0x01) recordAxisStepTiming(m_thetaStepTiming, epoch, scheduledUs, preRiseUs);
    if (executedMask & 0x02) recordAxisStepTiming(m_rhoStepTiming, epoch, scheduledUs, preRiseUs);
}
