#pragma once

#include <cstdint>

// Filters the TMC2209 SG_RESULT stream for sensorless homing. The baseline is
// deliberately slow to follow load increases so a tight section does not
// immediately become the new normal. A hard stop must remain below the
// relative threshold for several consecutive samples.
class StallGuardDetector {
public:
    StallGuardDetector(uint32_t ignoreMs, uint8_t requiredSamples,
                       float triggerRatio = 0.65f)
        : m_ignoreMs(ignoreMs),
          m_requiredSamples(requiredSamples),
          m_triggerRatio(triggerRatio) {}

    bool update(uint16_t sample, uint32_t elapsedMs) {
        // The caller must pass only CRC/framing-validated UART samples. A
        // checked zero is a legitimate extreme-load result.
        if (sample > 0) {
            if (m_baseline <= 0.0f) {
                m_baseline = static_cast<float>(sample);
            } else if (sample > m_baseline) {
                m_baseline += (static_cast<float>(sample) - m_baseline) * 0.20f;
            } else if (elapsedMs < m_ignoreMs) {
                m_baseline += (static_cast<float>(sample) - m_baseline) * 0.01f;
            }
        }

        const bool enabled = elapsedMs >= m_ignoreMs && m_baseline >= 4.0f;
        if (enabled && sample <= threshold()) {
            if (m_consecutiveLow < UINT8_MAX) ++m_consecutiveLow;
        } else {
            m_consecutiveLow = 0;
        }
        return m_consecutiveLow >= m_requiredSamples;
    }

    uint16_t baseline() const { return static_cast<uint16_t>(m_baseline); }
    uint16_t threshold() const {
        return static_cast<uint16_t>(m_baseline * m_triggerRatio);
    }

private:
    uint32_t m_ignoreMs;
    uint8_t m_requiredSamples;
    float m_triggerRatio;
    float m_baseline = 0.0f;
    uint8_t m_consecutiveLow = 0;
};
