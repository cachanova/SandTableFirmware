// Desktop test harness for MotionPlanner
// Build with: pio run -e native
// Run with: .pio/build/native/program [pattern.thr]

#include <iostream>
#include <iomanip>
#include <limits>
#include <string>
#include <cmath>
#include <vector>
#include <chrono>

#include "esp32_mock.hpp"
#include "thr_reader.hpp"
#include "profile_validator.hpp"
#include "RhoAcousticProfile.hpp"
#include "StallGuardDetector.hpp"
#include "RhoContactConsensus.hpp"
#include "RhoRollingSearch.hpp"
#include "RhoHomingBounds.hpp"

// Directly include implementations for native build to resolve linker errors
// This mimics a unity build
#include "../../lib/PolarControl/src/SCurve.cpp"
#include "../../lib/PolarControl/src/MotionPlanner.cpp"

// Test configuration
static constexpr float R_MAX = 450.0f;           // mm
static constexpr int STEPS_PER_MM_R = 100;       // steps per mm for rho
static constexpr int STEPS_PER_RAD_T = 3000;     // steps per rad for theta (aligned with MotionPlanner default)

int getQueueSpace(const MotionPlanner& p) {
    uint32_t depth, underruns;
    p.getDiagnostics(depth, underruns);
    return STEP_QUEUE_SIZE - 1 - depth;
}

// Motion limits
static constexpr float R_MAX_VEL = 10.0f;        // mm/s
static constexpr float R_MAX_ACCEL = 20.0f;      // mm/s²
static constexpr float R_MAX_JERK = 100.0f;      // mm/s³
static constexpr float T_MAX_VEL = 0.25f;        // rad/s
static constexpr float T_MAX_ACCEL = 1.0f;       // rad/s²
static constexpr float T_MAX_JERK = 10.0f;       // rad/s³

struct MotionRun {
    float maxExecutedCartesianVelocity = 0.0f;
    bool completed = false;
};

MotionRun runSingleMove(float speedMultiplier,
                        float startTheta, float startRho,
                        float targetTheta, float targetRho) {
    resetMock();
    MotionPlanner planner;
    planner.init(STEPS_PER_MM_R, STEPS_PER_RAD_T, R_MAX,
                 R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK,
                 T_MAX_VEL, T_MAX_ACCEL, T_MAX_JERK);
    planner.resetPosition(startTheta, startRho);
    planner.setSpeedMultiplier(speedMultiplier);
    planner.addSegment(targetTheta, targetRho);
    planner.setEndOfPattern(true);
    planner.recalculate();
    planner.start();

    MotionRun result;
    float previousTheta = startTheta;
    float previousRho = startRho;
    for (int i = 0; i < 1000000 && !planner.isIdle(); ++i) {
        planner.process();
        advanceMicros(1000);

        // Derive speed from executed steps over a one-second window instead of
        // trusting the profile-derived velocity telemetry under test.
        if ((i + 1) % 1000 == 0) {
            float theta = 0.0f;
            float rho = 0.0f;
            planner.getCurrentPosition(theta, rho);
            const float previousX = previousRho * cosf(previousTheta);
            const float previousY = previousRho * sinf(previousTheta);
            const float x = rho * cosf(theta);
            const float y = rho * sinf(theta);
            const float cartesianVelocity = std::hypot(
                x - previousX, y - previousY);
            result.maxExecutedCartesianVelocity = std::max(
                result.maxExecutedCartesianVelocity, cartesianVelocity);
            previousTheta = theta;
            previousRho = rho;
        }
    }
    result.completed = planner.isIdle();
    return result;
}

bool testSpeedMultiplierScalesSpatialVelocity() {
    std::cout << "\n=== Test: Speed Multiplier Scales Spatial Velocity ===" << std::endl;

    const MotionRun radialFull = runSingleMove(1.0f, 0.0f, 20.0f, 0.0f, 420.0f);
    const MotionRun radialHalf = runSingleMove(0.5f, 0.0f, 20.0f, 0.0f, 420.0f);
    const MotionRun angularFull = runSingleMove(1.0f, 0.0f, 200.0f, 20.0f, 200.0f);
    const MotionRun angularHalf = runSingleMove(0.5f, 0.0f, 200.0f, 20.0f, 200.0f);

    const float radialRatio = radialHalf.maxExecutedCartesianVelocity /
        radialFull.maxExecutedCartesianVelocity;
    const float angularRatio = angularHalf.maxExecutedCartesianVelocity /
        angularFull.maxExecutedCartesianVelocity;
    const float expectedAngularChordPerSecond =
        2.0f * 200.0f * sinf(T_MAX_VEL * 0.5f);
    const bool passed = radialFull.completed && radialHalf.completed &&
        angularFull.completed && angularHalf.completed &&
        fabsf(radialFull.maxExecutedCartesianVelocity - R_MAX_VEL) < 0.02f &&
        fabsf(radialRatio - 0.5f) < 0.005f &&
        fabsf(angularFull.maxExecutedCartesianVelocity -
              expectedAngularChordPerSecond) < 0.1f &&
        fabsf(angularRatio - 0.5f) < 0.005f;

    std::cout << (passed ? "PASS" : "FAIL")
              << ": radial vxy " << radialFull.maxExecutedCartesianVelocity
              << " -> " << radialHalf.maxExecutedCartesianVelocity
              << " (ratio " << radialRatio << "), angular vxy "
              << angularFull.maxExecutedCartesianVelocity << " -> "
              << angularHalf.maxExecutedCartesianVelocity << " (ratio "
              << angularRatio << ")" << std::endl;
    return passed;
}

bool testControlledSpeedTransition() {
    std::cout << "\n=== Test: Controlled Speed Transition ===" << std::endl;
    resetMock();
    MotionPlanner planner;
    planner.init(STEPS_PER_MM_R, STEPS_PER_RAD_T, R_MAX,
                 R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK,
                 T_MAX_VEL, T_MAX_ACCEL, T_MAX_JERK);
    planner.resetPosition(0.0f, 200.0f);
    planner.setSpeedMultiplier(1.0f);
    planner.addSegment(5.0f, 200.0f);
    planner.addSegment(10.0f, 200.0f);
    planner.setEndOfPattern(true);
    planner.recalculate();
    planner.start();

    for (int i = 0; i < 2000; ++i) {
        planner.process();
        advanceMicros(1000);
    }
    float velocityBefore = 0.0f;
    float unusedRhoVelocity = 0.0f;
    planner.getCurrentVelocity(velocityBefore, unusedRhoVelocity);

    float pendingTheta[SEGMENT_BUFFER_SIZE];
    float pendingRho[SEGMENT_BUFFER_SIZE];
    const size_t pending = planner.copyPendingTargets(
        pendingTheta, pendingRho, SEGMENT_BUFFER_SIZE);
    planner.stopGracefully();

    float previousVelocity = velocityBefore;
    float maxBrakeVelocityJump = 0.0f;
    for (int i = 0; i < 10000 && !planner.isIdle(); ++i) {
        planner.process();
        advanceMicros(1000);
        float thetaVelocity = 0.0f;
        float rhoVelocity = 0.0f;
        planner.getCurrentVelocity(thetaVelocity, rhoVelocity);
        maxBrakeVelocityJump = std::max(
            maxBrakeVelocityJump, fabsf(thetaVelocity - previousVelocity));
        previousVelocity = thetaVelocity;
    }

    planner.setSpeedMultiplier(0.5f);
    for (size_t i = 0; i < pending; ++i) {
        planner.addSegment(pendingTheta[i], pendingRho[i]);
    }
    planner.setEndOfPattern(true);
    planner.recalculate();
    const float resumedBoundaryJump = planner.getMaxBoundaryVelocityDiscontinuity();
    planner.start();

    float maxResumedVelocity = 0.0f;
    for (int i = 0; i < 100000 && !planner.isIdle(); ++i) {
        planner.process();
        advanceMicros(1000);
        float thetaVelocity = 0.0f;
        float rhoVelocity = 0.0f;
        planner.getCurrentVelocity(thetaVelocity, rhoVelocity);
        maxResumedVelocity = std::max(maxResumedVelocity, fabsf(thetaVelocity));
    }

    float finalTheta = 0.0f;
    float finalRho = 0.0f;
    planner.getCurrentPosition(finalTheta, finalRho);
    PlannerTelemetry telemetry;
    planner.getTelemetry(telemetry);
    const bool passed = pending == 2 && velocityBefore > 0.24f &&
        maxBrakeVelocityJump < 0.005f && planner.isIdle() &&
        maxResumedVelocity > 0.124f && maxResumedVelocity < 0.126f &&
        resumedBoundaryJump < 0.001f &&
        fabsf(finalTheta - 10.0f) <= 2.0f / STEPS_PER_RAD_T &&
        fabsf(finalRho - 200.0f) <= 2.0f / STEPS_PER_MM_R &&
        telemetry.underruns == 0;

    std::cout << (passed ? "PASS" : "FAIL")
              << ": pending=" << pending
              << ", brake jump=" << maxBrakeVelocityJump
              << " rad/s, resumed max=" << maxResumedVelocity
              << " rad/s, boundary jump=" << resumedBoundaryJump
              << ", final=(" << finalTheta << ", " << finalRho << ")"
              << std::endl;
    return passed;
}

bool testStallGuardFiltering() {
    std::cout << "\n=== Test: StallGuard Homing Filter ===" << std::endl;
    StallGuardDetector detector(200, 5, 0.75f);
    uint32_t elapsedMs = 0;
    const uint16_t normalRipple[] = {230, 154, 220, 160, 238, 150, 216, 164};
    for (uint32_t index = 0; index < 120; ++index, elapsedMs += 7) {
        if (detector.update(normalRipple[index % 8], elapsedMs)) {
            std::cout << "FAIL: normal electrical-phase ripple triggered homing"
                      << std::endl;
            return false;
        }
    }

    // This is the terminal window from the observed CW false trigger. Its
    // median changes with rail load, but it lacks the deep collapse of a hard
    // stop and must pass through without triggering.
    const uint16_t cwTightSection[] = {
        96, 142, 150, 214, 154, 224, 126, 220, 132,
        164, 128, 96, 146, 96
    };
    for (uint16_t sample : cwTightSection) {
        if (detector.update(sample, elapsedMs)) {
            std::cout << "FAIL: recorded CW tight section triggered homing"
                      << std::endl;
            return false;
        }
        elapsedMs += 7;
    }

    // Main-RHO precision false stop: five soft lows accumulated in a moving
    // nine-sample window, but the terminal 160 was back near baseline 177.
    // This is a complete fresh-full-step detector history from that trial.
    StallGuardDetector recoveredPrecisionDetector(0, 5, 0.75f);
    const uint16_t recoveredPrecisionTrace[] = {
        130, 250, 128, 224, 110, 152, 196, 172, 190, 180, 208, 174,
        142, 170, 210, 188, 132, 248, 140, 242, 134, 250, 128, 246,
        134, 240, 170, 198, 166, 206, 180, 200, 176, 206, 202, 194,
        170, 144, 160, 200, 168, 142, 178, 122, 252, 128, 168, 190,
        218, 120, 112, 96, 196, 96, 236, 128, 160,
    };
    elapsedMs = 0;
    for (uint16_t sample : recoveredPrecisionTrace) {
        if (recoveredPrecisionDetector.update(sample, elapsedMs)) {
            std::cout << "FAIL: recovered precision ripple triggered homing"
                      << std::endl;
            return false;
        }
        elapsedMs += 7;
    }

    // The recorded main hard-stop tail contains both the normal phase ripple
    // and a multi-sample collapse. Replay it against its own approach history;
    // the real axes have independent detector instances.
    StallGuardDetector hardStopDetector(0, 5, 0.75f);
    elapsedMs = 0;
    for (uint32_t index = 0; index < 120; ++index, elapsedMs += 7) {
        if (hardStopDetector.update(normalRipple[index % 8], elapsedMs)) {
            std::cout << "FAIL: hard-stop approach history triggered homing"
                      << std::endl;
            return false;
        }
    }
    const uint16_t mainHardStop[] = {
        208, 160, 164, 148, 180, 122, 166, 96, 96, 32, 2
    };
    bool triggered = false;
    for (uint16_t sample : mainHardStop) {
        triggered |= hardStopDetector.update(sample, elapsedMs);
        elapsedMs += 7;
    }
    if (!triggered) {
        std::cout << "FAIL: recorded main hard stop was not detected"
                  << std::endl;
        return false;
    }

    // The assembled main motor's full-step precision trace alternated zero
    // SG readings with normal phases after it reached the stop. The ordinary
    // five-vote collapse missed contact before the shared travel cap.
    StallGuardDetector alternatingStopDetector(0, 5, 0.75f);
    elapsedMs = 0;
    for (uint32_t index = 0; index < 60; ++index, elapsedMs += 7) {
        if (alternatingStopDetector.update(normalRipple[index % 8], elapsedMs)) {
            std::cout << "FAIL: alternating-stop prelude triggered homing"
                      << std::endl;
            return false;
        }
    }
    const uint16_t alternatingStop[] = {
        150, 160, 146, 160, 0, 160, 0, 154
    };
    triggered = false;
    for (uint16_t sample : alternatingStop) {
        triggered |= alternatingStopDetector.update(sample, elapsedMs);
        elapsedMs += 7;
    }
    if (!triggered) {
        std::cout << "FAIL: repeated deep hard-stop pulses were missed"
                  << std::endl;
        return false;
    }

    StallGuardDetector isolatedSpikeDetector(0, 5, 0.75f);
    elapsedMs = 0;
    for (uint32_t index = 0; index < 60; ++index, elapsedMs += 7) {
        isolatedSpikeDetector.update(normalRipple[index % 8], elapsedMs);
    }
    const uint16_t isolatedSpike[] = {
        160, 180, 0, 170, 150, 190, 160, 180, 150
    };
    for (uint16_t sample : isolatedSpike) {
        if (isolatedSpikeDetector.update(sample, elapsedMs)) {
            std::cout << "FAIL: one deep SG spike triggered homing"
                      << std::endl;
            return false;
        }
        elapsedMs += 7;
    }

    // A sustained load change may propose a contact. Multipass agreement
    // now decides whether contacts establish home; the baseline still tracks.
    StallGuardDetector loadTrackingDetector(0, 5, 0.75f);
    elapsedMs = 0;
    for (uint32_t index = 0; index < 100; ++index, elapsedMs += 7) {
        if (loadTrackingDetector.update(200, elapsedMs)) {
            std::cout << "FAIL: initial load baseline triggered homing" << std::endl;
            return false;
        }
    }
    for (uint32_t index = 0; index < 100; ++index, elapsedMs += 7) {
        loadTrackingDetector.update(120, elapsedMs);
    }
    const uint16_t collapsed[] = {100, 90, 80, 60, 50, 40, 20, 0, 0};
    triggered = false;
    for (uint16_t sample : collapsed) {
        triggered |= loadTrackingDetector.update(sample, elapsedMs);
        elapsedMs += 7;
    }
    if (!triggered) {
        std::cout << "FAIL: hard stop was hidden by load tracking" << std::endl;
        return false;
    }

    std::cout << "PASS" << std::endl;
    return true;
}


bool testRhoContactConsensus() {
    RhoRollingSearch search(1000, 20, 50);
    if (!search.inward(200) || search.usedOverrun() != 0 ||
        search.backoffLimit(500) != 200 || !search.outward(100) ||
        search.approachLimit() != 920 || !search.inward(905) ||
        search.coordinate() != 1005 || search.usedOverrun() != 5) {
        std::cout << "FAIL: false contact prevented continued inward search\n";
        return false;
    }
    if (!search.outward(100) || !search.inward(120) ||
        search.usedOverrun() != 25 || !search.outward(100) ||
        !search.inward(120) || search.usedOverrun() != 45 ||
        !search.outward(100) || search.approachLimit() != 105 ||
        search.inward(106) || !search.inward(105) ||
        search.usedOverrun() != 50 || search.backoffLimit(2000) != 1000 ||
        search.outward(1001) || !search.outward(100) ||
        search.approachLimit() != 100 || search.inward(101)) {
        std::cout << "FAIL: rolling search exceeded per-pass/shared/outward caps\n";
        return false;
    }
    RhoRollingSearch obstruction(1000, 20, 50);
    if (!obstruction.inward(200) || obstruction.nearKnownHome(4)) {
        std::cout << "FAIL: repeated false contact replaced known origin\n";
        return false;
    }
    RhoRollingSearch lateThenEarly(1000, 20, 50);
    if (!lateThenEarly.inward(1020) || !lateThenEarly.outward(100) ||
        !lateThenEarly.inward(90) || lateThenEarly.nearKnownHome(4)) {
        std::cout << "FAIL: prior stop overrun hid a later early consensus\n";
        return false;
    }
    if (rhoRetryAllowance(800, 2000, 0) != 800 ||
        rhoRetryAllowance(800, 2000, 1600) != 400 ||
        rhoRetryAllowance(800, 2000, 2000) != 0 ||
        rhoRetryAllowance(800, 2000, 2400) != 0) {
        std::cout << "FAIL: per-pass overrun was not bounded by total allowance\n";
        return false;
    }
    RhoContactConsensus isolated(160); // 0.4 mm at u8
    if (isolated.add(3000) || isolated.add(3200) || isolated.add(3210) ||
        !isolated.add(3195)) {
        std::cout << "FAIL: one false contact prevented three later agreements\n";
        return false;
    }
    RhoContactConsensus drifting(160);
    if (drifting.add(1000) || drifting.add(1120) || drifting.add(1240)) {
        std::cout << "FAIL: pairwise proximity accepted cumulative drift\n";
        return false;
    }
    RhoContactConsensus separated(160);
    if (separated.add(1000) || separated.add(1400) || separated.add(1000)) {
        std::cout << "FAIL: nonconsecutive contacts accepted\n";
        return false;
    }
    RhoContactConsensus boundary(160);
    if (boundary.add(-80) || boundary.add(80) || !boundary.add(0)) {
        std::cout << "FAIL: exact consensus boundary rejected\n";
        return false;
    }
    return true;
}

bool testRhoAcousticProfiles() {
    std::cout << "\n=== Test: Outward-only Rho Acoustic Profiles ===" << std::endl;

    auto validate = [](const auto& offsets, const char* name) {
        if (offsets.empty() || offsets.back() != 0.0f) {
            std::cout << "FAIL: " << name << " does not return to its start" << std::endl;
            return false;
        }
        bool reachedExcursion = false;
        for (float offset : offsets) {
            if (offset < 0.0f || offset > RhoAcousticProfile::kExcursionMm) {
                std::cout << "FAIL: " << name << " leaves the outward-only envelope" << std::endl;
                return false;
            }
            reachedExcursion |= offset == RhoAcousticProfile::kExcursionMm;
        }
        if (!reachedExcursion) {
            std::cout << "FAIL: " << name << " never reaches the configured endpoint" << std::endl;
            return false;
        }
        return true;
    };

    const bool passed =
        validate(RhoAcousticProfile::kContinuousOffsetsMm, "continuous") &&
        validate(RhoAcousticProfile::kGatedOffsetsMm, "gated") &&
        validate(RhoAcousticProfile::kStressOffsetsMm, "stress");
    if (passed) std::cout << "PASS: all profiles stay in the outward-only envelope and return to zero" << std::endl;
    return passed;
}

bool testSynchronizedBoundaryVelocity() {
    std::cout << "\n=== Test: Synchronized Boundary Velocity ===" << std::endl;
    MotionPlanner planner;
    planner.init(STEPS_PER_MM_R, STEPS_PER_RAD_T, R_MAX,
                 R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK,
                 T_MAX_VEL, T_MAX_ACCEL, T_MAX_JERK);
    planner.addSegment(0.5f, 1.0f);
    planner.addSegment(1.0f, 120.0f);
    planner.addSegment(1.5f, 121.0f);
    planner.addSegment(2.0f, 240.0f);
    planner.setEndOfPattern(true);
    planner.recalculate();
    const float jump = planner.getMaxBoundaryVelocityDiscontinuity();
    const bool passed = jump <= 0.001f;
    std::cout << (passed ? "PASS" : "FAIL") << ": max velocity jump=" << jump << std::endl;
    return passed;
}

bool testMixedAxisBoundaryContinuityRegression() {
    std::cout << "\n=== Test: Mixed-Axis Boundary Continuity Regression ===" << std::endl;
    MotionPlanner planner;
    planner.init(STEPS_PER_MM_R, STEPS_PER_RAD_T, R_MAX,
                 R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK,
                 T_MAX_VEL, T_MAX_ACCEL, T_MAX_JERK);
    planner.addSegment(-0.222335f, 5.80746f);
    planner.addSegment(1.89776f, 58.6902f);
    planner.setEndOfPattern(true);
    planner.recalculate();
    const float jump = planner.getMaxBoundaryVelocityDiscontinuity();
    const bool passed = jump <= 0.0001f;
    std::cout << (passed ? "PASS" : "FAIL") << ": max velocity jump=" << jump << std::endl;
    return passed;
}

bool testCoordinatedAxisArrival() {
    std::cout << "\n=== Test: Coordinated Axis Arrival ===" << std::endl;
    resetMock();
    MotionPlanner planner;
    planner.init(STEPS_PER_MM_R, STEPS_PER_RAD_T, R_MAX,
                 R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK,
                 T_MAX_VEL, T_MAX_ACCEL, T_MAX_JERK);

    constexpr float targetTheta = 1.2f;
    constexpr float targetRho = 180.0f;
    const int32_t targetThetaSteps = static_cast<int32_t>(targetTheta * STEPS_PER_RAD_T);
    const int32_t targetRhoSteps = static_cast<int32_t>(targetRho * STEPS_PER_MM_R);
    planner.addSegment(targetTheta, targetRho);
    planner.setEndOfPattern(true);
    planner.recalculate();
    planner.start();

    uint64_t thetaArrival = 0;
    uint64_t rhoArrival = 0;
    int32_t rhoErrorAtThetaArrival = INT32_MAX;
    int32_t thetaErrorAtRhoArrival = INT32_MAX;
    for (int i = 0; i < 200000 && !planner.isIdle(); ++i) {
        planner.process();
        advanceMicros(STEP_TIMER_PERIOD_US);
        float theta = 0.0f;
        float rho = 0.0f;
        planner.getCurrentPosition(theta, rho);
        const int32_t thetaSteps = static_cast<int32_t>(lroundf(theta * STEPS_PER_RAD_T));
        const int32_t rhoSteps = static_cast<int32_t>(lroundf(rho * STEPS_PER_MM_R));
        if (thetaArrival == 0 && thetaSteps == targetThetaSteps) {
            thetaArrival = micros64();
            rhoErrorAtThetaArrival = std::abs(targetRhoSteps - rhoSteps);
        }
        if (rhoArrival == 0 && rhoSteps == targetRhoSteps) {
            rhoArrival = micros64();
            thetaErrorAtRhoArrival = std::abs(targetThetaSteps - thetaSteps);
        }
    }

    const uint64_t arrivalDelta = thetaArrival > rhoArrival
        ? thetaArrival - rhoArrival : rhoArrival - thetaArrival;
    // Quantized steppers can take their final microstep at different times near
    // a zero-velocity endpoint. At either exact arrival, the other axis must
    // already be within a physically negligible final-error envelope.
    const int32_t thetaToleranceSteps = static_cast<int32_t>(ceilf(0.001f * STEPS_PER_RAD_T));
    const int32_t rhoToleranceSteps = static_cast<int32_t>(ceilf(0.1f * STEPS_PER_MM_R));
    const bool passed = planner.isIdle() && thetaArrival != 0 && rhoArrival != 0 &&
        rhoErrorAtThetaArrival <= rhoToleranceSteps &&
        thetaErrorAtRhoArrival <= thetaToleranceSteps;
    std::cout << (passed ? "PASS" : "FAIL") << ": arrival delta="
              << arrivalDelta << " us, cross-axis errors=(theta "
              << thetaErrorAtRhoArrival << " step, rho "
              << rhoErrorAtThetaArrival << " step)" << std::endl;
    return passed;
}

bool testDisconnectedAxisIsSkipped() {
    std::cout << "\n=== Test: Disconnected Axis Is Skipped ===" << std::endl;

    auto runMove = [](bool thetaAvailable, bool rhoAvailable,
                      float targetTheta, float targetRho,
                      float& actualTheta, float& actualRho) {
        resetMock();
        MotionPlanner planner;
        planner.init(STEPS_PER_MM_R, STEPS_PER_RAD_T, R_MAX,
                     R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK,
                     T_MAX_VEL, T_MAX_ACCEL, T_MAX_JERK);
        planner.setAxisAvailability(thetaAvailable, rhoAvailable);
        if (!planner.addSegment(targetTheta, targetRho)) return false;
        planner.setEndOfPattern(true);
        planner.recalculate();
        planner.start();
        for (int i = 0; i < 200000 && !planner.isIdle(); ++i) {
            planner.process();
            advanceMicros(STEP_TIMER_PERIOD_US);
        }
        planner.getCurrentPosition(actualTheta, actualRho);
        return planner.isIdle();
    };

    float theta = 0.0f;
    float rho = 0.0f;
    const bool rhoOnlyFinished = runMove(false, true, 1.0f, 25.0f, theta, rho);
    const bool rhoOnlyPassed = rhoOnlyFinished && fabsf(theta) < 0.0001f &&
        fabsf(rho - 25.0f) <= (1.0f / STEPS_PER_MM_R);

    theta = 0.0f;
    rho = 0.0f;
    const bool thetaOnlyFinished = runMove(true, false, 0.2f, 25.0f, theta, rho);
    const bool thetaOnlyPassed = thetaOnlyFinished &&
        fabsf(theta - 0.2f) <= (1.0f / STEPS_PER_RAD_T) && fabsf(rho) < 0.0001f;

    const bool passed = rhoOnlyPassed && thetaOnlyPassed;
    std::cout << (passed ? "PASS" : "FAIL")
              << ": rho-only=" << rhoOnlyPassed
              << ", theta-only=" << thetaOnlyPassed << std::endl;
    return passed;
}

bool testInvalidMotionInputs() {
    std::cout << "\n=== Test: Invalid Motion Inputs ===" << std::endl;
    SCurve::Profile profile;
    if (SCurve::calculate(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, profile)) {
        std::cout << "FAIL: zero velocity limit accepted" << std::endl;
        return false;
    }
    MotionPlanner planner;
    planner.init(STEPS_PER_MM_R, STEPS_PER_RAD_T, R_MAX,
                 R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK,
                 T_MAX_VEL, T_MAX_ACCEL, T_MAX_JERK);
    if (planner.addSegment(std::nanf(""), 10.0f) ||
        planner.addSegment(0.0f, std::numeric_limits<float>::infinity())) {
        std::cout << "FAIL: non-finite target accepted" << std::endl;
        return false;
    }
    std::cout << "PASS" << std::endl;
    return true;
}

bool testGracefulStopAfterFullGeneration() {
    std::cout << "\n=== Test: Graceful Stop After Full Generation ===" << std::endl;
    resetMock();
    MotionPlanner planner;
    planner.init(STEPS_PER_MM_R, STEPS_PER_RAD_T, R_MAX,
                 R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK,
                 T_MAX_VEL, T_MAX_ACCEL, T_MAX_JERK);
    planner.addSegment(0.0f, 0.01f);
    planner.addSegment(0.0f, 0.02f);
    planner.recalculate();
    planner.start();
    planner.process();
    planner.stopGracefully();

    int iterations = 0;
    while (!planner.isIdle() && iterations++ < 10000) {
        planner.process();
        advanceMicros(1000);
    }
    const bool passed = planner.isIdle() && planner.getCompletedCount() == 2;
    PlannerTelemetry telemetry;
    planner.getTelemetry(telemetry);
    std::cout << (passed ? "PASS" : "FAIL") << ": completed="
              << planner.getCompletedCount() << ", idle=" << planner.isIdle()
              << ", running=" << telemetry.running << ", queue=" << telemetry.queueDepth
              << std::endl;
    return passed;
}

bool testBoundedRhoHomingPulses() {
    std::cout << "\n=== Test: Bounded Rho Homing Pulses ===" << std::endl;
    resetMock();
    MotionPlanner planner;
    planner.init(STEPS_PER_MM_R, STEPS_PER_RAD_T, R_MAX,
                 R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK,
                 T_MAX_VEL, T_MAX_ACCEL, T_MAX_JERK);

    constexpr uint32_t rate = 200;
    constexpr uint32_t limit = 20;
    if (!planner.startRhoHoming(-1, rate, limit)) {
        std::cout << "FAIL: bounded homing pulse source did not start" << std::endl;
        return false;
    }
    if (planner.startRhoHoming(+1, rate, limit)) {
        std::cout << "FAIL: a second homing pulse source started concurrently" << std::endl;
        return false;
    }
    planner.start();
    if (planner.isRunning()) {
        std::cout << "FAIL: normal planner motion started during homing" << std::endl;
        return false;
    }

    advanceMicros(200000);
    const uint32_t boundedCount = planner.getRhoHomingStepCount();
    if (boundedCount != limit || planner.isRhoHoming() || g_timerActive) {
        std::cout << "FAIL: bounded move count=" << boundedCount
                  << ", active=" << planner.isRhoHoming()
                  << ", timer=" << g_timerActive << std::endl;
        return false;
    }

    if (!planner.startRhoHoming(+1, rate)) {
        std::cout << "FAIL: unbounded homing pulse source did not restart" << std::endl;
        return false;
    }
    advanceMicros(30000);
    const uint32_t countBeforeStop = planner.getRhoHomingStepCount();
    planner.stopRhoHoming();
    advanceMicros(30000);
    const bool passed = countBeforeStop > 0 &&
        planner.getRhoHomingStepCount() == countBeforeStop &&
        !planner.isRhoHoming() && !g_timerActive;
    std::cout << (passed ? "PASS" : "FAIL")
              << ": bounded=" << boundedCount
              << ", stopped-at=" << countBeforeStop << std::endl;
    return passed;
}

bool testRhoSharedOverrunBudget() {
    std::cout << "\n=== Test: Rho Shared Homing Overrun Budget ===" << std::endl;
    constexpr uint32_t expected = 3200;  // 8 mm at 400 steps/mm
    constexpr uint32_t backoff = 1600;
    constexpr uint32_t tolerance = 400;
    for (uint32_t coarse : {2800U, 3200U, 3400U, 3600U}) {
        const RhoBoundedReturn bound = rhoBoundedReturn(
            expected, coarse, backoff, tolerance);
        const int64_t finalOffset = static_cast<int64_t>(expected) - coarse +
            backoff - bound.precisionCapSteps;
        if (!bound.contactWithinWindow || finalOffset < -400) {
            std::cout << "FAIL: coarse=" << coarse << ", final="
                      << finalOffset << std::endl;
            return false;
        }
    }
    if (rhoBoundedReturn(expected, 2799, backoff, tolerance)
            .contactWithinWindow ||
        rhoBoundedReturn(expected, 3601, backoff, tolerance)
            .contactWithinWindow ||
        rhoBoundedReturn(expected, 3600, backoff, tolerance)
            .precisionCapSteps != backoff) {
        std::cout << "FAIL: contact window or second-pass cap" << std::endl;
        return false;
    }
    if (!rhoReturnWithinWindow(expected, 3200, backoff, 1600, tolerance) ||
        rhoReturnWithinWindow(expected, 2800, backoff, 1200, tolerance) ||
        !rhoReturnWithinWindow(expected, 3000, backoff, 1500, tolerance)) {
        std::cout << "FAIL: combined return window" << std::endl;
        return false;
    }
    constexpr uint32_t twoMmTolerance = 800;
    const RhoBoundedReturn wider = rhoBoundedReturn(
        expected, 3600, backoff, twoMmTolerance);
    if (!wider.contactWithinWindow ||
        wider.precisionCapSteps != backoff + 400 ||
        rhoReturnWithinWindow(expected, 3600, backoff, 2200,
                              twoMmTolerance)) {
        std::cout << "FAIL: two-millimetre shared budget" << std::endl;
        return false;
    }
    if (!rhoApproachesAgree(200, 200, 25) ||
        !rhoApproachesAgree(200, 175, 25) ||
        !rhoApproachesAgree(200, 225, 25) ||
        rhoApproachesAgree(200, 174, 25) ||
        rhoApproachesAgree(200, 226, 25)) {
        std::cout << "FAIL: independent half-millimetre agreement window"
                  << std::endl;
        return false;
    }
    std::cout << "PASS: shared contact cap and separate agreement window"
              << std::endl;
    return true;
}

// ============================================================================
// Test: SCurve basic functionality
// ============================================================================

bool testSCurveBasic() {
    std::cout << "\n=== Test: SCurve Basic ===" << std::endl;

    ProfileValidator validator;
    SCurve::Profile profile;
    bool allPassed = true;

    // Test 1: Simple move from rest to rest
    std::cout << "\n1. Simple move (100mm, 0->0):" << std::endl;
    SCurve::calculate(100.0f, 0.0f, 0.0f, R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK, profile);
    auto result = validator.validate(profile, 100.0f, R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK);
    validator.printValidation(result);
    allPassed &= result.passed;

    // Test 2: Move with entry velocity
    std::cout << "\n2. Move with entry velocity (100mm, 15->0):" << std::endl;
    SCurve::calculate(100.0f, 15.0f, 0.0f, R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK, profile);
    result = validator.validate(profile, 100.0f, R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK);
    validator.printValidation(result);
    allPassed &= result.passed;

    // Test 3: Move with exit velocity
    std::cout << "\n3. Move with exit velocity (100mm, 0->15):" << std::endl;
    SCurve::calculate(100.0f, 0.0f, 15.0f, R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK, profile);
    result = validator.validate(profile, 100.0f, R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK);
    validator.printValidation(result);
    allPassed &= result.passed;

    // Test 4: Short move (can't reach max velocity)
    std::cout << "\n4. Short move (10mm, 0->0):" << std::endl;
    SCurve::calculate(10.0f, 0.0f, 0.0f, R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK, profile);
    result = validator.validate(profile, 10.0f, R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK);
    validator.printValidation(result);
    allPassed &= result.passed;

    // Test 5: Very short move
    std::cout << "\n5. Very short move (1mm, 0->0):" << std::endl;
    SCurve::calculate(1.0f, 0.0f, 0.0f, R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK, profile);
    result = validator.validate(profile, 1.0f, R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK);
    validator.printValidation(result);
    allPassed &= result.passed;

    // Test 6: Zero distance
    std::cout << "\n6. Zero distance (0mm):" << std::endl;
    SCurve::calculate(0.0f, 0.0f, 0.0f, R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK, profile);
    result = validator.validate(profile, 0.0f, R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK);
    validator.printValidation(result);
    allPassed &= result.passed;

    // Test 7: Entry velocity exceeds max
    std::cout << "\n7. Entry vel > max (100mm, 50->0, max=30):" << std::endl;
    SCurve::calculate(100.0f, 50.0f, 0.0f, R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK, profile);
    result = validator.validate(profile, 100.0f, R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK);
    validator.printValidation(result);
    allPassed &= result.passed;

    // Test 8: Theta axis parameters
    std::cout << "\n8. Theta axis (PI rad, 0->0):" << std::endl;
    SCurve::calculate((float)M_PI, 0.0f, 0.0f, T_MAX_VEL, T_MAX_ACCEL, T_MAX_JERK, profile);
    result = validator.validate(profile, (float)M_PI, T_MAX_VEL, T_MAX_ACCEL, T_MAX_JERK);
    validator.printValidation(result);
    allPassed &= result.passed;

    return allPassed;
}

// ============================================================================
// Test: SCurve deceleration distance calculation
// ============================================================================

bool testDecelDistance() {
    std::cout << "\n=== Test: Deceleration Distance ===" << std::endl;

    bool allPassed = true;

    // Test various velocity combinations
    struct TestCase {
        float vStart, vEnd;
        const char* desc;
    };

    TestCase cases[] = {
        {30.0f, 0.0f, "Full stop from max"},
        {30.0f, 15.0f, "Half decel"},
        {15.0f, 0.0f, "Half to stop"},
        {5.0f, 0.0f, "Slow to stop"},
        {30.0f, 29.0f, "Small decel"},
    };

    for (const auto& tc : cases) {
        float dist = SCurve::decelerationDistance(tc.vStart, tc.vEnd, R_MAX_ACCEL, R_MAX_JERK);

        // Verify by calculating profile
        SCurve::Profile profile;
        SCurve::calculate(dist, tc.vStart, tc.vEnd, R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK, profile);

        float actualDist = profile.totalDistance;
        float error = fabsf(actualDist - dist);
        bool passed = error < dist * 0.02f + 0.1f;  // 2% + 0.1mm tolerance

        std::cout << tc.desc << " (" << tc.vStart << " -> " << tc.vEnd << "): "
                  << "dist=" << dist << "mm, actual=" << actualDist << "mm"
                  << (passed ? " PASS" : " FAIL") << std::endl;

        allPassed &= passed;
    }

    return allPassed;
}

// ============================================================================
// Test: Max achievable entry velocity
// ============================================================================

bool testMaxEntryVel() {
    std::cout << "\n=== Test: Max Achievable Entry Velocity ===" << std::endl;

    bool allPassed = true;

    struct TestCase {
        float distance, vEnd;
        const char* desc;
    };

    TestCase cases[] = {
        {100.0f, 0.0f, "100mm to stop"},
        {50.0f, 0.0f, "50mm to stop"},
        {10.0f, 0.0f, "10mm to stop"},
        {100.0f, 15.0f, "100mm to 15mm/s"},
        {50.0f, 15.0f, "50mm to 15mm/s"},
    };

    for (const auto& tc : cases) {
        float maxEntry = SCurve::maxAchievableEntryVelocity(
            tc.distance, tc.vEnd, R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK);

        // Verify by calculating decel distance
        float decelDist = SCurve::decelerationDistance(maxEntry, tc.vEnd, R_MAX_ACCEL, R_MAX_JERK);
        bool passed = decelDist <= tc.distance * 1.02f + 0.1f;

        std::cout << tc.desc << ": maxEntry=" << maxEntry << " mm/s"
                  << ", decelDist=" << decelDist << "mm"
                  << (passed ? " PASS" : " FAIL") << std::endl;

        allPassed &= passed;
    }

    return allPassed;
}

// ============================================================================
// Test: Acceleration Reachability (Forward Pass)
// ============================================================================

bool testAccelReachability() {
    std::cout << "\n=== Test: Acceleration Reachability (Forward Pass) ===" << std::endl;
    // Reproduction of issue where exit velocity is set higher than physically achievable

    MotionPlanner planner;
    planner.init(STEPS_PER_MM_R, STEPS_PER_RAD_T, R_MAX,
                 R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK,
                 T_MAX_VEL, T_MAX_ACCEL, T_MAX_JERK);

    // Segment 1: Very short move, 0 -> ?
    planner.addSegment(0.0f, 0.5f);

    // Segment 2: Long move, ? -> 0
    planner.addSegment(0.0f, 20.0f);

    planner.setEndOfPattern(true);
    planner.recalculate();

    resetMock();
    setMicros(0);
    planner.start();

    // Execute and measure time for first segment
    int iters = 0;
    while (planner.getCompletedCount() == 0 && iters < 100000) {
        planner.process();
        advanceMicros(100);
        iters++;
    }

    float duration = g_mockMicros.load() / 1000000.0f;
    std::cout << "Segment 1 duration: " << duration << "s" << std::endl;

    // For this short move the acceleration is triangular. Independently of
    // the helper under test, d=j*t^3 and the total duration is 2*t.
    const float jerkPhaseTime = cbrtf(0.5f / R_MAX_JERK);
    const float expectedDuration = 2.0f * jerkPhaseTime;
    const float expectedExitVelocity = R_MAX_JERK * jerkPhaseTime * jerkPhaseTime;
    float maxExit = SCurve::maxAchievableExitVelocity(0.5f, 0.0f, 10.0f, 20.0f, 100.0f);

    if (fabsf(duration - expectedDuration) < 0.01f &&
        fabsf(maxExit - expectedExitVelocity) < 0.01f) {
        std::cout << "PASS: Duration matches fixed behavior." << std::endl;
        return true;
    } else {
        std::cout << "FAIL: duration=" << duration << " expected=" << expectedDuration
                  << ", exit=" << maxExit << " expectedExit=" << expectedExitVelocity << std::endl;
        return false;
    }
}

// ============================================================================
// Test: Theta Continuous (Spin) with various limits
// ============================================================================

bool testThetaContinuous(float vel, float accel, float jerk, const char* desc) {
    std::cout << "\n=== Test: Theta Continuous (" << desc << ") ===" << std::endl;
    std::cout << "Limits: v=" << vel << ", a=" << accel << ", j=" << jerk << std::endl;

    MotionPlanner planner;
    planner.init(STEPS_PER_MM_R, STEPS_PER_RAD_T, R_MAX,
                 R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK,
                 vel, accel, jerk);
    planner.setSpeedMultiplier(1.0f); // Run at full configured speed

    // Create a pattern similar to TestThetaContinuousGen
    // 5 rotations forward, 5 rotations back
    // 5 * 2pi = 10pi radians ~ 31.4159 rad
    float totalRotations = 5.0f;
    float totalRad = totalRotations * 2.0f * (float)M_PI;

    // Segment 1: Spin forward
    planner.addSegment(totalRad, 100.0f); // Rho fixed at 100
    // Segment 2: Spin back
    planner.addSegment(0.0f, 100.0f);

    planner.setEndOfPattern(true);
    planner.recalculate();

    resetMock();
    setMicros(0);
    planner.start();

    // Calculate expected time
    // Distance = totalRad.
    // Time to accel to max vel: t_acc = vel / accel (assuming jerk is high enough, else simplified)
    // Dist to accel: d_acc = 0.5 * accel * t_acc^2 = 0.5 * vel^2 / accel
    // If d_acc * 2 < distance, we reach cruise.

    // Using SCurve::calculate to get precise expected time
    SCurve::Profile p;
    SCurve::calculate(totalRad, 0.0f, 0.0f, vel, accel, jerk, p);
    float expectedTimePerMove = p.totalTime;
    float expectedTotalTime = expectedTimePerMove * 2.0f;

    std::cout << "Expected time per move: " << expectedTimePerMove << "s" << std::endl;
    std::cout << "Expected total time: " << expectedTotalTime << "s" << std::endl;

    // Run simulation
    int iters = 0;
    // Cap at 2x expected time or 10s min
    float timeoutS = std::max(10.0f, expectedTotalTime * 2.0f);
    int maxIters = (int)(timeoutS * 100.0f); // 10ms steps

    while ((planner.isRunning() || !planner.isIdle()) && iters < maxIters) {
        planner.process();
        advanceMicros(10000); // 10ms
        iters++;
    }

    float actualTime = g_mockMicros.load() / 1000000.0f;
    std::cout << "Actual time: " << actualTime << "s" << std::endl;

    // Check if we completed
    if (planner.getCompletedCount() != 2) {
        std::cout << "FAIL: Did not complete all segments. Completed: " << planner.getCompletedCount() << std::endl;
        return false;
    }

    // Check time tolerance (e.g. 5%)
    float error = fabsf(actualTime - expectedTotalTime);
    float tolerance = expectedTotalTime * 0.05f + 0.1f; // 5% + 100ms

    if (error <= tolerance) {
        std::cout << "PASS" << std::endl;
        return true;
    } else {
        std::cout << "FAIL: Time mismatch. Error: " << error << "s (Tol: " << tolerance << "s)" << std::endl;
        return false;
    }
}

// ============================================================================
// Test: Rho Continuous (In/Out) with various limits
// ============================================================================

bool testRhoContinuous(float vel, float accel, float jerk, const char* desc) {
    std::cout << "\n=== Test: Rho Continuous (" << desc << ") ===" << std::endl;
    std::cout << "Limits: v=" << vel << ", a=" << accel << ", j=" << jerk << std::endl;

    MotionPlanner planner;
    planner.init(STEPS_PER_MM_R, STEPS_PER_RAD_T, R_MAX,
                 vel, accel, jerk,
                 T_MAX_VEL, T_MAX_ACCEL, T_MAX_JERK);
    planner.setSpeedMultiplier(1.0f);

    // Create pattern: Out to max, then back to 20mm.
    float startRho = 20.0f;
    float endRho = R_MAX;

    // Segment 1: Out
    planner.addSegment(0.0f, endRho); // Theta fixed at 0
    // Segment 2: In
    planner.addSegment(0.0f, startRho);

    planner.setEndOfPattern(true);

    planner.recalculate();

    resetMock();
    setMicros(0);
    planner.start();

    // Calculate expected time using SCurve
    SCurve::Profile outward;
    SCurve::Profile inward;
    SCurve::calculate(endRho, 0.0f, 0.0f, vel, accel, jerk, outward);
    SCurve::calculate(endRho - startRho, 0.0f, 0.0f, vel, accel, jerk, inward);
    float expectedTotalTime = outward.totalTime + inward.totalTime;

    std::cout << "Expected total time: " << expectedTotalTime << "s" << std::endl;

    int iters = 0;
    float timeoutS = std::max(10.0f, expectedTotalTime * 2.0f);
    int maxIters = (int)(timeoutS * 100.0f);

    while ((planner.isRunning() || !planner.isIdle()) && iters < maxIters) {
        planner.process();
        advanceMicros(10000);
        iters++;
    }

    float actualTime = g_mockMicros.load() / 1000000.0f;
    std::cout << "Actual time: " << actualTime << "s" << std::endl;

    if (planner.getCompletedCount() != 2) {
        std::cout << "FAIL: Did not complete all segments. Completed: " << planner.getCompletedCount() << std::endl;
        return false;
    }

    float actualTheta = 0.0f;
    float actualRho = 0.0f;
    planner.getCurrentPosition(actualTheta, actualRho);
    if (fabsf(actualRho - startRho) > (2.0f / STEPS_PER_MM_R)) {
        std::cout << "FAIL: Final rho mismatch: " << actualRho << std::endl;
        return false;
    }

    float error = fabsf(actualTime - expectedTotalTime);
    float tolerance = expectedTotalTime * 0.05f + 0.1f;

    if (error <= tolerance) {
        std::cout << "PASS" << std::endl;
        return true;
    } else {
        std::cout << "FAIL: Time mismatch. Error: " << error << "s (Tol: " << tolerance << "s)" << std::endl;
        return false;
    }
}

// ============================================================================
// Test: Stop Gracefully
// ============================================================================

bool testStopGracefully() {
    std::cout << "\n=== Test: Stop Gracefully ===" << std::endl;

    // Setup planner with slow deceleration to make it measurable
    MotionPlanner planner;
    planner.init(STEPS_PER_MM_R, STEPS_PER_RAD_T, R_MAX,
                 R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK,
                 1.0f, 0.5f, 5.0f); // 1.0 rad/s, 0.5 rad/s^2 accel
    planner.setSpeedMultiplier(1.0f);

    // Start a long continuous move
    planner.addSegment(100.0f, 100.0f); // Long theta move

    // Important: recalculate to set velocities
    planner.recalculate();

    // Reset time BEFORE start so start time is 0
    resetMock();
    setMicros(0);

    planner.start();

    // Run for a bit to get up to speed

    // Max speed is 1.0 rad/s. Accel 0.5. Reach max in ~2s.
    // Run for 3s.
    int iters = 0;
    while (iters < 300) { // 3s
        planner.process();
        advanceMicros(10000);
        iters++;
    }

    float vTheta, vRho;
    float pTheta, pRho;
    planner.getCurrentVelocity(vTheta, vRho);
    planner.getCurrentPosition(pTheta, pRho);

    std::cout << "Time: " << (g_mockMicros.load()/1000000.0f) << "s" << std::endl;
    std::cout << "Pos: T=" << pTheta << " R=" << pRho << std::endl;
    std::cout << "Velocity before stop: " << vTheta << " rad/s" << std::endl;

    if (fabsf(vTheta) < 0.9f) {
        std::cout << "FAIL: Did not reach cruising speed." << std::endl;
        return false;
    }

    // Now trigger graceful stop
    std::cout << "Triggering stopGracefully()..." << std::endl;
    planner.stopGracefully();

    // If abrupt stop: velocity becomes 0 immediately or very quickly (next step).
    // If graceful stop: velocity ramps down.
    // Decel time from 1.0 rad/s at 0.5 rad/s^2 is ~2s.

    float tStopTrigger = g_mockMicros.load() / 1000000.0f;

    // Run for another 0.5s and check both the overall deceleration and every
    // sampled transition. A quantized braking target once caused the solver to
    // fall back to a rest-to-rest profile, producing 1 -> 0 -> rising speed.
    // The old endpoint-only assertion missed that discontinuity.
    float previousVelocity = vTheta;
    bool stopVelocityContinuous = true;
    iters = 0;
    while (iters < 50) { // 0.5s
        planner.process();
        advanceMicros(10000);
        float sampledTheta = 0.0f;
        float sampledRho = 0.0f;
        planner.getCurrentVelocity(sampledTheta, sampledRho);
        if (sampledTheta > previousVelocity + 0.02f ||
            previousVelocity - sampledTheta > 0.05f) {
            std::cout << "FAIL: Discontinuous stop velocity: " << previousVelocity
                      << " -> " << sampledTheta << std::endl;
            stopVelocityContinuous = false;
            break;
        }
        previousVelocity = sampledTheta;
        iters++;
    }

    if (!stopVelocityContinuous) return false;

    planner.getCurrentVelocity(vTheta, vRho);
    // std::cout << "Velocity 0.5s after stop: " << vTheta << " rad/s" << std::endl;

    bool decreasing = (vTheta < 0.9f) && (vTheta > 0.1f);
    if (!decreasing) {
        std::cout << "FAIL: Velocity not ramping down correctly. Got " << vTheta << std::endl;
        return false;
    } else {
        std::cout << "PASS: Velocity is decreasing." << std::endl;
    }

    // Run to completion
    iters = 0;
    while ((planner.isRunning() || !planner.isIdle()) && iters < 500) {
        planner.process();
        advanceMicros(10000);
        iters++;
    }

    float tEnd = g_mockMicros.load() / 1000000.0f;
    float stopDuration = tEnd - tStopTrigger;

    std::cout << "Stop duration: " << stopDuration << "s" << std::endl;

    // The synchronized brake may lengthen the faster axis so both axes retain
    // a continuous boundary. It should still stop within a tight bounded
    // window rather than running the original long move.
    if (stopDuration > 1.5f && stopDuration < 3.5f) {
        std::cout << "PASS: Stop duration within expected range." << std::endl;
        return true;
    } else {
        std::cout << "FAIL: Stop duration unexpected." << std::endl;
        return false;
    }
}

// ============================================================================
// Main
// ============================================================================

bool testPatternFile(const std::string& filepath) {
    std::cout << "\n=== Test: Pattern File " << filepath << " ===" << std::endl;
    resetMock();

    ThrReader reader;
    reader.setMaxRho(R_MAX);
    if (!reader.load(filepath)) return false;

    MotionPlanner planner;
    planner.init(STEPS_PER_MM_R, STEPS_PER_RAD_T, R_MAX,
                 R_MAX_VEL, R_MAX_ACCEL, R_MAX_JERK,
                 T_MAX_VEL, T_MAX_ACCEL, T_MAX_JERK);

    float theta = 0.0f;
    float rho = 0.0f;
    float finalTheta = 0.0f;
    float finalRho = 0.0f;
    bool sourceDone = false;
    bool started = false;
    size_t accepted = 0;
    size_t planned = 0;
    int32_t lastThetaSteps = 0;
    int32_t lastRhoSteps = 0;
    float maxBoundaryJump = 0.0f;
    const int kMaxIterations = std::max<int>(2000000,
        static_cast<int>(reader.size() * 10000));

    for (int iteration = 0; iteration < kMaxIterations; ++iteration) {
        bool added = false;
        while (!sourceDone && planner.hasSpace()) {
            if (!reader.getNextPosition(theta, rho)) {
                sourceDone = true;
                planner.setEndOfPattern(true);
                break;
            }
            if (!planner.addSegment(theta, rho)) {
                std::cerr << "Planner rejected finite pattern point " << accepted << std::endl;
                return false;
            }
            const int32_t targetThetaSteps = static_cast<int32_t>(theta * STEPS_PER_RAD_T);
            const int32_t targetRhoSteps = static_cast<int32_t>(rho * STEPS_PER_MM_R);
            if (targetThetaSteps != lastThetaSteps || targetRhoSteps != lastRhoSteps) {
                ++planned;
                lastThetaSteps = targetThetaSteps;
                lastRhoSteps = targetRhoSteps;
            }
            finalTheta = theta;
            finalRho = rho;
            ++accepted;
            added = true;
        }
        if (added || sourceDone) {
            planner.recalculate();
            const float boundaryJump = planner.getMaxBoundaryVelocityDiscontinuity();
            if (boundaryJump > maxBoundaryJump) {
                maxBoundaryJump = boundaryJump;
                if (maxBoundaryJump > 0.0001f) {
                    std::cout << "Boundary diagnostic: jump=" << maxBoundaryJump
                              << " after " << accepted << " inputs, completed="
                              << planner.getCompletedCount() << std::endl;
                }
            }
        }
        if (!started && accepted > 0) {
            planner.start();
            started = true;
        }
        planner.process();
        advanceMicros(10000);

        if (sourceDone && planner.isIdle()) {
            float actualTheta = 0.0f;
            float actualRho = 0.0f;
            planner.getCurrentPosition(actualTheta, actualRho);
            const int32_t thetaError = std::abs(
                static_cast<int32_t>(std::lround(actualTheta * STEPS_PER_RAD_T)) -
                static_cast<int32_t>(finalTheta * STEPS_PER_RAD_T));
            const int32_t rhoError = std::abs(
                static_cast<int32_t>(std::lround(actualRho * STEPS_PER_MM_R)) -
                static_cast<int32_t>(finalRho * STEPS_PER_MM_R));
            PlannerTelemetry telemetry;
            planner.getTelemetry(telemetry);
            const bool passed = accepted == reader.size() &&
                planner.getCompletedCount() == planned && thetaError <= 2 && rhoError <= 2 &&
                maxBoundaryJump <= 0.0001f && telemetry.underruns == 0;
            std::cout << (passed ? "PASS" : "FAIL") << ": " << accepted
                      << " points (" << planned << " planned), final step error=("
                      << thetaError << "," << rhoError << "), max boundary jump="
                      << maxBoundaryJump << ", underruns=" << telemetry.underruns
                      << std::endl;
            return passed;
        }
    }

    PlannerTelemetry telemetry;
    planner.getTelemetry(telemetry);
    std::cerr << "Pattern simulation timed out: accepted=" << accepted
              << ", total=" << reader.size()
              << ", completed=" << planner.getCompletedCount()
              << ", running=" << telemetry.running
              << ", queue=" << telemetry.queueDepth << std::endl;
    return false;
}

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "MotionPlanner Desktop Test Harness" << std::endl;
    std::cout << "========================================" << std::endl;

    if (argc > 1) {
        return testPatternFile(argv[1]) ? 0 : 1;
    }

    bool allPassed = true;

    // Run S-curve tests
    allPassed &= testStallGuardFiltering();
    allPassed &= testRhoContactConsensus();
    allPassed &= testRhoAcousticProfiles();
    allPassed &= testSpeedMultiplierScalesSpatialVelocity();
    allPassed &= testControlledSpeedTransition();
    allPassed &= testSynchronizedBoundaryVelocity();
    allPassed &= testMixedAxisBoundaryContinuityRegression();
    allPassed &= testCoordinatedAxisArrival();
    allPassed &= testDisconnectedAxisIsSkipped();
    allPassed &= testInvalidMotionInputs();
    allPassed &= testGracefulStopAfterFullGeneration();
    allPassed &= testBoundedRhoHomingPulses();
    allPassed &= testRhoSharedOverrunBudget();
    allPassed &= testSCurveBasic();
    allPassed &= testDecelDistance();
    allPassed &= testMaxEntryVel();
    allPassed &= testAccelReachability();

    // Run Theta Continuous Tests
    // 1. Standard config
    allPassed &= testThetaContinuous(0.5f, 0.5f, 5.0f, "Standard");
    // 2. Low Acceleration (should take long to spin up)
    allPassed &= testThetaContinuous(0.5f, 0.01f, 5.0f, "Low Accel");
    // 3. High Speed, Low Accel
    allPassed &= testThetaContinuous(2.0f, 0.05f, 10.0f, "High Speed Low Accel");

    // Run Rho Continuous Tests
    // 1. Standard config
    allPassed &= testRhoContinuous(10.0f, 20.0f, 100.0f, "Standard");
    // 2. Low Accel
    allPassed &= testRhoContinuous(10.0f, 1.0f, 100.0f, "Low Accel");

    // Test Stop Gracefully
    allPassed &= testStopGracefully();

    std::cout << "\n========================================" << std::endl;
    if (allPassed) {
        std::cout << "ALL TESTS PASSED" << std::endl;
    } else {
        std::cout << "SOME TESTS FAILED" << std::endl;
    }
    std::cout << "========================================" << std::endl;

    return allPassed ? 0 : 1;
}
