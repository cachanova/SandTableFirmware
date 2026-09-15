#pragma once

#include <algorithm>
#include <cstdint>

// Inward-positive command coordinates, zero at the first approach's start.
// Outward STEP commands must actually move. knownHome is an independent
// commissioning reference, never an SG candidate.
class RhoRollingSearch {
public:
    RhoRollingSearch(uint32_t knownHome, uint32_t perPass, uint32_t total)
        : m_home(knownHome), m_furthest(knownHome),
          m_perPass(perPass), m_total(total) {}

    uint32_t approachLimit() const {
        const uint32_t remaining = m_total - usedOverrun();
        return m_furthest + std::min(m_perPass, remaining) - m_coordinate;
    }
    uint32_t backoffLimit(uint32_t requested) const {
        return std::min(requested, m_coordinate - usedOverrun());
    }
    bool inward(uint32_t steps) {
        if (steps > approachLimit()) return false;
        m_coordinate += steps;
        m_furthest = std::max(m_furthest, m_coordinate);
        return true;
    }
    bool outward(uint32_t steps) {
        if (steps > backoffLimit(steps)) return false;
        m_coordinate -= steps;
        return true;
    }
    uint32_t coordinate() const { return m_coordinate; }
    uint32_t usedOverrun() const { return m_furthest - m_home; }
    bool nearKnownHome(uint32_t earlyTolerance) const {
        // Commands into the stop shift the ledger, but not physical home.
        // A later cluster must also be close to the deepest prior coordinate.
        return m_furthest - m_coordinate <= earlyTolerance;
    }

private:
    uint32_t m_home;
    uint32_t m_coordinate = 0;
    uint32_t m_furthest;
    uint32_t m_perPass;
    uint32_t m_total;
};
