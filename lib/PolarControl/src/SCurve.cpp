#include "SCurve.hpp"
#include <algorithm>

// Distance during jerk phase: d = v0*t + 0.5*a0*t² + (1/6)*j*t³
double SCurve::jerkPhaseDistance(double v0, double a0, double j, double t) {
    return v0 * t + 0.5 * a0 * t * t + (1.0 / 6.0) * j * t * t * t;
}

// Distance during constant acceleration: d = v0*t + 0.5*a*t²
double SCurve::constAccelDistance(double v0, double a, double t) {
    return v0 * t + 0.5 * a * t * t;
}

// Velocity after jerk phase: v = v0 + a0*t + 0.5*j*t²
double SCurve::jerkPhaseVelocity(double v0, double a0, double j, double t) {
    return v0 + a0 * t + 0.5 * j * t * t;
}

bool SCurve::calculate(double distance, double vStart, double vEnd, double vMax, double aMax,
                       double jMax, Profile& p) {
    p = {};
    if (!std::isfinite(distance) || !std::isfinite(vStart) || !std::isfinite(vEnd) ||
        !std::isfinite(vMax) || !std::isfinite(aMax) || !std::isfinite(jMax) || distance < 0.0 ||
        vStart < 0.0 || vEnd < 0.0 || vMax <= 0.0 || aMax <= 0.0 || jMax <= 0.0) {
        return false;
    }

    // Store constraints
    p.jerk = jMax;
    p.maxAccel = aMax;
    p.maxVelocity = vMax;

    // Handle zero or negative distance
    if (distance <= 0.0) {
        // Zero-duration profile
        for (int i = 0; i < 7; i++) {
            p.t[i] = 0.0;
            p.tEnd[i] = 0.0;
            p.posEnd[i] = 0.0;
        }
        for (int i = 0; i < 8; i++) {
            p.v[i] = vStart;
            p.a[i] = 0.0;
        }
        p.v[7] = vEnd;
        p.totalTime = 0.0;
        p.totalDistance = 0.0;
        return true;
    }

    // An infeasible boundary must not be silently changed; it is shared with
    // the adjacent segment and may already have been committed to STEP events.
    if (vStart > vMax + 1e-9 || vEnd > vMax + 1e-9)
        return false;
    vStart = std::min(vStart, vMax);
    vEnd = std::min(vEnd, vMax);

    // Time to reach max acceleration with jerk limit
    double tJerk = aMax / jMax;

    // Velocity change during one jerk phase (phases 1 or 3)
    double vJerk = 0.5 * jMax * tJerk * tJerk;

    // Velocity change during accel ramp (phases 1+2+3) if we reach max accel
    // This is the minimum velocity change if we use full acceleration
    double vAccelMin = 2.0 * vJerk; // Just the jerk phases, no const accel

    // Find the highest reachable peak velocity. Acceleration from v0 to vp is
    // the time-reverse of deceleration from vp to v0, so the same exact
    // distance helper applies to both sides. Binary search is both faster and
    // substantially more precise than decrementing the peak in 1% steps.
    const double minimumPeak = std::max(vStart, vEnd);
    auto transitionDistance = [=](double peak) {
        return decelerationDistance(peak, vStart, aMax, jMax) +
               decelerationDistance(peak, vEnd, aMax, jMax);
    };

    if (transitionDistance(minimumPeak) > distance + 1e-10 * std::max(1.0, distance)) {
        return false;
    }

    double vCruise = vMax;
    if (transitionDistance(vMax) > distance) {
        double low = minimumPeak;
        double high = vMax;
        for (int i = 0; i < 48; ++i) {
            const double mid = (low + high) * 0.5;
            if (transitionDistance(mid) <= distance) {
                low = mid;
            } else {
                high = mid;
            }
        }
        vCruise = low;
    }

    // Now calculate actual profile with determined cruise velocity
    // Initialize all phases to zero
    for (int i = 0; i < 7; i++) {
        p.t[i] = 0.0;
        p.tEnd[i] = 0.0;
        p.posEnd[i] = 0.0;
    }
    for (int i = 0; i < 8; i++) {
        p.v[i] = 0.0;
        p.a[i] = 0.0;
    }

    p.v[0] = vStart;
    p.a[0] = 0.0;

    double pos = 0.0;
    double currentTime = 0.0;

    // Phase 1: Jerk+ (accelerating)
    double deltaVAccel = vCruise - vStart;
    if (deltaVAccel > 1e-12) {
        if (deltaVAccel <= vAccelMin) {
            // Reduced jerk - can't reach max accel
            p.t[0] = std::sqrt(std::max(0.0, deltaVAccel / jMax));
            p.t[1] = 0.0;
            p.t[2] = p.t[0];
        } else {
            p.t[0] = tJerk;
            p.t[1] = (deltaVAccel - vAccelMin) / aMax;
            p.t[2] = tJerk;
        }
    }

    // Calculate velocities and positions through accel phases
    // Phase 1
    p.v[1] = jerkPhaseVelocity(p.v[0], 0.0, jMax, p.t[0]);
    p.a[1] = jMax * p.t[0];
    pos += jerkPhaseDistance(p.v[0], 0.0, jMax, p.t[0]);
    currentTime += p.t[0];
    p.tEnd[0] = currentTime;
    p.posEnd[0] = pos;

    // Phase 2
    p.v[2] = p.v[1] + p.a[1] * p.t[1];
    p.a[2] = p.a[1];
    pos += constAccelDistance(p.v[1], p.a[1], p.t[1]);
    currentTime += p.t[1];
    p.tEnd[1] = currentTime;
    p.posEnd[1] = pos;

    // Phase 3
    p.v[3] = jerkPhaseVelocity(p.v[2], p.a[2], -jMax, p.t[2]);
    p.a[3] = 0.0; // Should be zero at end of accel
    pos += jerkPhaseDistance(p.v[2], p.a[2], -jMax, p.t[2]);
    currentTime += p.t[2];
    p.tEnd[2] = currentTime;
    p.posEnd[2] = pos;

    // Phase 4: Cruise
    double cruiseDist = distance - pos;

    // Calculate decel distance
    double decelDist = 0.0;
    double deltaVDecel = vCruise - vEnd;
    double t5, t6, t7;
    if (deltaVDecel > 1e-12) {
        if (deltaVDecel <= vAccelMin) {
            t5 = std::sqrt(std::max(0.0, deltaVDecel / jMax));
            t6 = 0.0;
            t7 = t5;
        } else {
            t5 = tJerk;
            t6 = (deltaVDecel - vAccelMin) / aMax;
            t7 = tJerk;
        }

        // Estimate decel distance
        double v5_tmp = jerkPhaseVelocity(vCruise, 0.0, -jMax, t5);
        double decelDist_tmp = jerkPhaseDistance(vCruise, 0.0, -jMax, t5);
        double a5_tmp = -jMax * t5;
        double v6_tmp = v5_tmp + a5_tmp * t6;
        decelDist_tmp += constAccelDistance(v5_tmp, a5_tmp, t6);
        decelDist_tmp += jerkPhaseDistance(v6_tmp, a5_tmp, jMax, t7);
        decelDist = decelDist_tmp;
    } else {
        t5 = t6 = t7 = 0.0;
    }

    cruiseDist -= decelDist;
    if (cruiseDist < 0.0)
        cruiseDist = 0.0;

    p.t[3] = (vCruise > 0.0) ? cruiseDist / vCruise : 0.0;
    p.v[4] = vCruise;
    p.a[4] = 0.0;
    pos += cruiseDist;
    currentTime += p.t[3];
    p.tEnd[3] = currentTime;
    p.posEnd[3] = pos;

    // Phase 5-7: Deceleration
    p.t[4] = t5;
    p.t[5] = t6;
    p.t[6] = t7;

    // Phase 5
    p.v[5] = jerkPhaseVelocity(p.v[4], 0.0, -jMax, p.t[4]);
    p.a[5] = -jMax * p.t[4];
    pos += jerkPhaseDistance(p.v[4], 0.0, -jMax, p.t[4]);
    currentTime += p.t[4];
    p.tEnd[4] = currentTime;
    p.posEnd[4] = pos;

    // Phase 6
    p.v[6] = p.v[5] + p.a[5] * p.t[5];
    p.a[6] = p.a[5];
    pos += constAccelDistance(p.v[5], p.a[5], p.t[5]);
    currentTime += p.t[5];
    p.tEnd[5] = currentTime;
    p.posEnd[5] = pos;

    // Phase 7
    p.v[7] = vEnd;
    p.a[7] = 0.0;
    pos += jerkPhaseDistance(p.v[6], p.a[6], jMax, p.t[6]);
    currentTime += p.t[6];
    p.tEnd[6] = currentTime;
    p.posEnd[6] = pos;

    p.totalTime = currentTime;
    p.totalDistance = distance;

    return true;
}

double SCurve::getVelocity(const Profile& p, double t) {
    if (t <= 0.0)
        return p.v[0];
    if (t >= p.totalTime) return p.v[7];

    // Find which phase we're in
    int phase = 0;
    double tPhase = t;
    for (int i = 0; i < 7; i++) {
        if (t <= p.tEnd[i]) {
            phase = i;
            tPhase = (i == 0) ? t : t - p.tEnd[i - 1];
            break;
        }
    }

    double v0 = p.v[phase];
    double a0 = p.a[phase];
    double j = 0.0;

    switch (phase) {
        case 0: j = p.jerk; break;       // Jerk+
        case 1:
            j = 0.0;
            break;                       // Const accel
        case 2: j = -p.jerk; break;      // Jerk-
        case 3:
            j = 0.0;
            break;                       // Cruise
        case 4: j = -p.jerk; break;      // Jerk-
        case 5:
            j = 0.0;
            break;                       // Const decel
        case 6: j = p.jerk; break;       // Jerk+
    }

    return jerkPhaseVelocity(v0, a0, j, tPhase);
}

double SCurve::getPosition(const Profile& p, double t) {
    if (t <= 0.0)
        return 0.0;
    if (t >= p.totalTime) return p.totalDistance;

    // Find which phase we're in
    int phase = 0;
    double tPhase = t;
    double posAtStart = 0.0;

    for (int i = 0; i < 7; i++) {
        if (t <= p.tEnd[i]) {
            phase = i;
            if (i > 0) {
                tPhase = t - p.tEnd[i - 1];
                posAtStart = p.posEnd[i - 1];
            } else {
                tPhase = t;
                posAtStart = 0.0;
            }
            break;
        }
    }

    double v0 = p.v[phase];
    double a0 = p.a[phase];
    double j = 0.0;

    switch (phase) {
        case 0: j = p.jerk; break;
        case 1:
            j = 0.0;
            break;
        case 2: j = -p.jerk; break;
        case 3:
            j = 0.0;
            break;
        case 4: j = -p.jerk; break;
        case 5:
            j = 0.0;
            break;
        case 6: j = p.jerk; break;
    }

    if (j != 0.0) {
        return posAtStart + jerkPhaseDistance(v0, a0, j, tPhase);
    } else {
        return posAtStart + constAccelDistance(v0, a0, tPhase);
    }
}

double SCurve::getAcceleration(const Profile& p, double t) {
    if (t <= 0.0)
        return p.a[0];
    if (t >= p.totalTime) return p.a[7];

    // Find which phase we're in
    int phase = 0;
    double tPhase = t;
    for (int i = 0; i < 7; i++) {
        if (t <= p.tEnd[i]) {
            phase = i;
            tPhase = (i == 0) ? t : t - p.tEnd[i - 1];
            break;
        }
    }

    double a0 = p.a[phase];
    double j = 0.0;

    switch (phase) {
        case 0: j = p.jerk; break;
        case 1:
            j = 0.0;
            break;
        case 2: j = -p.jerk; break;
        case 3:
            j = 0.0;
            break;
        case 4: j = -p.jerk; break;
        case 5:
            j = 0.0;
            break;
        case 6: j = p.jerk; break;
    }

    return a0 + j * tPhase;
}

double SCurve::decelerationDistance(double vStart, double vEnd, double aMax, double jMax) {
    if (vStart <= vEnd)
        return 0.0;

    double deltaV = vStart - vEnd;

    // Time to reach max deceleration with jerk limit
    double tJerk = aMax / jMax;

    // Velocity change during one jerk phase
    double vJerk = 0.5 * jMax * tJerk * tJerk;

    // Minimum velocity change (just jerk phases, no constant decel)
    double vDecelMin = 2.0 * vJerk;

    double distance = 0.0;

    if (deltaV <= vDecelMin) {
        // Reduced jerk profile - can't reach max deceleration
        double tJ = std::sqrt(std::max(0.0, deltaV / jMax));
        double v5 = vStart - 0.5 * jMax * tJ * tJ;
        double a5 = -jMax * tJ;
        distance = jerkPhaseDistance(vStart, 0.0, -jMax, tJ);
        distance += jerkPhaseDistance(v5, a5, jMax, tJ);
    } else {
        // Full profile with constant deceleration phase
        double vConstDecel = deltaV - vDecelMin;
        double tConstDecel = vConstDecel / aMax;

        // Phase 5: jerk- (building deceleration)
        distance = jerkPhaseDistance(vStart, 0.0, -jMax, tJerk);

        // Phase 6: constant deceleration
        double v5 = vStart - vJerk;
        distance += constAccelDistance(v5, -aMax, tConstDecel);

        // Phase 7: jerk+ (reducing deceleration)
        double v6 = v5 - vConstDecel;
        distance += jerkPhaseDistance(v6, -aMax, jMax, tJerk);
    }

    return distance;
}

double SCurve::maxAchievableEntryVelocity(double distance, double vEnd, double vMax, double aMax,
                                          double jMax) {
    if (distance <= 0.0)
        return vEnd;

    // Binary search for max vStart that can decelerate to vEnd within distance
    double vLow = vEnd;
    double vHigh = vMax;

    // Check if we can achieve max velocity
    double distAtMax = decelerationDistance(vMax, vEnd, aMax, jMax);
    if (distAtMax <= distance) {
        return vMax;
    }

    // Binary search
    for (int i = 0; i < 48; i++) { // ~6 decimal places precision
        double vMid = (vLow + vHigh) * 0.5;
        double dist = decelerationDistance(vMid, vEnd, aMax, jMax);

        if (dist <= distance) {
            vLow = vMid;
        } else {
            vHigh = vMid;
        }
    }

    return vLow;
}

double SCurve::maxAchievableExitVelocity(double distance, double vStart, double vMax, double aMax,
                                         double jMax) {
    if (distance <= 0.0)
        return vStart;

    // Binary search for max vEnd that can be reached from vStart within distance
    double vLow = vStart;
    double vHigh = vMax;

    // Check if we can reach max velocity
    // Note: acceleration logic is symmetric to deceleration.
    // Distance to accel from vStart to vEnd is same as decel from vEnd to vStart
    double distAtMax = decelerationDistance(vMax, vStart, aMax, jMax);
    if (distAtMax <= distance) {
        return vMax;
    }

    // Binary search
    for (int i = 0; i < 48; i++) {
        double vMid = (vLow + vHigh) * 0.5;
        double dist = decelerationDistance(vMid, vStart, aMax, jMax);

        if (dist <= distance) {
            vLow = vMid;
        } else {
            vHigh = vMid;
        }
    }

    return vLow;
}

double SCurve::getPosition(const Profile& p, double t, int& phaseIdx) {
    if (t <= 0.0)
        return 0.0;
    if (t >= p.totalTime) {
        phaseIdx = 6;
        return p.totalDistance;
    }

    // Advance phase if needed
    while (phaseIdx < 6 && t > p.tEnd[phaseIdx]) {
        phaseIdx++;
    }

    // Determine local time and start pos
    double tPhase;
    double posAtStart;

    if (phaseIdx > 0) {
        tPhase = t - p.tEnd[phaseIdx - 1];
        posAtStart = p.posEnd[phaseIdx - 1];
    } else {
        tPhase = t;
        posAtStart = 0.0;
    }

    double v0 = p.v[phaseIdx];
    double a0 = p.a[phaseIdx];
    double j = 0.0;

    switch (phaseIdx) {
        case 0: j = p.jerk; break;
        case 1:
            j = 0.0;
            break;
        case 2: j = -p.jerk; break;
        case 3:
            j = 0.0;
            break;
        case 4: j = -p.jerk; break;
        case 5:
            j = 0.0;
            break;
        case 6: j = p.jerk; break;
    }

    if (j != 0.0) {
        return posAtStart + jerkPhaseDistance(v0, a0, j, tPhase);
    } else {
        return posAtStart + constAccelDistance(v0, a0, tPhase);
    }
}
