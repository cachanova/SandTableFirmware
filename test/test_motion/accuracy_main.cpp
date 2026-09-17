// Native regression tests at the actual firmware sampling period.
#include "../../lib/PolarControl/src/MotionPlanner.cpp"
#include "../../lib/PolarControl/src/SCurve.cpp"
#include "ThrParser.hpp"
#include "esp32_mock.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

constexpr double kPi = 3.14159265358979323846, kThetaSteps = 12000 / (2 * kPi);
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct MotionAccuracyTestAccess {
    static Segment &at(MotionPlanner &p, int i) { return p.m_segments[i]; }
    static bool allGenerated(const MotionPlanner &p) {
        return p.m_genSegmentIdx == p.m_segmentHead;
    }
    static int head(const MotionPlanner &p) { return p.m_segmentHead; }
    static int tail(const MotionPlanner &p) { return p.m_segmentTail; }
    static int32_t theta(const MotionPlanner &p) { return p.m_executedTSteps.load(); }
    static int32_t rho(const MotionPlanner &p) { return p.m_executedRSteps.load(); }
    static bool resumeReady(const MotionPlanner &p) { return p.m_resumeReady; }
    static double distance(const MotionPlanner &p, const Segment &s, double t) {
        return p.segmentDistance(s, t);
    }
    static double speed(const MotionPlanner &p, const Segment &s, double t) {
        return p.segmentSpeed(s, t);
    }
    static void longClock(MotionPlanner &p, uint64_t elapsed) {
        auto &s = p.m_segments[0];
        p.m_genSegmentStartTime = 123456;
        p.m_genSegmentIdx = 0;
        s.nextSampleUs = elapsed;
        const auto point = s.path.position(p.segmentDistance(s, elapsed / 1000000.0));
        s.lastGenThetaSteps = p.thetaToSteps(point.theta);
        s.lastGenRhoSteps = p.rhoToSteps(point.rho);
        setMicros(123456 + elapsed);
        p.fillStepQueue(2000);
        require(s.nextSampleUs > elapsed + 2000, "integer sample clock did not advance");
        require(s.nextSampleUs <= elapsed + 2000 + STEP_TIMER_PERIOD_US,
                "sample clock skipped its horizon");
        require(!s.generationComplete, "long move completed prematurely");
        p.m_stepQueueHead = 0;
        p.m_stepQueueTail = 0;
        p.m_lastQueuedEventTimeValid = false;
    }
    static void endpoint(MotionPlanner &p) {
        auto &s = p.m_segments[0];
        p.m_genSegmentStartTime = 0;
        p.m_genSegmentIdx = 0;
        s.nextSampleUs = s.durationUs - STEP_TIMER_PERIOD_US;
        const auto point = s.path.position(p.segmentDistance(s, s.nextSampleUs / 1000000.0));
        s.lastGenThetaSteps = p.thetaToSteps(point.theta);
        s.lastGenRhoSteps = p.rhoToSteps(point.rho);
        setMicros(s.durationUs - 1);
        p.fillStepQueue(0);
        require(!s.generationComplete, "horizon skipped endpoint evaluation");
        setMicros(s.durationUs);
        p.fillStepQueue(0);
        require(s.generationComplete && s.lastGenThetaSteps == s.theta.targetSteps &&
                    s.lastGenRhoSteps == s.rho.targetSteps,
                "endpoint did not flush its exact step target");
    }
};
void init(MotionPlanner &p, double tolerance = .1) {
    resetMock();
    p.init(400, kThetaSteps, 425, 5.5, 20, 100, .225, 2, 10);
    p.setPathLimits(30, 100, tolerance);
}
void run(MotionPlanner &p, int maxMillis = 300000) {
    for (int i = 0; i < maxMillis && !p.isIdle(); ++i) {
        p.process();
        advanceMicros(1000);
    }
    require(p.isIdle(), "move did not complete");
}
void compactProfiles() {
    for (double distance : {0.001, 0.1, 1.0, 10.0, 1000.0, 1000000.0}) {
        for (double entry : {0.0, 0.01, 1.0}) {
            for (double exit : {0.0, 0.02, 1.0}) {
                SCurve::Profile original;
                if (!SCurve::calculate(distance, entry, exit, 10, 20, 100, original))
                    continue;
                const auto restored = SCurve::CompactProfile(original).expand();
                for (int i = 0; i < 7; ++i) {
                    require(original.t[i] == restored.t[i] &&
                                original.tEnd[i] == restored.tEnd[i] &&
                                original.posEnd[i] == restored.posEnd[i],
                            "compact phase lost precision");
                }
                for (int i = 0; i < 8; ++i)
                    require(original.v[i] == restored.v[i] && original.a[i] == restored.a[i],
                            "compact boundary lost precision");
                for (int i = 0; i <= 100; ++i) {
                    const double t = original.totalTime * i / 100;
                    require(SCurve::getPosition(original, t) == SCurve::getPosition(restored, t) &&
                                SCurve::getVelocity(original, t) ==
                                    SCurve::getVelocity(restored, t) &&
                                SCurve::getAcceleration(original, t) ==
                                    SCurve::getAcceleration(restored, t),
                            "compact profile changed evaluated motion");
                }
            }
        }
    }
    require(sizeof(Segment) <= 416, "buffered planner memory budget exceeded");
    std::cout << "PASS lossless compact profiles and planner memory budget\n";
}
void conversion() {
    MotionPlanner p;
    init(p);
    for (int turns : {1, 10, 100, 1000, -1, -1000}) {
        p.resetPosition();
        require(p.addSegment(turns * 2 * kPi, 123.45625), "turn target rejected");
        auto &s = MotionAccuracyTestAccess::at(p, 0);
        require(s.theta.targetSteps == turns * 12000, "full turns do not close at nominal gearing");
        require(s.rho.targetSteps == std::llround(123.45625 * 400),
                "rho is not rounded to nearest");
    }
    p.resetPosition();
    require(p.addSegment(23237.463033, .5), "large angle rejected");
    require(MotionAccuracyTestAccess::at(p, 0).theta.targetSteps ==
                std::llround(23237.463033 * kThetaSteps),
            "large angle lost precision");
    require(!p.addSegment(INFINITY, 0) && !p.addSegment(1e20, 0),
            "invalid/overflow target accepted");
    std::cout << "PASS exact gearing, absolute rounding, unwrapped angles\n";
}
void parser() {
    double theta = 0, rho = 0;
    require(parseThrLine("23237.463033, .75 # source", 425, theta, rho) == ThrLine::Coordinate &&
                std::abs(theta - 23237.463033) < 1e-10 && rho == 318.75,
            "precise THR parsing");
    for (const char *line : {"", "   # test", "\t// test"})
        require(parseThrLine(line, 425, theta, rho) == ThrLine::Ignore, "comment parsing");
    for (const char *line : {"nan .2", "1 inf", "1 -0.1", "1 1.1", "1.2.3", "1 .2 xyz", "1 .2 .3",
                             "1,", "1e999 .2", "1,,.2"})
        require(parseThrLine(line, 425, theta, rho) == ThrLine::Invalid,
                "invalid coordinate accepted");
    ThrValidator validator(425, kThetaSteps);
    require(validator.points() == 0, "empty pattern preflight");
    require(validator.accept("1 .2", 4, false, theta, rho) == ThrLine::Coordinate,
            "preflight valid point");
    require(validator.accept("2 BAD", 5, false, theta, rho) == ThrLine::Invalid &&
                validator.points() == 1,
            "preflight skipped corrupt point");
    require(validator.accept("3 .2", 4, true, theta, rho) == ThrLine::Invalid,
            "preflight accepted overflow");
    const char embedded[] = {'1', ' ', '.', '2', 0, '5', 0};
    require(validator.accept(embedded, 6, false, theta, rho) == ThrLine::Invalid,
            "preflight accepted embedded NUL");
    std::cout << "PASS strict shared THR parser and whole-file preflight validator\n";
}
PathPoint derivative(const PolarPath &p, double u, int order) {
    PathPoint sum{};
    for (int i = order; i < 6; ++i) {
        double factor = 1;
        for (int j = 0; j < order; ++j)
            factor *= i - j;
        sum =
            sum + p.coefficients[i] * (factor * std::pow(u, i - order) / std::pow(p.length, order));
    }
    return sum;
}
void checkCurve(const Segment &s, double tolerance) {
    const auto profile = s.profile.expand();
    const auto delta = s.path.end - s.path.start;
    for (int i = 0; i <= 1500; ++i) {
        const double time = s.duration * i / 1500;
        const double distance = SCurve::getPosition(profile, time) + s.startDistance;
        const double u = distance / s.path.length;
        auto q = s.path.position(distance);
        const double projected =
            std::clamp(((q.theta - s.path.start.theta) * delta.theta * 425 * 425 +
                        (q.rho - s.path.start.rho) * delta.rho) /
                           (s.path.length * s.path.length),
                       0.0, 1.0);
        const auto ref = s.path.start + delta * projected;
        const double deviation =
            std::hypot(q.rho * std::cos(q.theta) - ref.rho * std::cos(ref.theta),
                       q.rho * std::sin(q.theta) - ref.rho * std::sin(ref.theta));
        require(deviation <= tolerance + 1e-7, "curve left its Cartesian tolerance corridor");
        require(q.rho >= -1e-8 && q.rho <= 425 + 1e-8, "curve left radial travel");
        const auto first = derivative(s.path, u, 1), second = derivative(s.path, u, 2),
                   third = derivative(s.path, u, 3);
        const double v = SCurve::getVelocity(profile, time),
                     a = SCurve::getAcceleration(profile, time);
        double jerk = 0;
        for (int phase = 0; phase < 7; ++phase)
            if (time < profile.tEnd[phase]) {
                const int signs[7] = {1, 0, -1, 0, -1, 0, 1};
                jerk = signs[phase] * s.profile.jerk;
                break;
            }
        const auto velocity = first * v, acceleration = first * a + second * (v * v);
        const auto j = first * jerk + second * (3 * v * a) + third * (v * v * v);
        require(std::abs(velocity.theta) <= .225001 && std::abs(velocity.rho) <= 5.500001,
                "axis velocity exceeded");
        require(std::abs(acceleration.theta) <= 2.000001 && std::abs(acceleration.rho) <= 20.000001,
                "axis acceleration exceeded");
        require(std::abs(j.theta) <= 10.00001 && std::abs(j.rho) <= 100.00001,
                "axis jerk exceeded");
        require(std::hypot(velocity.rho, q.rho * velocity.theta) <= 30.00001,
                "ball speed exceeded");
        require(std::hypot(acceleration.rho - q.rho * velocity.theta * velocity.theta,
                           q.rho * acceleration.theta + 2 * velocity.rho * velocity.theta) <=
                    100.00001,
                "ball acceleration exceeded");
    }
}
void geometry() {
    for (double tolerance : {0.0, .1}) {
        MotionPlanner p;
        init(p, tolerance);
        p.resetPosition(0, 50);
        const PathPoint targets[] = {{.10, 100}, {.12, 200}, {.5, 210}, {-.5, 425},
                                     {-1, 0},    {1, 0},     {2, 425}};
        for (auto q : targets)
            require(p.addSegment(q.theta, q.rho), "geometry target rejected");
        p.setEndOfPattern(true);
        p.recalculate();
        require(p.getMaxBoundaryVelocityDiscontinuity() < 1e-8, "junction velocity discontinuity");
        for (int i = 0; i < 7; ++i) {
            auto &s = MotionAccuracyTestAccess::at(p, i);
            require(s.calculated, "profile solve failed");
            checkCurve(s, tolerance);
        }
    }
    // Exercise very different lengths, reversals, and nearly collinear joins.
    uint32_t seed = 0x53495359;
    auto random = [&]() {
        seed = seed * 1664525U + 1013904223U;
        return double(seed) / UINT32_MAX;
    };
    for (int trial = 0; trial < 12; ++trial) {
        MotionPlanner p;
        init(p, trial % 2 ? .1 : 0);
        double theta = trial * 2000.0, rho = 212.5;
        p.resetPosition(theta, rho);
        for (int i = 0; i < 20; ++i) {
            theta += (random() - .5) * (i % 3 ? .02 : 4);
            rho = std::clamp(rho + (random() - .5) * (i % 3 ? .5 : 400), 0.0, 425.0);
            require(p.addSegment(theta, rho), "varied geometry target rejected");
        }
        p.setEndOfPattern(true);
        p.recalculate();
        require(p.getMaxBoundaryVelocityDiscontinuity() < 1e-7, "varied junction discontinuity");
        for (int i = 0; i < MotionAccuracyTestAccess::head(p); ++i) {
            const auto &segment = MotionAccuracyTestAccess::at(p, i);
            require(segment.calculated, "varied geometry profile failed");
            checkCurve(segment, trial % 2 ? .1 : 0);
        }
    }
    std::cout << "PASS strict/shared geometry, bounded corners, axis and ball limits\n";
}
void clockAndEndpoint() {
    MotionPlanner p;
    init(p, 0);
    p.resetPosition(0, 100);
    p.addSegment(3000, 100);
    p.setEndOfPattern(true);
    p.recalculate();
    MotionAccuracyTestAccess::longClock(p, 1024ULL * 1000000);
    MotionAccuracyTestAccess::longClock(p, 8192ULL * 1000000);
    p.stop();
    init(p, 0);
    p.addSegment(.2, 1);
    p.setEndOfPattern(true);
    p.recalculate();
    MotionAccuracyTestAccess::endpoint(p);
    std::cout << "PASS long integer clock, micros rollover, endpoint horizon at "
              << STEP_TIMER_PERIOD_US << " us\n";
}
void pauseResume() {
    for (int pauseAt : {150, 2000, 5000}) {
        MotionPlanner p;
        init(p);
        p.resetPosition(0, 50);
        p.addSegment(.2, 65);
        p.addSegment(.7, 200);
        p.addSegment(1.0, 250);
        p.setEndOfPattern(true);
        p.recalculate();
        p.start();
        for (int i = 0; i < pauseAt; ++i) {
            p.process();
            advanceMicros(1000);
        }
        p.stopGracefully(true);
        double theta[32], rho[32];
        const size_t pending = p.copyPendingTargets(theta, rho, 32);
        require(pending > 0, "pause lost remaining source targets");
        double previousRho = 0;
        for (int i = 0; i < 100000 && !p.isIdle(); ++i) {
            p.process();
            advanceMicros(1000);
            double t, r;
            p.getCurrentPosition(t, r);
            require(r + 1e-9 >= previousRho, "pause rewound along its path");
            previousRho = r;
        }
        require(p.isIdle(), "pause did not finish braking");
        const bool partial = MotionAccuracyTestAccess::resumeReady(p);
        p.setSpeedMultiplier(.5);
        for (size_t i = 0; i < pending; ++i)
            require(p.addSegment(theta[i], rho[i]), "resume target rejected");
        if (partial)
            require(MotionAccuracyTestAccess::at(p, 0).startDistance > 0,
                    "resume restarted a completed path portion");
        p.setEndOfPattern(true);
        p.recalculate();
        require(p.getMaxBoundaryVelocityDiscontinuity() < 1e-8,
                "resume boundary velocity discontinuity");
        p.start();
        run(p);
        require(MotionAccuracyTestAccess::theta(p) == std::llround(kThetaSteps) &&
                    MotionAccuracyTestAccess::rho(p) == 100000,
                "resume endpoint step loss");
    }
    MotionPlanner tail;
    init(tail);
    tail.resetPosition(0, 100);
    tail.addSegment(.01, 100);
    tail.setEndOfPattern(true);
    tail.recalculate();
    tail.start();
    for (int i = 0; i < 10000 && !MotionAccuracyTestAccess::allGenerated(tail); ++i) {
        tail.process();
        advanceMicros(1000);
    }
    require(MotionAccuracyTestAccess::allGenerated(tail) && !tail.isIdle(), "queued-tail setup");
    tail.stopGracefully(true);
    double theta[32], rho[32];
    require(tail.copyPendingTargets(theta, rho, 32) == 0, "queued tail was scheduled twice");
    run(tail);
    require(tail.copyPendingTargets(theta, rho, 32) == 0, "completed tail reappeared on resume");
    tail.discardResume();
    require(!MotionAccuracyTestAccess::resumeReady(tail),
            "cancel retained partial resume geometry");
    std::cout << "PASS source-path braking, resume, and completed-tail handling\n";
}
void streaming() {
    MotionPlanner p;
    init(p);
    p.resetPosition(0, 100);
    int added = 0;
    bool started = false;
    for (int tick = 0; tick < 400000; ++tick) {
        bool changed = false;
        while (added < 96 && p.hasSpace()) {
            const double theta = .02 * (added + 1), rho = 100 + 10 * std::sin(added * .1);
            require(p.addSegment(theta, rho), "stream target rejected");
            ++added;
            changed = true;
        }
        p.setEndOfPattern(added == 96);
        if (changed) {
            p.recalculate();
            require(p.getMaxBoundaryVelocityDiscontinuity() < 1e-7,
                    "streaming junction discontinuity");
            for (int n = MotionAccuracyTestAccess::tail(p); n != MotionAccuracyTestAccess::head(p);
                 n = (n + 1) % 32)
                require(MotionAccuracyTestAccess::at(p, n).calculated,
                        "streaming profile became infeasible");
        }
        if (!started) {
            p.start();
            started = true;
        }
        p.process();
        advanceMicros(1000);
        if (added == 96 && p.isIdle())
            break;
    }
    require(p.isIdle() && p.getCompletedCount() == 96, "streaming completion");
    require(MotionAccuracyTestAccess::theta(p) == std::llround(1.92 * kThetaSteps) &&
                MotionAccuracyTestAccess::rho(p) == std::llround((100 + 10 * std::sin(9.5)) * 400),
            "streaming endpoint lost steps");
    PlannerTelemetry telemetry;
    p.getTelemetry(telemetry);
    require(telemetry.underruns == 0, "streaming underruns");
    std::cout << "PASS streamed lookahead, exact endpoint ledger, no underruns\n";
}
int main() try {
    compactProfiles();
    conversion();
    parser();
    geometry();
    clockAndEndpoint();
    pauseResume();
    streaming();
    std::cout << "ALL ACCURACY TESTS PASSED; Segment=" << sizeof(Segment)
              << " planner=" << sizeof(MotionPlanner) << " bytes\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << "\n";
    return 1;
}
