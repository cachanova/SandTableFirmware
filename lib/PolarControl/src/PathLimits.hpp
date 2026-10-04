#pragma once
#include <algorithm>
#include <cmath>
#include "PolarPath.hpp"

// Motion limits shared by the planner and the run-time estimate.
struct MotionLimits {
    double tMaxVel = 0, tMaxAccel = 0, tMaxJerk = 0;
    double rMaxVel = 0, rMaxAccel = 0, rMaxJerk = 0;
    double ballMaxVelocity = 0, ballMaxAcceleration = 0;
    double speedMultiplier = 1;
};

// Scalar velocity, acceleration and jerk limits along `path` for a segment
// covering `distance` of it in at least minDuration seconds. Projects the axis
// and ball limits through convex-hull derivative bounds of the whole curve.
inline void pathLimits(const PolarPath& path, double distance, double minDuration,
                       const MotionLimits& m, double& v, double& a, double& j) {
    PathPoint d1, d2, d3;
    path.derivativeBounds(d1, d2, d3);
    v = distance / minDuration;
    a = 1e12;
    j = 1e12;
    auto axis = [&](double first, double second, double third, double vmax, double amax,
                    double jmax) {
        if (first < 1e-15)
            return;
        v = std::min(v, vmax * m.speedMultiplier / first);
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
    axis(d1.theta, d2.theta, d3.theta, m.tMaxVel, m.tMaxAccel, m.tMaxJerk);
    axis(d1.rho, d2.rho, d3.rho, m.rMaxVel, m.rMaxAccel, m.rMaxJerk);
    // Bound Cartesian derivatives, including centripetal and Coriolis terms.
    const double r = std::max(path.start.rho, path.end.rho);
    const double cartFirst = std::hypot(d1.rho, r * d1.theta);
    const double cartSecond =
        d2.rho + r * d2.theta + 2 * d1.rho * d1.theta + r * d1.theta * d1.theta;
    if (m.ballMaxVelocity > 0 && cartFirst > 1e-15)
        v = std::min(v, m.ballMaxVelocity * m.speedMultiplier / cartFirst);
    if (m.ballMaxAcceleration > 0 && cartFirst > 1e-15) {
        if (cartSecond > 1e-14) {
            v = std::min(v, std::sqrt(m.ballMaxAcceleration / (2 * cartSecond)));
            a = std::min(a, m.ballMaxAcceleration / (2 * cartFirst));
        } else
            a = std::min(a, m.ballMaxAcceleration / cartFirst);
    }
    // Remaining jerk budget for 3*q''*v*a, after q'*j and q'''*v^3.
    if (d2.theta > 1e-14)
        a = std::min(a, m.tMaxJerk / (9 * d2.theta * v));
    if (d2.rho > 1e-14)
        a = std::min(a, m.rMaxJerk / (9 * d2.rho * v));
}
