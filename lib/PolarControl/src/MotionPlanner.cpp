#include "MotionPlanner.hpp"
#include <cmath>
#include <cstdio>
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
    , m_timerHandle(nullptr)
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
    if (m_homingHardwareTimerReady) {
        timer_isr_callback_remove(TIMER_GROUP_1, TIMER_1);
        timer_deinit(TIMER_GROUP_1, TIMER_1);
    }
#endif
}

void MotionPlanner::init(int stepsPerMmR, int stepsPerRadT, float maxRho,
                         float rMaxVel, float rMaxAccel, float rMaxJerk,
                         float tMaxVel, float tMaxAccel, float tMaxJerk,
                         bool resetPosition) {
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

bool MotionPlanner::addSegment(float theta, float rho) {
    if (!hasSpace() || !std::isfinite(theta) || !std::isfinite(rho) ||
        m_stepsPerRadT <= 0 || m_stepsPerMmR <= 0 || m_maxRho <= 0.0f) {
        return false;
    }

    m_stopEventQueued = false;

    // Hold an unavailable axis at its last commanded position. This prevents
    // both GPIO pulses and fictitious position updates when a driver is
    // disconnected, while allowing the other axis to keep operating.
    if (!m_thetaAvailable.load()) {
        theta = stepsToTheta(m_queuedTSteps.load());
    }
    if (!m_rhoAvailable.load()) {
        rho = stepsToRho(m_queuedRSteps.load());
    }

    // Clamp rho to valid range
    rho = std::max(0.0f, std::min(rho, m_maxRho));
    const double thetaSteps = static_cast<double>(theta) * m_stepsPerRadT;
    if (thetaSteps < static_cast<double>(INT32_MIN) ||
        thetaSteps > static_cast<double>(INT32_MAX)) {
        return false;
    }

    const int32_t targetThetaSteps = thetaToSteps(theta);
    const int32_t targetRhoSteps = rhoToSteps(rho);
    // Repeated source coordinates should not insert a 10 ms zero-motion dwell
    // or force the surrounding lookahead velocities to zero.
    if (targetThetaSteps == m_queuedTSteps.load() &&
        targetRhoSteps == m_queuedRSteps.load()) {
        m_targetTheta = theta;
        m_targetRho = rho;
        return true;
    }

    Segment& seg = m_segments[m_segmentHead];
    seg.targetTheta = theta;
    seg.targetRho = rho;
    seg.calculated = false;
    seg.executing = false;
    seg.generationComplete = false;

    // Reset execution/generation state for new segment
    seg.thetaPhaseIdx = 0;
    seg.rhoPhaseIdx = 0;
    seg.lastGenTime = 0.0f;

    // Set start positions based on previous segment or queued position
    if (m_segmentHead != m_segmentTail) {
        int prevIdx = (m_segmentHead - 1 + SEGMENT_BUFFER_SIZE) % SEGMENT_BUFFER_SIZE;
        seg.theta.startSteps = m_segments[prevIdx].theta.targetSteps;
        seg.rho.startSteps = m_segments[prevIdx].rho.targetSteps;
    } else {
        seg.theta.startSteps = m_queuedTSteps.load();
        seg.rho.startSteps = m_queuedRSteps.load();
    }

    // Initialize generation counters to segment start
    seg.lastGenThetaSteps = seg.theta.startSteps;
    seg.lastGenRhoSteps = seg.rho.startSteps;

    // Calculate target steps
    seg.theta.targetSteps = targetThetaSteps;
    seg.rho.targetSteps = targetRhoSteps;

    // Calculate deltas
    const int64_t thetaDelta = static_cast<int64_t>(seg.theta.targetSteps) - seg.theta.startSteps;
    const int64_t rhoDelta = static_cast<int64_t>(seg.rho.targetSteps) - seg.rho.startSteps;
    if (thetaDelta < INT32_MIN || thetaDelta > INT32_MAX ||
        rhoDelta < INT32_MIN || rhoDelta > INT32_MAX) {
        return false;
    }
    seg.theta.deltaSteps = static_cast<int32_t>(thetaDelta);
    seg.rho.deltaSteps = static_cast<int32_t>(rhoDelta);

    seg.theta.direction = (seg.theta.deltaSteps > 0) ? 1 : ((seg.theta.deltaSteps < 0) ? -1 : 0);
    seg.rho.direction = (seg.rho.deltaSteps > 0) ? 1 : ((seg.rho.deltaSteps < 0) ? -1 : 0);

    // Convert to physical units
    seg.theta.deltaUnits = stepsToTheta(seg.theta.deltaSteps);
    seg.rho.deltaUnits = stepsToRho(seg.rho.deltaSteps);

    // Update target positions
    m_targetTheta = theta;
    m_targetRho = rho;

    // Advance head
    m_segmentHead = (m_segmentHead + 1) % SEGMENT_BUFFER_SIZE;

    // Update queued positions
    m_queuedTSteps.store(seg.theta.targetSteps);
    m_queuedRSteps.store(seg.rho.targetSteps);

    return true;
}

void MotionPlanner::setAxisAvailability(bool thetaAvailable, bool rhoAvailable) {
    m_thetaAvailable.store(thetaAvailable);
    m_rhoAvailable.store(rhoAvailable);
}

static bool calculateProfileForDuration(float distance, float vStart, float vEnd,
                                        float vMax, float aMax, float jMax,
                                        float duration, SCurve::Profile& out) {
    SCurve::Profile fastest{};
    if (!SCurve::calculate(distance, vStart, vEnd, vMax, aMax, jMax, fastest)) {
        return false;
    }
    if (fastest.totalTime >= duration - 0.000001f) {
        out = fastest;
        return true;
    }

    const float minimumPeak = std::max(std::max(vStart, vEnd), vMax * 0.000001f);
    SCurve::Profile slowest{};
    if (!SCurve::calculate(distance, vStart, vEnd, minimumPeak,
                           aMax, jMax, slowest) || slowest.totalTime < duration) {
        return false;
    }

    float low = minimumPeak;
    float high = vMax;
    SCurve::Profile closest = fastest;
    float closestError = fabsf(fastest.totalTime - duration);
    for (int i = 0; i < 36; ++i) {
        const float peak = (low + high) * 0.5f;
        SCurve::Profile candidate{};
        if (!SCurve::calculate(distance, vStart, vEnd, peak,
                               aMax, jMax, candidate)) {
            low = peak;
            continue;
        }
        const float error = fabsf(candidate.totalTime - duration);
        if (error < closestError) {
            closest = candidate;
            closestError = error;
        }
        if (candidate.totalTime > duration) {
            low = peak;
        } else {
            high = peak;
        }
    }
    out = closest;
    return true;
}

void MotionPlanner::calculateSegmentProfile(Segment& seg) {
    // Apply speed multiplier to velocity limits only
    float tMaxVel = m_tMaxVel * m_speedMultiplier;
    float rMaxVel = m_rMaxVel * m_speedMultiplier;
    // Calculate S-curve for theta axis
    float thetaDist = fabsf(seg.theta.deltaUnits);
    if (thetaDist > 0.0001f) {
        if (!SCurve::calculate(
            thetaDist,
            seg.thetaEntryVel,
            seg.thetaExitVel,
            tMaxVel,
            m_tMaxAccel,
            m_tMaxJerk,
            seg.theta.profile
        )) {
            // Never publish the zero-initialized failed profile: that would
            // collapse every step onto the segment endpoint timestamp.
            seg.thetaEntryVel = 0.0f;
            seg.thetaExitVel = 0.0f;
            SCurve::calculate(thetaDist, 0.0f, 0.0f, tMaxVel,
                              m_tMaxAccel, m_tMaxJerk, seg.theta.profile);
        }
    } else {
        // Zero motion - zero-duration profile
        seg.theta.profile = {};
    }

    // Calculate S-curve for rho axis
    float rhoDist = fabsf(seg.rho.deltaUnits);
    if (rhoDist > 0.0001f) {
        if (!SCurve::calculate(
            rhoDist,
            seg.rhoEntryVel,
            seg.rhoExitVel,
            rMaxVel,
            m_rMaxAccel,
            m_rMaxJerk,
            seg.rho.profile
        )) {
            seg.rhoEntryVel = 0.0f;
            seg.rhoExitVel = 0.0f;
            SCurve::calculate(rhoDist, 0.0f, 0.0f, rMaxVel,
                              m_rMaxAccel, m_rMaxJerk, seg.rho.profile);
        }
    } else {
        // Zero motion - zero-duration profile
        seg.rho.profile = {};
    }

    // Solve both axes directly for one wall-clock duration. Reducing the peak
    // speed of the faster axis preserves its entry/exit velocities; stretching
    // a completed profile after the fact does not.
    for (int attempt = 0; attempt < 3; ++attempt) {
        const float syncDuration = std::max(MIN_SEGMENT_DURATION,
            std::max(seg.theta.profile.totalTime, seg.rho.profile.totalTime));
        bool thetaOk = true;
        bool rhoOk = true;
        SCurve::Profile thetaFixed = seg.theta.profile;
        SCurve::Profile rhoFixed = seg.rho.profile;
        if (thetaDist > 0.0001f) {
            thetaOk = calculateProfileForDuration(thetaDist, seg.thetaEntryVel,
                seg.thetaExitVel, tMaxVel, m_tMaxAccel, m_tMaxJerk,
                syncDuration, thetaFixed);
        }
        if (rhoDist > 0.0001f) {
            rhoOk = calculateProfileForDuration(rhoDist, seg.rhoEntryVel,
                seg.rhoExitVel, rMaxVel, m_rMaxAccel, m_rMaxJerk,
                syncDuration, rhoFixed);
        }
        if (thetaOk && rhoOk) {
            seg.theta.profile = thetaFixed;
            seg.rho.profile = rhoFixed;
            break;
        }

        // A long shared duration can be incompatible with a high boundary
        // velocity on a very short axis move. Stop only that axis at the
        // surrounding waypoints, then let boundary reconciliation propagate
        // the safe constraint to its neighbors.
        if (!thetaOk && thetaDist > 0.0001f) {
            seg.thetaEntryVel = 0.0f;
            seg.thetaExitVel = 0.0f;
            SCurve::calculate(thetaDist, 0.0f, 0.0f, tMaxVel,
                              m_tMaxAccel, m_tMaxJerk, seg.theta.profile);
        }
        if (!rhoOk && rhoDist > 0.0001f) {
            seg.rhoEntryVel = 0.0f;
            seg.rhoExitVel = 0.0f;
            SCurve::calculate(rhoDist, 0.0f, 0.0f, rMaxVel,
                              m_rMaxAccel, m_rMaxJerk, seg.rho.profile);
        }
    }

    const float thetaTime = seg.theta.profile.totalTime;
    const float rhoTime = seg.rho.profile.totalTime;
    seg.duration = std::max(MIN_SEGMENT_DURATION, std::max(thetaTime, rhoTime));
    // Bisection is float-limited, so retain only the tiny final correction.
    seg.theta.timeScale = (thetaTime > 0.0001f) ? seg.duration / thetaTime : 1.0f;
    seg.rho.timeScale = (rhoTime > 0.0001f) ? seg.duration / rhoTime : 1.0f;

    seg.thetaPhaseIdx = 0;
    seg.rhoPhaseIdx = 0;

    seg.lastGenTime = 0.0f;
    seg.lastGenThetaSteps = seg.theta.startSteps;
    seg.lastGenRhoSteps = seg.rho.startSteps;

    seg.calculated = true;
}

void MotionPlanner::recalculate() {
    if (m_segmentHead == m_segmentTail) return;

    // Apply speed multiplier to velocity limits
    float tMaxVel = m_tMaxVel * m_speedMultiplier;
    float rMaxVel = m_rMaxVel * m_speedMultiplier;
    auto isUncommitted = [](const Segment& seg) {
        return !seg.executing && seg.lastGenTime < 0.0001f;
    };
    auto actualExitVelocity = [](const AxisProfile& axis, float segmentDuration) {
        if (axis.profile.totalTime <= 0.0f || axis.timeScale <= 0.0f) return 0.0f;
        const float profileTime = std::min(axis.profile.totalTime,
                                           segmentDuration / axis.timeScale);
        return SCurve::getVelocity(axis.profile, profileTime) / axis.timeScale;
    };

    // =========================================================================
    // PASS 1: Forward pass - Set entry velocities from previous segment's exit
    // =========================================================================

    // Get starting velocity from currently executing segment or 0
    float prevThetaVel = 0.0f;
    float prevRhoVel = 0.0f;
    int8_t prevThetaDir = 0;
    int8_t prevRhoDir = 0;

    if (m_running && m_segmentHead != m_segmentTail) {
        Segment& current = m_segments[m_segmentTail];
        if (current.executing && current.calculated) {
            prevThetaVel = actualExitVelocity(current.theta, current.duration);
            prevRhoVel = actualExitVelocity(current.rho, current.duration);
            prevThetaDir = current.theta.direction;
            prevRhoDir = current.rho.direction;
        }
    }

    int idx = m_segmentTail;
    while (idx != m_segmentHead) {
        Segment& seg = m_segments[idx];

        if (isUncommitted(seg)) {
            // Check for direction reversals - if direction changes, entry vel must be 0
            bool thetaReverses = (prevThetaDir != 0) && (seg.theta.direction != 0) &&
                                 (prevThetaDir != seg.theta.direction);
            bool rhoReverses = (prevRhoDir != 0) && (seg.rho.direction != 0) &&
                               (prevRhoDir != seg.rho.direction);

            const bool thetaMoves = fabsf(seg.theta.deltaUnits) > 0.0001f;
            const bool rhoMoves = fabsf(seg.rho.deltaUnits) > 0.0001f;
            seg.thetaEntryVel = (!thetaMoves || thetaReverses) ? 0.0f : std::min(prevThetaVel, tMaxVel);
            seg.rhoEntryVel = (!rhoMoves || rhoReverses) ? 0.0f : std::min(prevRhoVel, rMaxVel);

            // Set initial exit velocities to max (will be constrained in backward pass)
            seg.thetaExitVel = thetaMoves ? tMaxVel : 0.0f;
            seg.rhoExitVel = rhoMoves ? rMaxVel : 0.0f;

            // Constrain exit velocity based on what is achievable from entry velocity (Forward Pass)
            float thetaDist = fabsf(seg.theta.deltaUnits);
            if (thetaDist > 0.0001f) {
                float maxExit = SCurve::maxAchievableExitVelocity(
                    thetaDist, seg.thetaEntryVel, tMaxVel, m_tMaxAccel, m_tMaxJerk);
                if (seg.thetaExitVel > maxExit) {
                    seg.thetaExitVel = maxExit;
                }
            }

            float rhoDist = fabsf(seg.rho.deltaUnits);
            if (rhoDist > 0.0001f) {
                float maxExit = SCurve::maxAchievableExitVelocity(
                    rhoDist, seg.rhoEntryVel, rMaxVel, m_rMaxAccel, m_rMaxJerk);
                if (seg.rhoExitVel > maxExit) {
                    seg.rhoExitVel = maxExit;
                }
            }
        }

        if (!isUncommitted(seg) && seg.calculated) {
            prevThetaVel = actualExitVelocity(seg.theta, seg.duration);
            prevRhoVel = actualExitVelocity(seg.rho, seg.duration);
        } else {
            prevThetaVel = seg.thetaExitVel;
            prevRhoVel = seg.rhoExitVel;
        }
        prevThetaDir = seg.theta.direction;
        prevRhoDir = seg.rho.direction;

        idx = (idx + 1) % SEGMENT_BUFFER_SIZE;
    }

    // =========================================================================
    // PASS 2: Backward pass - Constrain exit velocities based on next segment
    // =========================================================================

    // Find the last segment index
    int lastIdx = (m_segmentHead - 1 + SEGMENT_BUFFER_SIZE) % SEGMENT_BUFFER_SIZE;

    // The lookahead tail is provisional until a successor exists. Planning it
    // to stop makes producer stalls safe; a later recalc may raise this speed
    // only while the profile is still wholly uncommitted.
    if (isUncommitted(m_segments[lastIdx])) {
        m_segments[lastIdx].thetaExitVel = 0.0f;
        m_segments[lastIdx].rhoExitVel = 0.0f;
    }

    // Work backwards, propagating constraints
    idx = lastIdx;
    while (true) {
        Segment& seg = m_segments[idx];

        if (!isUncommitted(seg)) {
            // Generated profiles are immutable: their events may already be
            // queued for execution and cannot be retroactively re-planned.
            if (idx == m_segmentTail) break; // Reached start
            idx = (idx - 1 + SEGMENT_BUFFER_SIZE) % SEGMENT_BUFFER_SIZE;
            continue;
        }

        // Get next segment's entry requirements (which become our exit constraints)
        int nextIdx = (idx + 1) % SEGMENT_BUFFER_SIZE;
        if (nextIdx != m_segmentHead) {
            Segment& nextSeg = m_segments[nextIdx];

            // Check for direction reversals - if next segment reverses, we must exit at 0
            bool thetaReverses = (seg.theta.direction != 0) && (nextSeg.theta.direction != 0) &&
                                 (seg.theta.direction != nextSeg.theta.direction);
            bool rhoReverses = (seg.rho.direction != 0) && (nextSeg.rho.direction != 0) &&
                               (seg.rho.direction != nextSeg.rho.direction);

            if (thetaReverses) {
                seg.thetaExitVel = 0.0f;
            } else {
                // Exit velocity can't exceed next segment's entry velocity
                seg.thetaExitVel = std::min(seg.thetaExitVel, nextSeg.thetaEntryVel);
            }

            if (rhoReverses) {
                seg.rhoExitVel = 0.0f;
            } else {
                seg.rhoExitVel = std::min(seg.rhoExitVel, nextSeg.rhoEntryVel);
            }
        }

        // Now check if our entry velocity can achieve the required exit velocity
        // If not, we need to reduce entry velocity and propagate backward

        float thetaDist = fabsf(seg.theta.deltaUnits);
        if (thetaDist > 0.0001f) {
            float maxEntry = SCurve::maxAchievableEntryVelocity(
                thetaDist, seg.thetaExitVel, tMaxVel, m_tMaxAccel, m_tMaxJerk);
            if (seg.thetaEntryVel > maxEntry) {
                seg.thetaEntryVel = maxEntry;
            }
        }

        float rhoDist = fabsf(seg.rho.deltaUnits);
        if (rhoDist > 0.0001f) {
            float maxEntry = SCurve::maxAchievableEntryVelocity(
                rhoDist, seg.rhoExitVel, rMaxVel, m_rMaxAccel, m_rMaxJerk);
            if (seg.rhoEntryVel > maxEntry) {
                seg.rhoEntryVel = maxEntry;
            }
        }

        // Propagate entry velocity constraint to previous segment's exit
        if (idx != m_segmentTail) {
            int prevIdx = (idx - 1 + SEGMENT_BUFFER_SIZE) % SEGMENT_BUFFER_SIZE;
            Segment& prevSeg = m_segments[prevIdx];
            if (isUncommitted(prevSeg)) {
                prevSeg.thetaExitVel = std::min(prevSeg.thetaExitVel, seg.thetaEntryVel);
                prevSeg.rhoExitVel = std::min(prevSeg.rhoExitVel, seg.rhoEntryVel);
            }
        }

        if (idx == m_segmentTail) break; // Finished all
        idx = (idx - 1 + SEGMENT_BUFFER_SIZE) % SEGMENT_BUFFER_SIZE;
    }

    // =========================================================================
    // PASS 3: Calculate actual S-curve profiles with final velocities
    // =========================================================================

    // Pass 3: Calculate actual profiles
    idx = m_segmentTail;
    while (idx != m_segmentHead) {
        Segment& seg = m_segments[idx];
        // Only calculate if not already executing AND no steps generated yet
        if (isUncommitted(seg)) {
            calculateSegmentProfile(seg);
        }
        idx = (idx + 1) % SEGMENT_BUFFER_SIZE;
    }

    // Rebuild all mutable profiles after a boundary adjustment.
    auto rebuildUncommitted = [&]() {
        int rebuildIdx = m_segmentTail;
        while (rebuildIdx != m_segmentHead) {
            Segment& seg = m_segments[rebuildIdx];
            if (isUncommitted(seg)) {
                const float thetaDist = fabsf(seg.theta.deltaUnits);
                const float rhoDist = fabsf(seg.rho.deltaUnits);
                if (thetaDist <= 0.0001f) {
                    seg.thetaEntryVel = 0.0f;
                    seg.thetaExitVel = 0.0f;
                } else {
                    seg.thetaExitVel = std::min(seg.thetaExitVel,
                        SCurve::maxAchievableExitVelocity(thetaDist, seg.thetaEntryVel,
                            tMaxVel, m_tMaxAccel, m_tMaxJerk));
                    seg.thetaEntryVel = std::min(seg.thetaEntryVel,
                        SCurve::maxAchievableEntryVelocity(thetaDist, seg.thetaExitVel,
                            tMaxVel, m_tMaxAccel, m_tMaxJerk));
                }
                if (rhoDist <= 0.0001f) {
                    seg.rhoEntryVel = 0.0f;
                    seg.rhoExitVel = 0.0f;
                } else {
                    seg.rhoExitVel = std::min(seg.rhoExitVel,
                        SCurve::maxAchievableExitVelocity(rhoDist, seg.rhoEntryVel,
                            rMaxVel, m_rMaxAccel, m_rMaxJerk));
                    seg.rhoEntryVel = std::min(seg.rhoEntryVel,
                        SCurve::maxAchievableEntryVelocity(rhoDist, seg.rhoExitVel,
                            rMaxVel, m_rMaxAccel, m_rMaxJerk));
                }
                calculateSegmentProfile(seg);
            }
            rebuildIdx = (rebuildIdx + 1) % SEGMENT_BUFFER_SIZE;
        }
    };

    // Measure what the motors are actually commanded to do in wall time. The
    // pre-scaled EntryVel/ExitVel fields are not a valid convergence metric.
    auto maxBoundaryResidualSteps = [&]() {
        float maximum = 0.0f;
        int currentIdx = m_segmentTail;
        while (currentIdx != m_segmentHead) {
            const int nextIdx = (currentIdx + 1) % SEGMENT_BUFFER_SIZE;
            if (nextIdx == m_segmentHead) break;
            const Segment& current = m_segments[currentIdx];
            const Segment& next = m_segments[nextIdx];
            if (current.calculated && next.calculated) {
                const float thetaExit = actualExitVelocity(current.theta, current.duration) * current.theta.direction;
                const float thetaEntry = (next.theta.profile.totalTime > 0.0f && next.theta.timeScale > 0.0f)
                    ? next.theta.profile.v[0] * next.theta.direction / next.theta.timeScale : 0.0f;
                const float rhoExit = actualExitVelocity(current.rho, current.duration) * current.rho.direction;
                const float rhoEntry = (next.rho.profile.totalTime > 0.0f && next.rho.timeScale > 0.0f)
                    ? next.rho.profile.v[0] * next.rho.direction / next.rho.timeScale : 0.0f;
                maximum = std::max(maximum, fabsf(thetaExit - thetaEntry) * m_stepsPerRadT);
                maximum = std::max(maximum, fabsf(rhoExit - rhoEntry) * m_stepsPerMmR);
            }
            currentIdx = nextIdx;
        }
        return maximum;
    };

    int mutableCount = 0;
    idx = m_segmentTail;
    while (idx != m_segmentHead) {
        if (isUncommitted(m_segments[idx])) ++mutableCount;
        idx = (idx + 1) % SEGMENT_BUFFER_SIZE;
    }
    const int maxIterations = std::max(8, mutableCount * 2);
    // Leave numerical headroom below the externally asserted 0.01 step/s
    // continuity threshold.
    static constexpr float kBoundaryToleranceStepsPerSecond = 0.005f;

    auto reconcile = [&]() {
        idx = m_segmentTail;
        while (idx != m_segmentHead) {
            const int nextIdx = (idx + 1) % SEGMENT_BUFFER_SIZE;
            if (nextIdx == m_segmentHead) break;
            Segment& current = m_segments[idx];
            Segment& next = m_segments[nextIdx];
            if (isUncommitted(next) && current.calculated && next.calculated) {
                auto synchronizeAxis = [&](float& exitVelocity, const AxisProfile& exitAxis,
                                           bool exitMutable, float& entryVelocity,
                                           const AxisProfile& entryAxis) {
                    const bool continuousDirection = exitAxis.direction != 0 &&
                        exitAxis.direction == entryAxis.direction;
                    const float entryScale = std::max(1.0f, entryAxis.timeScale);
                    float common = 0.0f;
                    if (continuousDirection) {
                        if (exitMutable) {
                            const float exitScale = std::max(1.0f, exitAxis.timeScale);
                            common = std::min(exitVelocity / exitScale,
                                              entryVelocity / entryScale);
                            exitVelocity = common * exitScale;
                        } else {
                            common = actualExitVelocity(exitAxis, current.duration);
                        }
                    } else if (exitMutable) {
                        exitVelocity = 0.0f;
                    }
                    entryVelocity = common * entryScale;
                };
                const bool currentMutable = isUncommitted(current);
                synchronizeAxis(current.thetaExitVel, current.theta, currentMutable,
                                next.thetaEntryVel, next.theta);
                synchronizeAxis(current.rhoExitVel, current.rho, currentMutable,
                                next.rhoEntryVel, next.rho);
            }
            idx = nextIdx;
        }
        rebuildUncommitted();
    };

    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        reconcile();
        if (maxBoundaryResidualSteps() <= kBoundaryToleranceStepsPerSecond) return;
    }

    // A fixed-point solution is not guaranteed for independent time-scaled
    // profiles. Preserve smoothness deterministically by stopping only the
    // offending axes at unconverged junctions, then try once more.
    idx = m_segmentTail;
    while (idx != m_segmentHead) {
        const int nextIdx = (idx + 1) % SEGMENT_BUFFER_SIZE;
        if (nextIdx == m_segmentHead) break;
        Segment& current = m_segments[idx];
        Segment& next = m_segments[nextIdx];
        if (isUncommitted(next) && current.calculated && next.calculated) {
            const float thetaExit = actualExitVelocity(current.theta, current.duration) * current.theta.direction;
            const float thetaEntry = next.theta.profile.totalTime > 0.0f
                ? next.theta.profile.v[0] * next.theta.direction / next.theta.timeScale : 0.0f;
            const float rhoExit = actualExitVelocity(current.rho, current.duration) * current.rho.direction;
            const float rhoEntry = next.rho.profile.totalTime > 0.0f
                ? next.rho.profile.v[0] * next.rho.direction / next.rho.timeScale : 0.0f;
            if (fabsf(thetaExit - thetaEntry) * m_stepsPerRadT > kBoundaryToleranceStepsPerSecond) {
                if (isUncommitted(current)) current.thetaExitVel = 0.0f;
                next.thetaEntryVel = 0.0f;
            }
            if (fabsf(rhoExit - rhoEntry) * m_stepsPerMmR > kBoundaryToleranceStepsPerSecond) {
                if (isUncommitted(current)) current.rhoExitVel = 0.0f;
                next.rhoEntryVel = 0.0f;
            }
        }
        idx = nextIdx;
    }
    rebuildUncommitted();
    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        if (maxBoundaryResidualSteps() <= kBoundaryToleranceStepsPerSecond) return;
        reconcile();
    }

    // Last-resort safe plan: every mutable junction stops. This is slower but
    // cannot publish a velocity discontinuity after a failed solve.
    idx = m_segmentTail;
    while (idx != m_segmentHead) {
        const int nextIdx = (idx + 1) % SEGMENT_BUFFER_SIZE;
        if (nextIdx == m_segmentHead) break;
        Segment& current = m_segments[idx];
        Segment& next = m_segments[nextIdx];
        if (isUncommitted(current)) {
            current.thetaExitVel = 0.0f;
            current.rhoExitVel = 0.0f;
        }
        if (isUncommitted(next)) {
            next.thetaEntryVel = 0.0f;
            next.rhoEntryVel = 0.0f;
        }
        idx = nextIdx;
    }
    rebuildUncommitted();
}

void MotionPlanner::stopGracefully() {
    if (!m_running || isIdle()) {
        stop();
        return;
    }

    // If all segments are already generated, preserve those events and append
    // a stop marker at their theoretical end. This is common for short moves
    // and avoids indexing a stale slot at m_segmentHead.
    if (m_genSegmentIdx == m_segmentHead) {
        m_endOfPattern = true;
        if (!m_stopEventQueued && queueStepEvent(m_genSegmentStartTime, STOP_MASK, 0)) {
            m_stopEventQueued = true;
        }
        return;
    }

    // 1. Identify current generation state
    // Note: m_genSegmentIdx points to the segment we are currently generating steps for
    // or about to generate steps for.
    Segment& currentGen = m_segments[m_genSegmentIdx];

    // If current segment hasn't started generating, we can just clear buffer
    if (currentGen.lastGenTime < 0.000001f) {
        m_segmentHead = m_genSegmentIdx; // Keep it as head? No, discard it.
        // Actually if we haven't generated anything for it, we are effectively at the end
        // of the previous segment (which is fully generated).
        // So we can just clear everything after the previous segment.
        m_segmentHead = m_genSegmentIdx;
        m_endOfPattern = true;
        recalculate();
        return;
    }

    // 2. Calculate current velocities at the point of interruption
    float vTheta = 0.0f;
    float vRho = 0.0f;

    // Need to use the profile to get velocity at lastGenTime
    // Note: Profiles calculate positive speed. Need direction.

    float t = currentGen.lastGenTime;

    if (currentGen.theta.profile.totalDistance > 0.0f) {
        float pt = t / currentGen.theta.timeScale;
        float v = SCurve::getVelocity(currentGen.theta.profile, pt) / currentGen.theta.timeScale;
        vTheta = v * static_cast<float>(currentGen.theta.direction);
    }

    if (currentGen.rho.profile.totalDistance > 0.0f) {
        float pt = t / currentGen.rho.timeScale;
        float v = SCurve::getVelocity(currentGen.rho.profile, pt) / currentGen.rho.timeScale;
        vRho = v * static_cast<float>(currentGen.rho.direction);
    }

    // 3. Update queued positions to match where we are interrupting
    m_queuedTSteps.store(currentGen.lastGenThetaSteps);
    m_queuedRSteps.store(currentGen.lastGenRhoSteps);

    // Update target steps of the terminated segment so next segment chains correctly
    // (addSegment uses the previous segment's target as start)
    currentGen.theta.targetSteps = currentGen.lastGenThetaSteps;
    currentGen.rho.targetSteps = currentGen.lastGenRhoSteps;

    // 4. Terminate the current segment
    // We force it to be "complete" so fillStepQueue moves on
    currentGen.duration = t;
    currentGen.generationComplete = true;

    // 5. Reset buffer pointers to discard future segments
    // The current segment becomes the tail (it's done generating)
    // The head moves to the next slot, which will be our braking segment
    m_segmentHead = (m_genSegmentIdx + 1) % SEGMENT_BUFFER_SIZE;

    // 6. Calculate stopping distances
    // Apply speed multiplier to limits?
    // Limits in planner are raw. m_speedMultiplier is applied during calculation.
    // SCurve::decelerationDistance needs RAW limits if we are passing RAW velocity?
    // Wait, vTheta is RAW velocity (physical units).
    // We want to stop using current limits.
    // Recalculate will apply multiplier. We just need a target.

    float tMaxAccel = m_tMaxAccel * m_speedMultiplier; // Approx
    float rMaxAccel = m_rMaxAccel * m_speedMultiplier;
    float tMaxJerk = m_tMaxJerk; // Jerk usually not scaled by speed mult in this codebase?
    float rMaxJerk = m_rMaxJerk;

    // Note: SCurve::decelerationDistance(vStart, vEnd, ...)
    // vStart is signed? No, SCurve usually deals with magnitudes for distance calc?
    // SCurve::decelerationDistance(vStart, vEnd) assumes positive.

    float stopDistT = SCurve::decelerationDistance(fabsf(vTheta), 0.0f, tMaxAccel, tMaxJerk);
    float stopDistR = SCurve::decelerationDistance(fabsf(vRho), 0.0f, rMaxAccel, rMaxJerk);

    // If both axes are moving, the one with the shorter natural stop must
    // cruise briefly before it decelerates. Stretching a minimum-distance
    // deceleration profile changes its entry velocity, which breaks boundary
    // continuity and can eventually force the generic lookahead fallback to
    // stop both axes abruptly.
    SCurve::Profile thetaStopProfile{};
    SCurve::Profile rhoStopProfile{};
    float thetaStopTime = 0.0f;
    float rhoStopTime = 0.0f;
    if (stopDistT > 0.0f && SCurve::calculate(
            stopDistT, fabsf(vTheta), 0.0f,
            std::max(fabsf(vTheta), m_tMaxVel * m_speedMultiplier),
            tMaxAccel, tMaxJerk, thetaStopProfile)) {
        thetaStopTime = thetaStopProfile.totalTime;
    }
    if (stopDistR > 0.0f && SCurve::calculate(
            stopDistR, fabsf(vRho), 0.0f,
            std::max(fabsf(vRho), m_rMaxVel * m_speedMultiplier),
            rMaxAccel, rMaxJerk, rhoStopProfile)) {
        rhoStopTime = rhoStopProfile.totalTime;
    }
    const float commonStopTime = std::max(thetaStopTime, rhoStopTime);
    stopDistT += fabsf(vTheta) * std::max(0.0f, commonStopTime - thetaStopTime);
    stopDistR += fabsf(vRho) * std::max(0.0f, commonStopTime - rhoStopTime);

    // 7. Calculate target position
    // Direction of stop is same as velocity
    float dirT = (vTheta > 0) ? 1.0f : -1.0f;
    float dirR = (vRho > 0) ? 1.0f : -1.0f;

    // Round the braking distance outward in step space. addSegment() converts
    // targets by truncating toward zero; feeding it the exact floating-point
    // deceleration distance can therefore make the quantized move fractionally
    // shorter than the minimum distance required for its non-zero entry speed.
    // SCurve::calculate() correctly rejects that impossible profile, but the
    // generic fallback then creates a rest-to-rest move: telemetry (and the
    // motor) sees an abrupt drop to zero followed by a second acceleration.
    const int32_t thetaBrakeSteps = static_cast<int32_t>(
        ceilf(stopDistT * static_cast<float>(m_stepsPerRadT)));
    const int32_t rhoBrakeSteps = static_cast<int32_t>(
        ceilf(stopDistR * static_cast<float>(m_stepsPerMmR)));
    const int32_t targetThetaSteps = m_queuedTSteps.load() +
        ((vTheta > 0.0f) ? thetaBrakeSteps : -thetaBrakeSteps);
    int32_t targetRhoSteps = m_queuedRSteps.load() +
        ((vRho > 0.0f) ? rhoBrakeSteps : -rhoBrakeSteps);
    targetRhoSteps = std::max(0, std::min(targetRhoSteps,
        static_cast<int32_t>(m_maxRho * m_stepsPerMmR)));
    float targetT = stepsToTheta(targetThetaSteps);
    float targetR = stepsToRho(targetRhoSteps);

    // 8. Add the braking segment
    // This adds it at m_segmentHead
    addSegment(targetT, targetR);

    // 9. Force velocity continuity
    // We manually set entry velocity so recalculate() picks it up
    // Note: recalculate() does a forward pass.
    // It normally takes prev segment exit velocity.
    // The "prev segment" is currentGen (at m_genSegmentIdx).
    // So we should set currentGen exit velocity.

    currentGen.thetaExitVel = fabsf(vTheta);
    currentGen.rhoExitVel = fabsf(vRho);

    // We also need to fix direction for velocity matching logic?
    // recalculate() checks for direction reversal.
    // brakeSeg direction should match vTheta direction (since we planned it that way).
    // So thetaReverses should be false.
    // Thus brakeSeg.thetaEntryVel will be min(prevExit, max).
    // prevExit is what we just set.
    // So it should work!

    // 10. Finalize
    m_endOfPattern = true;
    recalculate();
}

void MotionPlanner::start() {
    if (m_running.load() ||
        m_homingRhoActive.load(std::memory_order_acquire)) return;

    if (!ensureStepTimer()) return;

    m_timingRunSerial.fetch_add(1, std::memory_order_relaxed);
    m_running.store(true);
    m_segmentStartTime = micros();
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
    if (m_timerHandle != nullptr) return true;

    esp_timer_create_args_t timerArgs{};
    timerArgs.callback = stepTimerISR;
    timerArgs.arg = this;
    timerArgs.dispatch_method = ESP_TIMER_TASK;
    timerArgs.name = "step_timer";
    timerArgs.skip_unhandled_events = true;
    return esp_timer_create(&timerArgs,
                            (esp_timer_handle_t*)&m_timerHandle) == 0;
}

#ifndef NATIVE_BUILD
// Reserve TG1/T1 for homing only. Normal motion keeps its existing planner
// timer. Register from the motor task so UART and Wi-Fi task scheduling do
// not dispatch these STEP pulses.
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
#endif
    if (wasHoming && m_timerHandle != nullptr) {
        esp_timer_stop((esp_timer_handle_t)m_timerHandle);
    }
    if (wasHoming) {
        m_timerActive.store(false, std::memory_order_release);
        FastGPIO::setLow(R_STEP_PIN);
    }
}

void MotionPlanner::stop() {
    m_homingRhoActive.store(false, std::memory_order_release);
#ifndef NATIVE_BUILD
    if (m_homingHardwareTimerReady) timer_pause(TIMER_GROUP_1, TIMER_1);
#endif
    if (m_timerHandle != nullptr) {
        esp_timer_stop((esp_timer_handle_t)m_timerHandle);
    }

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
             m_segmentStartTime = micros();
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
            esp_timer_start_periodic((esp_timer_handle_t)m_timerHandle, STEP_TIMER_PERIOD_US);
            m_timerActive.store(true);
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
        uint32_t segmentNow = micros();
        // Handle timer wraparound for elapsed calculation
        uint32_t diff = segmentNow - m_segmentStartTime;
        float elapsed = diff / 1000000.0f;

        // Check if current segment is complete
        // It's complete if time has elapsed AND we've generated all steps for it
        if (current.executing && elapsed >= current.duration && current.generationComplete) {

            // Segment complete
            current.executing = false;
            m_completedCount++;

            // Move to next segment
            m_segmentTail = (m_segmentTail + 1) % SEGMENT_BUFFER_SIZE;

            // Update start time deterministically
            m_segmentStartTime += (uint32_t)(current.duration * 1000000.0f);
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
        stop();
    }

    m_processProfiler.addSample(micros() - now);
}

FillStopReason MotionPlanner::fillStepQueue(uint32_t horizonUs) {
    uint32_t startUs = micros();
    uint32_t now = startUs;
    static constexpr float SAMPLE_INTERVAL = STEP_TIMER_PERIOD_US / 1000000.0f;
    FillStopReason reason = FillStopReason::None;

    while (m_genSegmentIdx != m_segmentHead) {
        if ((micros() - startUs) >= STEP_QUEUE_MAX_PROCESS_US) {
            reason = FillStopReason::TimeBudget;
            break;
        }
        Segment& seg = m_segments[m_genSegmentIdx];

        if (!seg.calculated) {
            calculateSegmentProfile(seg);
        }

        uint32_t startTime = m_genSegmentStartTime;
        float segDuration = seg.duration;

        // Calculate current wall-clock position relative to THIS segment's start time
        // If startTime is in the future, wallTime will be negative, which is correct
        int32_t timeDiff = (int32_t)(now - startTime);
        float wallTime = timeDiff / 1000000.0f;

        // Start generating from where we last generated
        float t = seg.lastGenTime;

        // Determine end time for this batch
        // We want to generate up to HORIZON ahead of current real time
        float tLimit = wallTime + (horizonUs / 1000000.0f);
        float tEnd = std::min(segDuration, tLimit);

        // If we are already ahead of the horizon, don't generate anything
        if (t >= tEnd) {
            if (t >= segDuration - 0.000001f) {
                seg.generationComplete = true;
                m_genSegmentStartTime += (uint32_t)(seg.duration * 1000000.0f);
                m_genSegmentIdx = (m_genSegmentIdx + 1) % SEGMENT_BUFFER_SIZE;
                continue;
            }
            uint32_t blankTime = startTime + (uint32_t)(tEnd * 1000000.0f);
            if (blankTime > m_lastQueuedEventTime && getStepQueueSpace() > 0) {
                queueStepEvent(blankTime, 0, 0);
            }
            reason = FillStopReason::Horizon;
            break;
        }

        int32_t lastThetaSteps = seg.lastGenThetaSteps;
        int32_t lastRhoSteps = seg.lastGenRhoSteps;

        // Cache profile references for faster access
        const SCurve::Profile& thetaProf = seg.theta.profile;
        const SCurve::Profile& rhoProf = seg.rho.profile;
        float thetaTimeScale = seg.theta.timeScale;
        float rhoTimeScale = seg.rho.timeScale;

        while (t <= tEnd) {
            if ((micros() - startUs) >= STEP_QUEUE_MAX_PROCESS_US) {
                seg.lastGenTime = t;
                seg.lastGenThetaSteps = lastThetaSteps;
                seg.lastGenRhoSteps = lastRhoSteps;
                reason = FillStopReason::TimeBudget;
                m_genProfiler.addSample(micros() - startUs);
                return reason;
            }
            // Get position from S-curve profile (with time scaling)
            float thetaProfileTime = t / thetaTimeScale;
            float rhoProfileTime = t / rhoTimeScale;

            // Get fractional position (0 to 1)
            float thetaFrac = (thetaProf.totalDistance > 0.0f)
                ? SCurve::getPosition(thetaProf, thetaProfileTime, seg.thetaPhaseIdx) / thetaProf.totalDistance
                : 0.0f;
            float rhoFrac = (rhoProf.totalDistance > 0.0f)
                ? SCurve::getPosition(rhoProf, rhoProfileTime, seg.rhoPhaseIdx) / rhoProf.totalDistance
                : 0.0f;

            // Calculate target steps
            int32_t targetThetaSteps, targetRhoSteps;
            if (t >= segDuration - 0.000001f) {
                targetThetaSteps = seg.theta.targetSteps;
                targetRhoSteps = seg.rho.targetSteps;
            } else {
                targetThetaSteps = seg.theta.startSteps + (int32_t)lroundf(thetaFrac * seg.theta.deltaSteps);
                targetRhoSteps = seg.rho.startSteps + (int32_t)lroundf(rhoFrac * seg.rho.deltaSteps);

                // Safety: clamp to target to prevent overshooting due to rounding
                if (seg.theta.deltaSteps > 0) {
                    targetThetaSteps = std::min(targetThetaSteps, seg.theta.targetSteps);
                } else if (seg.theta.deltaSteps < 0) {
                    targetThetaSteps = std::max(targetThetaSteps, seg.theta.targetSteps);
                }

                if (seg.rho.deltaSteps > 0) {
                    targetRhoSteps = std::min(targetRhoSteps, seg.rho.targetSteps);
                } else if (seg.rho.deltaSteps < 0) {
                    targetRhoSteps = std::max(targetRhoSteps, seg.rho.targetSteps);
                }

            }

            // Generate step events for any steps needed
            // Use startTime (the segment's absolute start) + t
            uint32_t eventTime = startTime + (uint32_t)(t * 1000000.0f);

            while (lastThetaSteps != targetThetaSteps || lastRhoSteps != targetRhoSteps) {
                uint8_t stepMask = 0;
                uint8_t dirMask = 0;

                if (lastThetaSteps != targetThetaSteps) {
                    stepMask |= 0x01;
                    if (targetThetaSteps > lastThetaSteps) {
                        dirMask |= 0x01;  // Forward
                        lastThetaSteps++;
                    } else {
                        lastThetaSteps--;
                    }
                }

                if (lastRhoSteps != targetRhoSteps) {
                    stepMask |= 0x02;
                    if (targetRhoSteps > lastRhoSteps) {
                        dirMask |= 0x02;  // Forward
                        lastRhoSteps++;
                    } else {
                        lastRhoSteps--;
                    }
                }

                if (stepMask != 0) {
                    if (!queueStepEvent(eventTime, stepMask, dirMask)) {
                        // Queue full - roll back steps and exit
                        if (stepMask & 0x01) {
                            if (dirMask & 0x01) lastThetaSteps--;
                            else lastThetaSteps++;
                        }
                        if (stepMask & 0x02) {
                            if (dirMask & 0x02) lastRhoSteps--;
                            else lastRhoSteps++;
                        }

                        seg.lastGenTime = t;
                        seg.lastGenThetaSteps = lastThetaSteps;
                        seg.lastGenRhoSteps = lastRhoSteps;
                        reason = FillStopReason::QueueFull;
                        m_genProfiler.addSample(micros() - startUs);
                        return reason;
                    }
                }
            }

            if (t >= segDuration - 0.000001f) break;
            t += SAMPLE_INTERVAL;
            if (t > segDuration) t = segDuration;
        }

        // Finished this segment batch - save state
        seg.lastGenTime = t;
        seg.lastGenThetaSteps = lastThetaSteps;
        seg.lastGenRhoSteps = lastRhoSteps;

        if (t >= segDuration - 0.000001f) {
            seg.generationComplete = true;
            m_genSegmentStartTime += (uint32_t)(seg.duration * 1000000.0f);
            m_genSegmentIdx = (m_genSegmentIdx + 1) % SEGMENT_BUFFER_SIZE;

            // If we just finished the last segment of the pattern, queue a stop event
            if (m_genSegmentIdx == m_segmentHead && m_endOfPattern && !m_stopEventQueued) {
                if (queueStepEvent(m_genSegmentStartTime, STOP_MASK, 0)) {
                    m_stopEventQueued = true;
                }
            }
            continue;
        }

        // Horizon reached within this segment; stop.
        uint32_t blankTime = startTime + (uint32_t)(tEnd * 1000000.0f);
        if (blankTime > m_lastQueuedEventTime && getStepQueueSpace() > 0) {
            queueStepEvent(blankTime, 0, 0);
        }
        reason = FillStopReason::Horizon;
        break;
    }

    m_genProfiler.addSample(micros() - startUs);
    return reason;
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

void MotionPlanner::getCurrentPosition(float& theta, float& rho) const {
    theta = stepsToTheta(m_executedTSteps);
    rho = stepsToRho(m_executedRSteps);
}

void MotionPlanner::getCurrentVelocity(float& thetaVel, float& rhoVel) const {
    thetaVel = 0.0f;
    rhoVel = 0.0f;

    if (!m_running.load() || m_segmentHead == m_segmentTail) {
        return;
    }

    const Segment& current = m_segments[m_segmentTail];
    if (!current.executing || !current.calculated || current.duration <= 0.0001f) {
        return;
    }

    uint32_t now = micros();
    uint32_t diff = now - m_segmentStartTime;
    float elapsed = diff / 1000000.0f;
    if (elapsed < 0.0f) elapsed = 0.0f;
    if (elapsed > current.duration) elapsed = current.duration;

    if (current.theta.profile.totalDistance > 0.0f && current.theta.timeScale > 0.0f) {
        float t = elapsed / current.theta.timeScale;
        float v = SCurve::getVelocity(current.theta.profile, t) / current.theta.timeScale;
        thetaVel = v * static_cast<float>(current.theta.direction);
    }

    if (current.rho.profile.totalDistance > 0.0f && current.rho.timeScale > 0.0f) {
        float t = elapsed / current.rho.timeScale;
        float v = SCurve::getVelocity(current.rho.profile, t) / current.rho.timeScale;
        rhoVel = v * static_cast<float>(current.rho.direction);
    }
}

void MotionPlanner::resetTheta() {
    // Reset theta to zero at current position
    m_executedTSteps = 0;
    m_queuedTSteps = 0;
    m_targetTheta = 0.0f;

    // Update any pending segments
    for (int i = 0; i < SEGMENT_BUFFER_SIZE; i++) {
        m_segments[i].theta.startSteps = 0;
        m_segments[i].theta.targetSteps = thetaToSteps(m_segments[i].targetTheta);
        m_segments[i].calculated = false;
    }
}

void MotionPlanner::resetPosition(float theta, float rho) {
    stop();

    const int32_t thetaSteps = thetaToSteps(theta);
    const int32_t rhoSteps = rhoToSteps(std::max(0.0f, std::min(rho, m_maxRho)));
    m_executedTSteps.store(thetaSteps);
    m_queuedTSteps.store(thetaSteps);
    m_executedRSteps.store(rhoSteps);
    m_queuedRSteps.store(rhoSteps);
    m_targetTheta = theta;
    m_targetRho = stepsToRho(rhoSteps);
}

size_t MotionPlanner::copyPendingTargets(float* theta, float* rho, size_t capacity) const {
    if (theta == nullptr || rho == nullptr || capacity == 0) {
        return 0;
    }

    size_t count = 0;
    int idx = m_genSegmentIdx;
    while (idx != m_segmentHead && count < capacity) {
        theta[count] = m_segments[idx].targetTheta;
        rho[count] = m_segments[idx].targetRho;
        ++count;
        idx = (idx + 1) % SEGMENT_BUFFER_SIZE;
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
        if (!seg.executing && seg.lastGenTime < 0.0001f) {
            seg.calculated = false;
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
        if (!seg.executing && seg.lastGenTime < 0.0001f) {
            seg.calculated = false;
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

void MotionPlanner::recordNormalCallbackTiming(uint32_t now) {
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

void MotionPlanner::recordAxisStepTiming(AxisStepTimingState& timing,
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
    float maximum = 0.0f;
    int idx = m_segmentTail;
    while (idx != m_segmentHead) {
        const int nextIdx = (idx + 1) % SEGMENT_BUFFER_SIZE;
        if (nextIdx == m_segmentHead) break;
        const Segment& current = m_segments[idx];
        const Segment& next = m_segments[nextIdx];
        if (current.calculated && next.calculated) {
            const float thetaExitTime = std::min(current.theta.profile.totalTime,
                current.duration / std::max(current.theta.timeScale, 0.000001f));
            const float rhoExitTime = std::min(current.rho.profile.totalTime,
                current.duration / std::max(current.rho.timeScale, 0.000001f));
            const float thetaExit = SCurve::getVelocity(current.theta.profile, thetaExitTime) *
                current.theta.direction / std::max(current.theta.timeScale, 0.000001f);
            const float thetaEntry = next.theta.profile.v[0] * next.theta.direction /
                std::max(next.theta.timeScale, 0.000001f);
            const float rhoExit = SCurve::getVelocity(current.rho.profile, rhoExitTime) *
                current.rho.direction / std::max(current.rho.timeScale, 0.000001f);
            const float rhoEntry = next.rho.profile.v[0] * next.rho.direction /
                std::max(next.rho.timeScale, 0.000001f);
            maximum = std::max(maximum, fabsf(thetaExit - thetaEntry));
            maximum = std::max(maximum, fabsf(rhoExit - rhoEntry));
        }
        idx = nextIdx;
    }
    return maximum;
}

int32_t MotionPlanner::thetaToSteps(float theta) const {
    return (int32_t)(theta * m_stepsPerRadT);
}

int32_t MotionPlanner::rhoToSteps(float rho) const {
    return (int32_t)(rho * m_stepsPerMmR);
}

float MotionPlanner::stepsToTheta(int32_t steps) const {
    return (float)steps / m_stepsPerRadT;
}

float MotionPlanner::stepsToRho(int32_t steps) const {
    return (float)steps / m_stepsPerMmR;
}

// esp_timer task callback, called at the configured 20kHz period.
void IRAM_ATTR MotionPlanner::stepTimerISR(void* arg) {
    MotionPlanner* planner = static_cast<MotionPlanner*>(arg);
    planner->handleStepTimer();
}

void IRAM_ATTR MotionPlanner::handleStepTimer() {
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

    // Normal-motion diagnostics do not observe or modify the homing ISR.
    uint32_t now = micros();
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
        if (m_timerHandle != nullptr) {
            esp_timer_stop((esp_timer_handle_t)m_timerHandle);
        }
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
    delayMicroseconds(DIR_SETUP_TIME_US);
#endif

    // Timestamp immediately before the rising writes, AFTER DIR setup. Keep
    // bookkeeping after STEP falls so it cannot extend setup or pulse width.
    const uint32_t preRiseUs = (event.stepMask & 0x03) ? micros() : 0;

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
    delayMicroseconds(STEP_PULSE_WIDTH_US);
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
