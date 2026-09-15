#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

// Compare the last three contact coordinates in the emitted-step ledger.
// Pairwise proximity is insufficient: 0, 0.3, 0.6 mm must not pass a 0.4 mm
// window merely because each adjacent pair is close.
class RhoContactConsensus {
public:
    explicit RhoContactConsensus(uint32_t toleranceSteps)
        : m_tolerance(toleranceSteps) {}

    bool add(int64_t coordinate) {
        m_points[0] = m_points[1];
        m_points[1] = m_points[2];
        m_points[2] = coordinate;
        m_count = std::min<uint8_t>(3, m_count + 1);
        if (m_count < 3) return false;
        const auto limits = std::minmax_element(m_points.begin(), m_points.end());
        return *limits.second - *limits.first <= m_tolerance;
    }

private:
    uint32_t m_tolerance;
    uint8_t m_count = 0;
    std::array<int64_t, 3> m_points{};
};
