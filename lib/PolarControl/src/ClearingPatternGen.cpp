#include "ClearingPatternGen.hpp"
#include "Logger.hpp"
#include "NominalTime.hpp"
#include <algorithm>

namespace {
constexpr double FULL_TURN = 2.0 * PI;
constexpr double THETA_STEP = 0.12;        // rad between spiral and ring points
constexpr double PETAL_THETA_STEP = 0.03;  // rad; finer steps keep the planner near nominal speed
constexpr double ROW_STEP_MAX = 16.0;      // mm between zigzag points at the rim
constexpr double ROW_STEP_MIN = 1.0;       // mm between zigzag points near the centre
constexpr int PETALS = 5;
// Petal depth as a fraction of the distance to the nearer of rim and centre,
// so petals fade out at both ends and every turn still nests in the last.
constexpr double PETAL_DEPTH = 0.1;
const PolarCord_t DONE = {std::nan(""), std::nan("")};

int passesFor(double span, double spacing) {
    return std::max(1, static_cast<int>(std::ceil(span / spacing - 1e-9)));
}
}  // namespace

ClearingPatternGen::ClearingPatternGen(ClearingPattern pattern, float maxRho)
    : m_pattern(pattern), m_maxRho(maxRho) {
    if (m_pattern == ZIGZAG_RADIAL) {
        m_passes = passesFor(2.0 * m_maxRho, MAX_PASS_SPACING);
        m_spacing = 2.0 * m_maxRho / m_passes;
        m_index = -1;  // rim lap first
    } else if (m_pattern == PETAL_FLOWER) {
        // Petal depth changes by at most PETAL_DEPTH per unit of radius, so
        // the radial gap between turns is at most pitch * (1 + PETAL_DEPTH).
        m_passes = passesFor(m_maxRho * (1.0 + PETAL_DEPTH), MAX_PASS_SPACING);
        m_spacing = m_maxRho / m_passes;
    } else {
        m_passes = passesFor(m_maxRho, MAX_PASS_SPACING);
        m_spacing = m_maxRho / m_passes;
    }
    LOG("ClearingPatternGen created: pattern=%d, maxRho=%.2f, passes=%d\r\n",
        pattern, maxRho, m_passes);
}

const char* ClearingPatternGen::displayName(ClearingPattern pattern) {
    switch (pattern) {
        case SPIRAL_OUTWARD: return "Spiral (CW)";
        case SPIRAL_INWARD: return "Spiral (CCW)";
        case CONCENTRIC_CIRCLES: return "Concentric Circles";
        case ZIGZAG_RADIAL: return "Zigzag";
        case PETAL_FLOWER: return "Petal Flower";
        case CLEARING_RANDOM: return "Random";
        case CLEARING_NONE:
        default: return "";
    }
}

double ClearingPatternGen::plannerOverhead(ClearingPattern pattern) {
    switch (pattern) {
        case CONCENTRIC_CIRCLES: return 1.02;
        case ZIGZAG_RADIAL: return 1.53;
        case PETAL_FLOWER: return 1.35;
        default: return 1.0;
    }
}

PolarCord_t ClearingPatternGen::getNextPos() {
    if (m_complete) {
        return DONE;
    }

    switch (m_pattern) {
        case SPIRAL_OUTWARD:
            return nextSpiral(-1.0);
        case SPIRAL_INWARD:
            return nextSpiral(1.0);
        case CONCENTRIC_CIRCLES:
            return nextRings();
        case ZIGZAG_RADIAL:
            return nextZigzag();
        case PETAL_FLOWER:
            return nextPetal();
        case CLEARING_RANDOM:
            // Should not happen - caller should resolve RANDOM before creating
            *this = ClearingPatternGen(getRandomClearingPattern(), m_maxRho);
            return getNextPos();
        case CLEARING_NONE:
        default:
            m_complete = true;
            return DONE;
    }
}

double ClearingPatternGen::nominalSeconds(PolarCord_t from, double tMaxVel, double rMaxVel,
                                          double ballMaxVel) const {
    ClearingPatternGen fresh(m_pattern, m_maxRho);
    double seconds = 0.0;
    for (PolarCord_t to = fresh.getNextPos(); !to.isNan(); to = fresh.getNextPos()) {
        seconds += nominalSegmentSeconds(to.theta - from.theta, to.rho - from.rho, from.rho,
                                         to.rho, tMaxVel, rMaxVel, ballMaxVel);
        from = to;
    }
    return seconds;
}

PolarCord_t ClearingPatternGen::nextSpiral(double direction) {
    // One lap on the rim, then an Archimedean spiral to the centre whose turns
    // are m_spacing apart.
    const double total = FULL_TURN * (m_passes + 1);
    const double phi = std::min(m_param, total);
    const double rho = phi <= FULL_TURN ? m_maxRho
                                     : std::max(0.0, m_maxRho - m_spacing * (phi - FULL_TURN) / FULL_TURN);
    if (phi >= total) {
        m_complete = true;
        LOG("Spiral clearing complete\r\n");
        return {direction * total, 0.0};
    }
    m_param += THETA_STEP;
    return {direction * phi, rho};
}

PolarCord_t ClearingPatternGen::nextRings() {
    // Rings m_spacing apart from the rim inward, alternating direction so
    // theta never winds up; each ring starts where the last one ended.
    const bool forward = (m_index % 2) == 0;
    if (m_index >= m_passes) {
        m_complete = true;
        LOG("Concentric circles complete\r\n");
        return {forward ? 0.0 : FULL_TURN, 0.0};
    }
    const double phi = std::min(m_param, FULL_TURN);
    const double rho = m_maxRho - m_index * m_spacing;
    if (phi >= FULL_TURN) {
        ++m_index;
        m_param = 0.0;
    } else {
        m_param += THETA_STEP;
    }
    return {forward ? phi : FULL_TURN - phi, rho};
}

PolarCord_t ClearingPatternGen::nextZigzag() {
    // One lap on the rim from the bottom, then parallel chords m_spacing
    // apart from the bottom extreme to the top. Without the lap the rim
    // between the shortest chords' ends is only reached from one side.
    // Points tighten near the centre so polar interpolation stays straight.
    if (m_index < 0) {
        const double phi = std::min(m_param, FULL_TURN);
        m_lastTheta = -0.5 * PI + phi;
        if (phi >= FULL_TURN) {
            m_index = 0;
            m_param = 0.0;
        } else {
            m_param += THETA_STEP;
        }
        return {m_lastTheta, m_maxRho};
    }
    const double y = -m_maxRho + m_index * m_spacing;
    const double halfWidth = std::sqrt(std::max(0.0, m_maxRho * m_maxRho - y * y));
    const double s = std::min(m_param, 2.0 * halfWidth);
    const double x = (m_index % 2) == 0 ? -halfWidth + s : halfWidth - s;
    const double rho = std::min(m_maxRho, std::hypot(x, y));

    double theta = m_lastTheta;
    if (rho > 1e-6) {
        theta = std::atan2(y, x);
        theta += std::round((m_lastTheta - theta) / FULL_TURN) * FULL_TURN;
    }
    m_lastTheta = theta;

    if (s >= 2.0 * halfWidth) {
        m_param = 0.0;
        if (++m_index > m_passes) {
            m_complete = true;
            LOG("Zigzag clearing complete\r\n");
        }
    } else {
        m_param += std::max(ROW_STEP_MIN, std::min(ROW_STEP_MAX, 0.25 * rho));
    }
    return {theta, rho};
}

double ClearingPatternGen::petalRho(double phi) const {
    if (phi <= FULL_TURN) {
        return m_maxRho;
    }
    const double base = m_maxRho - m_spacing * (phi - FULL_TURN) / FULL_TURN;
    if (base <= 0.0) {
        return 0.0;
    }
    const double depth = PETAL_DEPTH * std::min(base, m_maxRho - base);
    return std::max(0.0, std::min(m_maxRho, base + depth * std::cos(PETALS * phi)));
}

PolarCord_t ClearingPatternGen::nextPetal() {
    // One lap on the rim, then a spiral whose radius swells into PETALS
    // petals per turn.
    const double total = FULL_TURN * (m_passes + 1);
    const double phi = std::min(m_param, total);
    if (phi >= total) {
        m_complete = true;
        LOG("Petal flower complete\r\n");
        return {total, 0.0};
    }
    m_param += PETAL_THETA_STEP;
    return {phi, petalRho(phi)};
}
