#pragma once
#include <cmath>

// Whole-turn offset that places a pattern's first angle within half a turn of
// the ball's current angle. Adding it to every file angle leaves the drawing
// unchanged but avoids unwinding turns left by the previous pattern, which
// would otherwise draw a spiral on the way to a first point away from the
// center.
inline double nearestTurnOffset(double currentTheta, double firstTheta) {
    constexpr double kFullTurn = 2.0 * 3.14159265358979323846;
    if (!std::isfinite(currentTheta) || !std::isfinite(firstTheta)) return 0.0;
    return std::round((currentTheta - firstTheta) / kFullTurn) * kFullTurn;
}
