#pragma once
#include <cstdint>
#include <cstddef>
#include <atomic>
#include "SCurve.hpp"
#include "FastGPIO.hpp"
#include "Profiler.hpp"

#ifndef NATIVE_BUILD
#include <Arduino.h>
#include <Config.h>
#endif

// IRAM_ATTR is ESP32-specific; define as empty for native builds
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

// Pin definitions for step/dir (Config.h in firmware, defaults for native builds).
#ifndef NATIVE_BUILD
#define R_STEP_PIN Config::kRhoStepPin
#define R_DIR_PIN Config::kRhoDirPin
#define T_STEP_PIN Config::kThetaStepPin
#define T_DIR_PIN Config::kThetaDirPin
#elif !defined(R_STEP_PIN)
#define R_STEP_PIN 33
#define R_DIR_PIN 25
#define T_STEP_PIN 32
#define T_DIR_PIN 22
#endif

// Configuration constants
static constexpr int SEGMENT_BUFFER_SIZE = 32;
static constexpr int STEP_QUEUE_SIZE = 512;
static constexpr float MIN_SEGMENT_DURATION = 0.010f;  // 10ms minimum
#ifdef NATIVE_BUILD
// Native tests use limits below 1,000 steps/s. A 4kHz callback preserves every
// possible test step while avoiding billions of empty callbacks for patterns
// representing many hours of table motion.
static constexpr uint32_t STEP_TIMER_PERIOD_US = 250;
#else
static constexpr uint32_t STEP_TIMER_PERIOD_US = 50;  // 20kHz timer callback
#endif
static constexpr uint32_t STEP_QUEUE_HORIZON_US = 250000;  // 250ms lookahead
static constexpr uint32_t STEP_QUEUE_MAX_PROCESS_US = 20000;

enum class FillStopReason : uint32_t {
    None = 0,
    Horizon = 1,
    QueueFull = 2,
    TimeBudget = 3,
};

// Per-axis motion profile
struct AxisProfile {
    int32_t startSteps;        // Starting position in steps
    int32_t targetSteps;       // Absolute target in steps
    int32_t deltaSteps;        // Signed delta from start
    float deltaUnits;          // Delta in physical units (rad or mm)
    int8_t direction;          // +1 or -1 (0 if no motion)
    SCurve::Profile profile;   // 7-phase S-curve
    float timeScale;           // Ratio: duration / profile.totalTime
};

// A motion segment with synchronized theta/rho profiles
struct Segment {
    float targetTheta;         // Target theta position (radians, unwrapped)
    float targetRho;           // Target rho position (mm)

    AxisProfile theta;         // Theta axis profile
    AxisProfile rho;           // Rho axis profile

    float duration;            // Synchronized duration (same for both axes)

    // Velocity continuity
    float thetaEntryVel;       // Entry velocity for theta (rad/s)
    float thetaExitVel;        // Exit velocity for theta (rad/s)
    float rhoEntryVel;         // Entry velocity for rho (mm/s)
    float rhoExitVel;          // Exit velocity for rho (mm/s)

    bool calculated;           // True if profile has been calculated
    bool executing;            // True if currently being executed
    bool generationComplete;   // True if all steps for this segment have been generated

    // Execution state (phase tracking)
    int thetaPhaseIdx = 0;
    int rhoPhaseIdx = 0;

    // Generation state (to prevent duplicate steps)
    float lastGenTime = 0.0f;
    int32_t lastGenThetaSteps = 0;
    int32_t lastGenRhoSteps = 0;
};

// Step event for the hardware-timer queue (mock task timer in native tests).
struct StepEvent {
    uint32_t executeTime;      // Microsecond timestamp (relative to segment start)
    uint8_t stepMask;          // bit 0 = theta, bit 1 = rho
    uint8_t dirMask;           // direction bits: bit 0 = theta dir, bit 1 = rho dir
};

// Normal-motion diagnostics only. Timestamps describe software immediately
// before the GPIO rise, not electrical pulse validation or mechanical motion.
// Counters are boot-cumulative modulo uint32_t; stop/home never reset them.
struct AxisStepTimingTelemetry {
    bool valid = false;
    uint32_t stepCount = 0;
    uint32_t outlierCount = 0;
    uint32_t maxLatenessUs = 0;
    uint32_t maxAbsIntervalErrorUs = 0;
    uint32_t outlierEpoch = 0;
    uint32_t outlierScheduledMicros = 0;
    uint32_t outlierActualMicros = 0;
    uint32_t outlierPlannedIntervalUs = 0;
    uint32_t outlierActualIntervalUs = 0;
    uint32_t outlierIntervalValid = 0;
};

struct PlannerTelemetry {
    uint32_t queueDepth = 0;
    uint32_t minQueueDepth = 0;
    uint32_t underruns = 0;
    uint32_t maxConsecutiveUnderruns = 0;
    uint32_t completedCount = 0;
    uint32_t stepMotionEpoch = 0;
    uint32_t lastStepMotionStartUs = 0;
    uint32_t lastStepMotionStopUs = 0;
    bool timerActive = false;
    bool running = false;
    bool stepMotionActive = false;
    AxisStepTimingTelemetry thetaStepTiming;
    AxisStepTimingTelemetry rhoStepTiming;
    bool callbackTimingValid = false;
    uint32_t callbackGapCount = 0;
    uint32_t maxCallbackGapUs = 0;
    uint32_t lastCallbackGapMicros = 0;
    uint32_t lastCallbackGapUs = 0;
};

// Motion planner with independent axis control and S-curve profiles
class MotionPlanner {
public:
    MotionPlanner();
    ~MotionPlanner();

    // Initialize with physical parameters
    // stepsPerMmR: steps per mm for rho axis
    // stepsPerRadT: steps per radian for theta axis
    // maxRho: maximum rho position in mm
    // rMaxVel/rMaxAccel/rMaxJerk: rho limits (mm/s, mm/s², mm/s³)
    // tMaxVel/tMaxAccel/tMaxJerk: theta limits (rad/s, rad/s², rad/s³)
    void init(int stepsPerMmR, int stepsPerRadT, float maxRho,
              float rMaxVel, float rMaxAccel, float rMaxJerk,
              float tMaxVel, float tMaxAccel, float tMaxJerk,
              bool resetPosition = true);

    // Add a segment to the buffer (returns false if buffer full)
    bool addSegment(float theta, float rho);

    // Recalculate profiles for all pending segments
    void recalculate();

    // Start motion execution
    void start();

    // Stop motion execution (decelerate to stop)
    void stop();

    // Dedicated constant-rate rho pulse source for sensorless homing. This
    // bypasses coordinate limits while retaining sole ownership of STEP/DIR.
    // direction is -1 toward rho zero and +1 away from rho zero.
    bool startRhoHoming(int8_t direction, uint32_t stepsPerSecond,
                        uint32_t maxSteps = 0);
    bool setRhoHomingStepRate(uint32_t stepsPerSecond);
    void stopRhoHoming();
    uint32_t getRhoHomingStepCount() const {
        return m_homingRhoStepCount.load(std::memory_order_acquire);
    }
    bool isRhoHoming() const {
        return m_homingRhoActive.load(std::memory_order_acquire);
    }

    // Gracefully stop motion by interrupting current segment and decelerating to zero
    void stopGracefully();

    // Main processing loop - call frequently (~50Hz or faster)
    // Fills the step queue and manages segment transitions
    void process();

    // Check if there's space for more segments
    bool hasSpace() const;

    // Check if motion is currently running
    bool isRunning() const;

    // Check if planner is idle (no more segments to execute)
    bool isIdle() const;

    // Get current position in physical units
    void getCurrentPosition(float& theta, float& rho) const;

    // Get current velocity in physical units (theta rad/s, rho mm/s)
    void getCurrentVelocity(float& thetaVel, float& rhoVel) const;

    // Reset theta to zero (current position becomes new origin)
    void resetTheta();

    // Reset both logical axes after a separately controlled homing move.
    // This must only be called while the planner is stopped.
    void resetPosition(float theta = 0.0f, float rho = 0.0f);

    // Copy targets that have not yet been fully generated. Used to resume a
    // pattern after inserting a controlled braking segment.
    size_t copyPendingTargets(float* theta, float* rho, size_t capacity) const;

    // Set speed multiplier (0.1 to 1.0, scales velocity only)
    void setSpeedMultiplier(float mult);

    // Update motion limits without resetting positions (safe to call while running)
    void setMotionLimits(float rMaxVel, float rMaxAccel, float rMaxJerk,
                         float tMaxVel, float tMaxAccel, float tMaxJerk);

    // Prevent commands and logical position updates for axes whose motor
    // drivers were not detected during startup. Rho may remain available when
    // either of its two drivers is connected because they share STEP/DIR.
    void setAxisAvailability(bool thetaAvailable, bool rhoAvailable);

    // Signal end of pattern (causes deceleration to stop)
    void setEndOfPattern(bool ending);

    // Get count of completed segments
    uint32_t getCompletedCount() const { return m_completedCount; }

    // Reset completed count
    void resetCompletedCount() { m_completedCount = 0; }

    // Get diagnostic info
    void getDiagnostics(uint32_t& queueDepth, uint32_t& underruns) const;

    // Get profiling data
    void getProfileData(uint32_t& maxProcessUs, uint32_t& maxIntervalUs, uint32_t& avgGenUs);

    // Get extended telemetry (resets min/max queue depth and max consecutive underruns)
    void getTelemetry(PlannerTelemetry& out);

    // Largest commanded axis-velocity jump between calculated segments.
    float getMaxBoundaryVelocityDiscontinuity() const;

private:
#ifdef NATIVE_BUILD
    friend struct MotionTimingTestAccess;
#endif
    // Single writer: the normal hardware ISR (mock task callback on native). Atomic payload
    // fields plus a versioned bounded snapshot avoid C++ data races and torn
    // outlier records without taking locks in the pulse path.
    struct AxisStepTimingState {
        std::atomic<uint32_t> version{0};
        std::atomic<uint32_t> stepCount{0}, outlierCount{0};
        std::atomic<uint32_t> maxLatenessUs{0}, maxAbsIntervalErrorUs{0};
        std::atomic<uint32_t> outlierEpoch{0}, outlierScheduledMicros{0};
        std::atomic<uint32_t> outlierActualMicros{0};
        std::atomic<uint32_t> outlierPlannedIntervalUs{0}, outlierActualIntervalUs{0};
        std::atomic<uint32_t> outlierIntervalValid{0};
        bool previousValid = false;
        uint32_t previousEpoch = 0, previousScheduledUs = 0, previousActualUs = 0;
    };
    static_assert(__atomic_always_lock_free(sizeof(uint32_t), nullptr),
                  "STEP ISR requires lock-free word atomics");
    AxisStepTimingState m_thetaStepTiming, m_rhoStepTiming;
    std::atomic<uint32_t> m_timingRunSerial{0};
    uint32_t m_callbackTimingRunSerial = 0, m_previousCallbackUs = 0;
    bool m_previousCallbackValid = false;
    std::atomic<uint32_t> m_callbackTimingVersion{0};
    std::atomic<uint32_t> m_callbackGapCount{0}, m_maxCallbackGapUs{0};
    std::atomic<uint32_t> m_lastCallbackGapMicros{0}, m_lastCallbackGapUs{0};
    void IRAM_ATTR recordNormalCallbackTiming(uint32_t now);
    static void IRAM_ATTR recordAxisStepTiming(AxisStepTimingState& timing, uint32_t epoch,
                                    uint32_t scheduledUs, uint32_t actualUs);
    static void snapshotAxisStepTiming(const AxisStepTimingState& timing,
                                      AxisStepTimingTelemetry& out);

    // Physical parameters
    int m_stepsPerMmR;
    int m_stepsPerRadT;
    float m_maxRho;

    // Motion limits (base values before speed scaling)
    float m_rMaxVel, m_rMaxAccel, m_rMaxJerk;
    float m_tMaxVel, m_tMaxAccel, m_tMaxJerk;

    // Speed multiplier (applied to velocity only)
    float m_speedMultiplier;

    // Segment ring buffer
    Segment m_segments[SEGMENT_BUFFER_SIZE];
    volatile int m_segmentHead;      // Next segment to add
    volatile int m_segmentTail;      // Next segment to execute
    // m_segmentCount removed to fix race condition - calculated from Head/Tail
    
    // Generation tracking
    int m_genSegmentIdx;             // Next segment to generate steps for
    uint32_t m_genSegmentStartTime;  // Theoretical start time of the generating segment

    // Position tracking (in steps)
    // These are the three distinct position values mentioned in the plan:

    // 1. Queued position - where the last queued segment ends
    //    Used as starting point for new segment calculations
    std::atomic<int32_t> m_queuedTSteps;
    std::atomic<int32_t> m_queuedRSteps;

    // 2. Executed position - actual motor position (updated by ISR)
    std::atomic<int32_t> m_executedTSteps;
    std::atomic<int32_t> m_executedRSteps;

    // Target positions in physical units (for the last added segment)
    float m_targetTheta;
    float m_targetRho;

    // Step event queue (circular buffer)
    StepEvent m_stepQueue[STEP_QUEUE_SIZE];
    std::atomic<int> m_stepQueueHead; // Next position to write
    std::atomic<int> m_stepQueueTail; // Next position to read (timer task)
    std::atomic<uint32_t> m_underrunCount{0}; // Track queue underruns

    std::atomic<uint32_t> m_consecutiveUnderruns{0};
    std::atomic<uint32_t> m_maxConsecutiveUnderruns{0};

    uint32_t m_minQueueDepth = 0xFFFFFFFFu;
    uint32_t m_lastQueuedEventTime = 0;

    // Timing
    uint32_t m_segmentStartTime;     // Microseconds when current segment started
    float m_segmentElapsed;          // Time elapsed in current segment

    // State
    // Word-sized atomics avoid the SDK's flash-resident atomic<bool> helper
    // in hardware ISRs; logical values remain 0/1.
    std::atomic<uint32_t> m_running;
    // Word-sized atomics compile to inline Xtensa loads/stores in the IRAM
    // homing callback; std::atomic<bool>::load may call a flash-resident helper.
    std::atomic<uint32_t> m_timerActive{false};
    std::atomic<bool> m_thetaAvailable{true};
    std::atomic<bool> m_rhoAvailable{true};
    std::atomic<uint32_t> m_homingRhoActive{false};
    std::atomic<uint32_t> m_homingRhoIntervalUs{0};
    std::atomic<uint32_t> m_homingRhoNextStepUs{0};
    std::atomic<uint32_t> m_homingRhoStepCount{0};
    std::atomic<uint32_t> m_homingRhoStepLimit{0};
    std::atomic<uint32_t> m_stepMotionEpoch{0};
    std::atomic<uint32_t> m_lastStepMotionStartUs{0};
    std::atomic<uint32_t> m_lastStepMotionStopUs{0};
    std::atomic<uint32_t> m_stepMotionActive{false};
    bool m_endOfPattern;
    bool m_stopEventQueued = false;
    uint32_t m_completedCount;
    bool m_startupHoldoff = false;

    // Timer handle (ESP32 specific)
#ifdef NATIVE_BUILD
    void* m_timerHandle;
#else
    bool m_normalHardwareTimerReady = false;
    std::atomic<uint32_t> m_normalHardwareActive{false};
    static bool IRAM_ATTR normalHardwareISR(void* arg);
#endif
    bool startNormalStepTimer();
    void pauseNormalStepTimer();
#ifndef NATIVE_BUILD
    bool m_homingHardwareTimerReady = false;
    bool ensureHomingHardwareTimer();
    bool setHomingHardwareInterval(uint32_t intervalUs);
    static bool IRAM_ATTR homingHardwareISR(void* arg);
#endif

    // Internal methods
    void calculateSegmentProfile(Segment& seg);
    bool ensureStepTimer();
    FillStopReason fillStepQueue(uint32_t horizonUs);
    int getStepQueueSpace() const;
    bool queueStepEvent(uint32_t time, uint8_t stepMask, uint8_t dirMask);

    // Convert physical units to steps
    int32_t thetaToSteps(float theta) const;
    int32_t rhoToSteps(float rho) const;

    // Convert steps to physical units
    float stepsToTheta(int32_t steps) const;
    float stepsToRho(int32_t steps) const;

    // ISR callback (static for C compatibility)
    static void IRAM_ATTR stepTimerISR(void* arg);
    void IRAM_ATTR handleStepTimer();

    // Profiling
    Profiler m_processProfiler;
    Profiler m_intervalProfiler;
    Profiler m_genProfiler;
    uint32_t m_lastProcessTime = 0;
};
