#pragma once
#include <PosGen.hpp>
#include <cmath>

enum ClearingPattern {
    CLEARING_NONE,       // No clearing
    SPIRAL_OUTWARD,      // Rim lap, then spiral to the centre (clockwise); legacy name
    SPIRAL_INWARD,       // Rim lap, then spiral to the centre (counterclockwise)
    CONCENTRIC_CIRCLES,  // Rings from the rim inward
    ZIGZAG_RADIAL,       // Parallel edge-to-edge chords; legacy name
    PETAL_FLOWER,        // Scalloped spiral from the rim inward
    CLEARING_RANDOM      // Pick a random clearing pattern (not NONE)
};

// Number of actual clearing patterns (excluding NONE and RANDOM)
constexpr int NUM_CLEARING_PATTERNS = 5;

// Get a random clearing pattern
inline ClearingPattern getRandomClearingPattern() {
    return static_cast<ClearingPattern>(1 + (random() % NUM_CLEARING_PATTERNS));
}

// Every pattern sweeps the whole disc: neighbouring passes are never farther
// apart than MAX_PASS_SPACING, the outermost pass runs along the rim, and the
// path reaches the centre (zigzag reaches both rim extremes instead).
// Coordinates are THR-style (theta, rho); the planner interpolates linearly in
// polar space, so spirals and rings are exact between points.
class ClearingPatternGen : public PosGen {
public:
    // Centre-to-centre spacing limit between passes. A 1/2" ball ploughs a
    // groove about 9-11 mm wide, so 9 mm leaves no ridge between passes.
    static constexpr double MAX_PASS_SPACING = 9.0;

    ClearingPatternGen(ClearingPattern pattern, float maxRho = 450.0);
    PolarCord_t getNextPos() override;
    ClearingPattern pattern() const { return m_pattern; }

    // Full-speed steady-state duration of the whole sweep starting at `from`,
    // summed with the planner's own per-segment formula (NominalTime.hpp).
    double nominalSeconds(PolarCord_t from, double tMaxVel, double rMaxVel,
                          double ballMaxVel) const;

    // Planned-to-nominal duration ratio over a whole sweep, from the native
    // planner simulation at the default motion settings. Nominal time ignores
    // acceleration and jerk, which matter for zigzag's reversals and the
    // petal's radial oscillation but hardly at all for spirals and rings.
    static double plannerOverhead(ClearingPattern pattern);

    static const char* displayName(ClearingPattern pattern);

private:
    PolarCord_t nextSpiral(double direction);
    PolarCord_t nextRings();
    PolarCord_t nextZigzag();
    PolarCord_t nextPetal();
    double petalRho(double phi) const;

    ClearingPattern m_pattern;
    double m_maxRho;
    int m_passes;        // spiral turns, rings, or zigzag row gaps
    double m_spacing;    // radial or row spacing actually used (<= MAX_PASS_SPACING)
    double m_param = 0;  // swept angle (spiral, ring, petal) or distance along a row
    int m_index = 0;     // current ring or row
    bool m_complete = false;
    double m_lastTheta = 0;
};
