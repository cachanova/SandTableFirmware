#include "ClearingPatternGen.hpp"
#include "Logger.hpp"
#include <algorithm>

ClearingPatternGen::ClearingPatternGen(ClearingPattern pattern, float maxRho)
    : m_pattern(pattern),
      m_maxRho(maxRho),
      m_currentTheta(0.0),
      m_currentRho(0.0),
      m_circleIndex(0),
      m_spokeIndex(0),
      m_inward(false),
      m_complete(false) {

    if (m_pattern == ZIGZAG_RADIAL) {
        m_currentRho = m_maxRho;
        m_inward = true;
    }

    LOG("ClearingPatternGen created: pattern=%d, maxRho=%.2f\r\n", pattern, maxRho);
}

PolarCord_t ClearingPatternGen::getNextPos() {
    if (m_complete) {
        return {std::nan(""), std::nan("")};
    }

    switch (m_pattern) {
        case CLEARING_NONE:
            m_complete = true;
            return {std::nan(""), std::nan("")};
        case SPIRAL_OUTWARD:
            return generateSpiralOutward();
        case SPIRAL_INWARD:
            return generateSpiralInward();
        case CONCENTRIC_CIRCLES:
            return generateConcentricCircles();
        case ZIGZAG_RADIAL:
            return generateZigzagRadial();
        case PETAL_FLOWER:
            return generatePetalFlower();
        case CLEARING_RANDOM:
            // Should not happen - caller should resolve RANDOM before creating
            m_pattern = getRandomClearingPattern();
            return getNextPos();
        default:
            m_complete = true;
            return {std::nan(""), std::nan("")};
    }
}

int ClearingPatternGen::getProgressPercent() const {
    if (m_complete) {
        return 100;
    }

    switch (m_pattern) {
        case CLEARING_NONE:
            return -1;
        case SPIRAL_OUTWARD:
        case SPIRAL_INWARD: {
            static constexpr float NUM_ROTATIONS = 32.0f;
            static constexpr float TOTAL_THETA = NUM_ROTATIONS * 2.0f * PI;
            float progress = (TOTAL_THETA > 0.0f) ? (m_currentTheta / TOTAL_THETA) : 0.0f;
            progress = std::min(1.0f, std::max(0.0f, progress));
            return static_cast<int>(lroundf(progress * 100.0f));
        }
        case CONCENTRIC_CIRCLES: {
            static constexpr int NUM_CIRCLES = 14;
            static constexpr float CIRCLE_THETA_MAX = 2.0f * PI;
            if (m_circleIndex >= NUM_CIRCLES) {
                return 100;
            }
            const float localTheta = m_currentTheta - m_circleIndex * CIRCLE_THETA_MAX;
            float thetaFrac = (CIRCLE_THETA_MAX > 0.0f) ? (localTheta / CIRCLE_THETA_MAX) : 0.0f;
            thetaFrac = std::min(1.0f, std::max(0.0f, thetaFrac));
            float progress = (static_cast<float>(m_circleIndex) + thetaFrac) / NUM_CIRCLES;
            progress = std::min(1.0f, std::max(0.0f, progress));
            return static_cast<int>(lroundf(progress * 100.0f));
        }
        case ZIGZAG_RADIAL: {
            static constexpr float Y_STEP = 16.0f;
            static constexpr float X_STEP = 16.0f;
            if (m_maxRho <= 0.0f) {
                return -1;
            }
            int totalRows = static_cast<int>(std::floor((2.0f * m_maxRho) / Y_STEP)) + 1;
            if (totalRows <= 0) {
                return -1;
            }
            float y = -m_maxRho + (m_circleIndex * Y_STEP);
            if (y > m_maxRho) {
                return 100;
            }
            float xMax = std::sqrt(std::max(0.0f, (m_maxRho * m_maxRho) - (y * y)));
            int maxSteps = 0;
            if (X_STEP > 0.0f && xMax > 0.0f) {
                maxSteps = static_cast<int>(std::floor((2.0f * xMax) / X_STEP));
            }
            float rowFrac = 0.0f;
            if (maxSteps <= 0) {
                rowFrac = 1.0f;
            } else {
                rowFrac = static_cast<float>(m_spokeIndex) / static_cast<float>(maxSteps);
                rowFrac = std::min(1.0f, std::max(0.0f, rowFrac));
            }
            float progress = (static_cast<float>(m_circleIndex) + rowFrac) / totalRows;
            progress = std::min(1.0f, std::max(0.0f, progress));
            return static_cast<int>(lroundf(progress * 100.0f));
        }
        case PETAL_FLOWER: {
            static constexpr int NUM_PASSES = 8;
            static constexpr float PASS_THETA_MAX = 2.0f * PI;
            if (m_circleIndex >= NUM_PASSES) {
                return 100;
            }
            const float localTheta = m_currentTheta - m_circleIndex * PASS_THETA_MAX;
            float thetaFrac = (PASS_THETA_MAX > 0.0f) ? (localTheta / PASS_THETA_MAX) : 0.0f;
            thetaFrac = std::min(1.0f, std::max(0.0f, thetaFrac));
            float progress = (static_cast<float>(m_circleIndex) + thetaFrac) / NUM_PASSES;
            progress = std::min(1.0f, std::max(0.0f, progress));
            return static_cast<int>(lroundf(progress * 100.0f));
        }
        case CLEARING_RANDOM:
        default:
            return -1;
    }
}

PolarCord_t ClearingPatternGen::generateSpiralOutward() {
    // Spiral from edge (rho=maxRho) to center (rho=0) over 10 rotations (clockwise)
    static constexpr float NUM_ROTATIONS = 32.0f;
    static constexpr float TOTAL_THETA = NUM_ROTATIONS * 2.0f * PI;
    static constexpr float THETA_STEP = 0.12f;

    if (m_currentTheta >= TOTAL_THETA) {
        m_complete = true;
        LOG("Spiral outward complete\r\n");
        return {std::nan(""), std::nan("")};
    }

    if (m_currentTheta + THETA_STEP >= TOTAL_THETA) {
        m_complete = true;
        LOG("Spiral outward complete\r\n");
        return {-TOTAL_THETA, 0.0f};
    }

    // Linear rho decrease from maxRho to 0
    float rho = m_maxRho * (1.0f - m_currentTheta / TOTAL_THETA);
    float theta = -m_currentTheta;

    m_currentTheta += THETA_STEP;

    return {theta, rho};
}

PolarCord_t ClearingPatternGen::generateSpiralInward() {
    // Spiral from edge (rho=maxRho) to center (rho=0) over 10 rotations (counterclockwise)
    static constexpr float NUM_ROTATIONS = 32.0f;
    static constexpr float TOTAL_THETA = NUM_ROTATIONS * 2.0f * PI;
    static constexpr float THETA_STEP = 0.12f;

    if (m_currentTheta >= TOTAL_THETA) {
        m_complete = true;
        LOG("Spiral inward complete\r\n");
        return {std::nan(""), std::nan("")};
    }

    if (m_currentTheta + THETA_STEP >= TOTAL_THETA) {
        m_complete = true;
        LOG("Spiral inward complete\r\n");
        return {TOTAL_THETA, 0.0f};
    }

    // Linear rho decrease from maxRho to 0
    float rho = m_maxRho * (1.0f - m_currentTheta / TOTAL_THETA);
    float theta = m_currentTheta;

    m_currentTheta += THETA_STEP;

    return {theta, rho};
}

PolarCord_t ClearingPatternGen::generateConcentricCircles() {
    // 9 circles at evenly spaced radii
    static constexpr int NUM_CIRCLES = 14;
    static constexpr float THETA_STEP = 0.12f;
    static constexpr float CIRCLE_THETA_MAX = 2.0f * PI;
    static constexpr int LAST_CIRCLE_INDEX = NUM_CIRCLES - 1;

    if (m_circleIndex >= NUM_CIRCLES) {
        m_complete = true;
        LOG("Concentric circles complete\r\n");
        return {std::nan(""), std::nan("")};
    }

    // Current circle radius
    float rho = 0.0f;
    if (LAST_CIRCLE_INDEX > 0) {
        rho = m_maxRho * (LAST_CIRCLE_INDEX - m_circleIndex) / static_cast<float>(LAST_CIRCLE_INDEX);
    }

    const float circleStart = m_circleIndex * CIRCLE_THETA_MAX;
    // Generate points around the circle without wrapping theta back to zero.
    if (m_currentTheta - circleStart >= CIRCLE_THETA_MAX) {
        // Move to next circle
        m_circleIndex++;
        m_currentTheta = m_circleIndex * CIRCLE_THETA_MAX;

        // Return to the start of the next circle
        if (m_circleIndex < NUM_CIRCLES) {
            rho = 0.0f;
            if (LAST_CIRCLE_INDEX > 0) {
                rho = m_maxRho * (LAST_CIRCLE_INDEX - m_circleIndex) / static_cast<float>(LAST_CIRCLE_INDEX);
            }
            return {m_currentTheta, rho};
        } else {
            m_complete = true;
            return {std::nan(""), std::nan("")};
        }
    }

    float theta = m_currentTheta;
    m_currentTheta += THETA_STEP;

    return {theta, rho};
}

PolarCord_t ClearingPatternGen::generateZigzagRadial() {
    // Horizontal zigzag across circular table (edge-to-edge chords).
    static constexpr float Y_STEP = 16.0f; // mm between zigzag rows
    static constexpr float X_STEP = 16.0f; // mm along each row

    float y = -m_maxRho + (m_circleIndex * Y_STEP);
    if (y > m_maxRho) {
        m_complete = true;
        LOG("Zigzag across complete\r\n");
        return {std::nan(""), std::nan("")};
    }

    float xMax = std::sqrt(std::max(0.0f, (m_maxRho * m_maxRho) - (y * y)));
    float startX = -xMax;
    float endX = xMax;
    bool leftToRight = (m_circleIndex % 2 == 0);

    int maxSteps = 0;
    if (X_STEP > 0.0f && xMax > 0.0f) {
        maxSteps = static_cast<int>(std::floor((2.0f * xMax) / X_STEP));
    }

    if (m_spokeIndex > maxSteps) {
        m_circleIndex++;
        m_spokeIndex = 0;
        return generateZigzagRadial();
    }

    float x = leftToRight ? (startX + (m_spokeIndex * X_STEP))
                          : (endX - (m_spokeIndex * X_STEP));
    if (x < startX) x = startX;
    if (x > endX) x = endX;

    m_spokeIndex++;

    float rho = std::sqrt((x * x) + (y * y));
    float theta = std::atan2(y, x);
    if (m_haveUnwrappedTheta) {
        theta += roundf((m_lastUnwrappedTheta - theta) / (2.0f * PI)) * (2.0f * PI);
    }
    m_lastUnwrappedTheta = theta;
    m_haveUnwrappedTheta = true;
    return {theta, rho};
}

PolarCord_t ClearingPatternGen::generatePetalFlower() {
    // Petal flower with 8 petals, multiple passes at decreasing amplitudes
    static constexpr int NUM_PETALS = 8;
    static constexpr int NUM_PASSES = 8; // 8 passes at different amplitudes
    static constexpr float THETA_STEP = 0.08f; // Coarser step for faster clears
    static constexpr float PASS_THETA_MAX = 2.0f * PI;
    static constexpr int LAST_PASS_INDEX = NUM_PASSES - 1;

    if (m_circleIndex >= NUM_PASSES) {
        m_complete = true;
        LOG("Petal flower complete\r\n");
        return {std::nan(""), std::nan("")};
    }

    // Amplitude decreases with each pass
    float amplitude = 0.0f;
    if (LAST_PASS_INDEX > 0) {
        amplitude = m_maxRho * (LAST_PASS_INDEX - m_circleIndex) / static_cast<float>(LAST_PASS_INDEX);
    }

    const float passStart = m_circleIndex * PASS_THETA_MAX;
    if (m_currentTheta - passStart >= PASS_THETA_MAX) {
        // Move to next pass
        m_circleIndex++;
        m_currentTheta = m_circleIndex * PASS_THETA_MAX;

        if (m_circleIndex >= NUM_PASSES) {
            m_complete = true;
            return {std::nan(""), std::nan("")};
        }

        // Start next pass
        amplitude = 0.0f;
        if (LAST_PASS_INDEX > 0) {
            amplitude = m_maxRho * (LAST_PASS_INDEX - m_circleIndex) / static_cast<float>(LAST_PASS_INDEX);
        }
    }

    float theta = m_currentTheta;
    const float localTheta = theta - m_circleIndex * PASS_THETA_MAX;
    // Petal function: rho = amplitude * |cos(NUM_PETALS * theta)|
    float rho = amplitude * std::abs(cos(NUM_PETALS * localTheta));

    m_currentTheta += THETA_STEP;

    return {theta, rho};
}
