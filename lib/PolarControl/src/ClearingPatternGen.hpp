#pragma once
#include <PosGen.hpp>
#include <cmath>

enum ClearingPattern {
    CLEARING_NONE,       // No clearing
    SPIRAL_OUTWARD,      // Edge → center (clockwise)
    SPIRAL_INWARD,       // Edge → center (counterclockwise)
    CONCENTRIC_CIRCLES,  // Series of circles from edge inward
    ZIGZAG_RADIAL,       // Radial zigzag pattern
    PETAL_FLOWER,        // Flower clearing pattern
    CLEARING_RANDOM      // Pick a random clearing pattern (not NONE)
};

// Number of actual clearing patterns (excluding NONE and RANDOM)
constexpr int NUM_CLEARING_PATTERNS = 5;

// Get a random clearing pattern
inline ClearingPattern getRandomClearingPattern() {
    return static_cast<ClearingPattern>(1 + (random() % NUM_CLEARING_PATTERNS));
}

class ClearingPatternGen : public PosGen {
public:
    ClearingPatternGen(ClearingPattern pattern, float maxRho = 450.0);
    PolarCord_t getNextPos() override;
    int getProgressPercent() const override;

private:
    ClearingPattern m_pattern;
    float m_maxRho;
    float m_currentTheta;
    float m_currentRho;
    int m_circleIndex;  // For concentric circles
    int m_spokeIndex;   // For zigzag radial
    bool m_inward;      // For zigzag radial direction
    bool m_complete;
    float m_lastUnwrappedTheta = 0.0f;
    bool m_haveUnwrappedTheta = false;

    // Pattern-specific generation methods
    PolarCord_t generateSpiralOutward();
    PolarCord_t generateSpiralInward();
    PolarCord_t generateConcentricCircles();
    PolarCord_t generateZigzagRadial();
    PolarCord_t generatePetalFlower();
};
