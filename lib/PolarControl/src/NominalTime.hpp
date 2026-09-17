#pragma once
#include <algorithm>
#include <cmath>

// Steady-state duration estimate for one THR segment at speed multiplier 1.
// Preflight sums it over a whole file and the planner accumulates it per
// completed segment, so both sides must use this exact formula for the
// remaining-time subtraction to stay meaningful. Acceleration, jerk, and
// corner blending are ignored, making the estimate a lower bound.
inline double nominalSegmentSeconds(double dTheta, double dRho, double rhoStart, double rhoEnd,
                                    double tMaxVel, double rMaxVel, double ballMaxVel) {
    dTheta = std::abs(dTheta);
    dRho = std::abs(dRho);
    double seconds = 0.0;
    if (tMaxVel > 0)
        seconds = std::max(seconds, dTheta / tMaxVel);
    if (rMaxVel > 0)
        seconds = std::max(seconds, dRho / rMaxVel);
    if (ballMaxVel > 0) {
        const double meanRadius = 0.5 * (rhoStart + rhoEnd);
        seconds = std::max(seconds, std::hypot(meanRadius * dTheta, dRho) / ballMaxVel);
    }
    return seconds;
}
