#include "SCurve.hpp"
#include <algorithm>

// Distance during jerk phase: d = v0*t + 0.5*a0*t² + (1/6)*j*t³
float SCurve::jerkPhaseDistance(float v0, float a0, float j, float t) {
    return v0 * t + 0.5f * a0 * t * t + (1.0f / 6.0f) * j * t * t * t;
}

// Distance during constant acceleration: d = v0*t + 0.5*a*t²
float SCurve::constAccelDistance(float v0, float a, float t) {
    return v0 * t + 0.5f * a * t * t;
}

// Velocity after jerk phase: v = v0 + a0*t + 0.5*j*t²
float SCurve::jerkPhaseVelocity(float v0, float a0, float j, float t) {
    return v0 + a0 * t + 0.5f * j * t * t;
}

bool SCurve::calculate(
    float distance,
    float vStart,
    float vEnd,
    float vMax,
    float aMax,
    float jMax,
    Profile& p
) {
    p = {};
    if (!std::isfinite(distance) || !std::isfinite(vStart) ||
        !std::isfinite(vEnd) || !std::isfinite(vMax) ||
        !std::isfinite(aMax) || !std::isfinite(jMax) ||
        distance < 0.0f || vStart < 0.0f || vEnd < 0.0f ||
        vMax <= 0.0f || aMax <= 0.0f || jMax <= 0.0f) {
        return false;
    }

    // Store constraints
    p.jerk = jMax;
    p.maxAccel = aMax;
    p.maxVelocity = vMax;

    // Handle zero or negative distance
    if (distance <= 0.0f) {
        // Zero-duration profile
        for (int i = 0; i < 7; i++) {
            p.t[i] = 0.0f;
            p.tEnd[i] = 0.0f;
            p.posEnd[i] = 0.0f;
        }
        for (int i = 0; i < 8; i++) {
            p.v[i] = vStart;
            p.a[i] = 0.0f;
        }
        p.v[7] = vEnd;
        p.totalTime = 0.0f;
        p.totalDistance = 0.0f;
        return true;
    }

    // Clamp start/end velocities to max
    vStart = std::min(vStart, vMax);
    vEnd = std::min(vEnd, vMax);

    // Time to reach max acceleration with jerk limit
    float tJerk = aMax / jMax;

    // Velocity change during one jerk phase (phases 1 or 3)
    float vJerk = 0.5f * jMax * tJerk * tJerk;

    // Velocity change during accel ramp (phases 1+2+3) if we reach max accel
    // This is the minimum velocity change if we use full acceleration
    float vAccelMin = 2.0f * vJerk;  // Just the jerk phases, no const accel

    // Find the highest reachable peak velocity. Acceleration from v0 to vp is
    // the time-reverse of deceleration from vp to v0, so the same exact
    // distance helper applies to both sides. Binary search is both faster and
    // substantially more precise than decrementing the peak in 1% steps.
    const float minimumPeak = std::max(vStart, vEnd);
    auto transitionDistance = [=](float peak) {
        return decelerationDistance(peak, vStart, aMax, jMax) +
               decelerationDistance(peak, vEnd, aMax, jMax);
    };

    if (transitionDistance(minimumPeak) > distance + 0.00001f) {
        return false;
    }

    float vCruise = vMax;
    if (transitionDistance(vMax) > distance) {
        float low = minimumPeak;
        float high = vMax;
        for (int i = 0; i < 24; ++i) {
            const float mid = (low + high) * 0.5f;
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
        p.t[i] = 0.0f;
        p.tEnd[i] = 0.0f;
        p.posEnd[i] = 0.0f;
    }
    for (int i = 0; i < 8; i++) {
        p.v[i] = 0.0f;
        p.a[i] = 0.0f;
    }

    p.v[0] = vStart;
    p.a[0] = 0.0f;

    float pos = 0.0f;
    float currentTime = 0.0f;

    // Phase 1: Jerk+ (accelerating)
    float deltaVAccel = vCruise - vStart;
    if (deltaVAccel > 0.001f) {
        if (deltaVAccel <= vAccelMin) {
            // Reduced jerk - can't reach max accel
            p.t[0] = sqrtf(std::max(0.0f, deltaVAccel / jMax));
            p.t[1] = 0.0f;
            p.t[2] = p.t[0];
        } else {
            p.t[0] = tJerk;
            p.t[1] = (deltaVAccel - vAccelMin) / aMax;
            p.t[2] = tJerk;
        }
    }

    // Calculate velocities and positions through accel phases
    // Phase 1
    p.v[1] = jerkPhaseVelocity(p.v[0], 0.0f, jMax, p.t[0]);
    p.a[1] = jMax * p.t[0];
    pos += jerkPhaseDistance(p.v[0], 0.0f, jMax, p.t[0]);
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
    p.a[3] = 0.0f;  // Should be zero at end of accel
    pos += jerkPhaseDistance(p.v[2], p.a[2], -jMax, p.t[2]);
    currentTime += p.t[2];
    p.tEnd[2] = currentTime;
    p.posEnd[2] = pos;

    // Phase 4: Cruise
    float cruiseDist = distance - pos;

    // Calculate decel distance
    float decelDist = 0.0f;
    float deltaVDecel = vCruise - vEnd;
    float t5, t6, t7;
    if (deltaVDecel > 0.001f) {
        if (deltaVDecel <= vAccelMin) {
            t5 = sqrtf(std::max(0.0f, deltaVDecel / jMax));
            t6 = 0.0f;
            t7 = t5;
        } else {
            t5 = tJerk;
            t6 = (deltaVDecel - vAccelMin) / aMax;
            t7 = tJerk;
        }

        // Estimate decel distance
        float v5_tmp = jerkPhaseVelocity(vCruise, 0.0f, -jMax, t5);
        float decelDist_tmp = jerkPhaseDistance(vCruise, 0.0f, -jMax, t5);
        float a5_tmp = -jMax * t5;
        float v6_tmp = v5_tmp + a5_tmp * t6;
        decelDist_tmp += constAccelDistance(v5_tmp, a5_tmp, t6);
        decelDist_tmp += jerkPhaseDistance(v6_tmp, a5_tmp, jMax, t7);
        decelDist = decelDist_tmp;
    } else {
        t5 = t6 = t7 = 0.0f;
    }

    cruiseDist -= decelDist;
    if (cruiseDist < 0.0f) cruiseDist = 0.0f;

    p.t[3] = (vCruise > 0.001f) ? cruiseDist / vCruise : 0.0f;
    p.v[4] = vCruise;
    p.a[4] = 0.0f;
    pos += cruiseDist;
    currentTime += p.t[3];
    p.tEnd[3] = currentTime;
    p.posEnd[3] = pos;

    // Phase 5-7: Deceleration
    p.t[4] = t5;
    p.t[5] = t6;
    p.t[6] = t7;

    // Phase 5
    p.v[5] = jerkPhaseVelocity(p.v[4], 0.0f, -jMax, p.t[4]);
    p.a[5] = -jMax * p.t[4];
    pos += jerkPhaseDistance(p.v[4], 0.0f, -jMax, p.t[4]);
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
    p.a[7] = 0.0f;
    pos += jerkPhaseDistance(p.v[6], p.a[6], jMax, p.t[6]);
    currentTime += p.t[6];
    p.tEnd[6] = currentTime;
    p.posEnd[6] = pos;

    p.totalTime = currentTime;
    p.totalDistance = distance;

    return true;
}

float SCurve::getVelocity(const Profile& p, float t) {
    if (t <= 0.0f) return p.v[0];
    if (t >= p.totalTime) return p.v[7];

    // Find which phase we're in
    int phase = 0;
    float tPhase = t;
    for (int i = 0; i < 7; i++) {
        if (t <= p.tEnd[i]) {
            phase = i;
            tPhase = (i == 0) ? t : t - p.tEnd[i - 1];
            break;
        }
    }

    float v0 = p.v[phase];
    float a0 = p.a[phase];
    float j = 0.0f;

    switch (phase) {
        case 0: j = p.jerk; break;       // Jerk+
        case 1: j = 0.0f; break;         // Const accel
        case 2: j = -p.jerk; break;      // Jerk-
        case 3: j = 0.0f; break;         // Cruise
        case 4: j = -p.jerk; break;      // Jerk-
        case 5: j = 0.0f; break;         // Const decel
        case 6: j = p.jerk; break;       // Jerk+
    }

    return jerkPhaseVelocity(v0, a0, j, tPhase);
}

float SCurve::getPosition(const Profile& p, float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= p.totalTime) return p.totalDistance;

    // Find which phase we're in
    int phase = 0;
    float tPhase = t;
    float posAtStart = 0.0f;

    for (int i = 0; i < 7; i++) {
        if (t <= p.tEnd[i]) {
            phase = i;
            if (i > 0) {
                tPhase = t - p.tEnd[i - 1];
                posAtStart = p.posEnd[i - 1];
            } else {
                tPhase = t;
                posAtStart = 0.0f;
            }
            break;
        }
    }

    float v0 = p.v[phase];
    float a0 = p.a[phase];
    float j = 0.0f;

    switch (phase) {
        case 0: j = p.jerk; break;
        case 1: j = 0.0f; break;
        case 2: j = -p.jerk; break;
        case 3: j = 0.0f; break;
        case 4: j = -p.jerk; break;
        case 5: j = 0.0f; break;
        case 6: j = p.jerk; break;
    }

    if (j != 0.0f) {
        return posAtStart + jerkPhaseDistance(v0, a0, j, tPhase);
    } else {
        return posAtStart + constAccelDistance(v0, a0, tPhase);
    }
}

float SCurve::getAcceleration(const Profile& p, float t) {
    if (t <= 0.0f) return p.a[0];
    if (t >= p.totalTime) return p.a[7];

    // Find which phase we're in
    int phase = 0;
    float tPhase = t;
    for (int i = 0; i < 7; i++) {
        if (t <= p.tEnd[i]) {
            phase = i;
            tPhase = (i == 0) ? t : t - p.tEnd[i - 1];
            break;
        }
    }

    float a0 = p.a[phase];
    float j = 0.0f;

    switch (phase) {
        case 0: j = p.jerk; break;
        case 1: j = 0.0f; break;
        case 2: j = -p.jerk; break;
        case 3: j = 0.0f; break;
        case 4: j = -p.jerk; break;
        case 5: j = 0.0f; break;
        case 6: j = p.jerk; break;
    }

    return a0 + j * tPhase;
}

float SCurve::decelerationDistance(float vStart, float vEnd, float aMax, float jMax) {
    if (vStart <= vEnd) return 0.0f;

    float deltaV = vStart - vEnd;

    // Time to reach max deceleration with jerk limit
    float tJerk = aMax / jMax;

    // Velocity change during one jerk phase
    float vJerk = 0.5f * jMax * tJerk * tJerk;

    // Minimum velocity change (just jerk phases, no constant decel)
    float vDecelMin = 2.0f * vJerk;

    float distance = 0.0f;

    if (deltaV <= vDecelMin) {
        // Reduced jerk profile - can't reach max deceleration
        float tJ = sqrtf(std::max(0.0f, deltaV / jMax));
        float v5 = vStart - 0.5f * jMax * tJ * tJ;
        float a5 = -jMax * tJ;
        distance = jerkPhaseDistance(vStart, 0.0f, -jMax, tJ);
        distance += jerkPhaseDistance(v5, a5, jMax, tJ);
    } else {
        // Full profile with constant deceleration phase
        float vConstDecel = deltaV - vDecelMin;
        float tConstDecel = vConstDecel / aMax;

        // Phase 5: jerk- (building deceleration)
        distance = jerkPhaseDistance(vStart, 0.0f, -jMax, tJerk);

        // Phase 6: constant deceleration
        float v5 = vStart - vJerk;
        distance += constAccelDistance(v5, -aMax, tConstDecel);

        // Phase 7: jerk+ (reducing deceleration)
        float v6 = v5 - vConstDecel;
        distance += jerkPhaseDistance(v6, -aMax, jMax, tJerk);
    }

    return distance;
}

float SCurve::maxAchievableEntryVelocity(float distance, float vEnd, float vMax, float aMax, float jMax) {
    if (distance <= 0.0f) return vEnd;

    // Binary search for max vStart that can decelerate to vEnd within distance
    float vLow = vEnd;
    float vHigh = vMax;

    // Check if we can achieve max velocity
    float distAtMax = decelerationDistance(vMax, vEnd, aMax, jMax);
    if (distAtMax <= distance) {
        return vMax;
    }

    // Binary search
    for (int i = 0; i < 20; i++) {  // ~6 decimal places precision
        float vMid = (vLow + vHigh) * 0.5f;
        float dist = decelerationDistance(vMid, vEnd, aMax, jMax);

        if (dist <= distance) {
            vLow = vMid;
        } else {
            vHigh = vMid;
        }
    }

    return vLow;
}

float SCurve::maxAchievableExitVelocity(float distance, float vStart, float vMax, float aMax, float jMax) {
    if (distance <= 0.0f) return vStart;

    // Binary search for max vEnd that can be reached from vStart within distance
    float vLow = vStart;
    float vHigh = vMax;

    // Check if we can reach max velocity
    // Note: acceleration logic is symmetric to deceleration.
    // Distance to accel from vStart to vEnd is same as decel from vEnd to vStart
    float distAtMax = decelerationDistance(vMax, vStart, aMax, jMax);
    if (distAtMax <= distance) {
        return vMax;
    }

    // Binary search
    for (int i = 0; i < 20; i++) {
        float vMid = (vLow + vHigh) * 0.5f;
        float dist = decelerationDistance(vMid, vStart, aMax, jMax);

        if (dist <= distance) {
            vLow = vMid;
        } else {
            vHigh = vMid;
        }
    }

    return vLow;
}

float SCurve::getPosition(const Profile& p, float t, int& phaseIdx) {
    if (t <= 0.0f) return 0.0f;
    if (t >= p.totalTime) {
        phaseIdx = 6;
        return p.totalDistance;
    }

    // Advance phase if needed
    while (phaseIdx < 6 && t > p.tEnd[phaseIdx]) {
        phaseIdx++;
    }

    // Determine local time and start pos
    float tPhase;
    float posAtStart;

    if (phaseIdx > 0) {
        tPhase = t - p.tEnd[phaseIdx - 1];
        posAtStart = p.posEnd[phaseIdx - 1];
    } else {
        tPhase = t;
        posAtStart = 0.0f;
    }

    float v0 = p.v[phaseIdx];
    float a0 = p.a[phaseIdx];
    float j = 0.0f;

    switch (phaseIdx) {
        case 0: j = p.jerk; break;
        case 1: j = 0.0f; break;
        case 2: j = -p.jerk; break;
        case 3: j = 0.0f; break;
        case 4: j = -p.jerk; break;
        case 5: j = 0.0f; break;
        case 6: j = p.jerk; break;
    }

    if (j != 0.0f) {
        return posAtStart + jerkPhaseDistance(v0, a0, j, tPhase);
    } else {
        return posAtStart + constAccelDistance(v0, a0, tPhase);
    }
}
