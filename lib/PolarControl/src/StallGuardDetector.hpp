#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

// Filters the TMC2209 SG_RESULT stream for sensorless homing. SG_RESULT has a
// large electrical-phase ripple on this mechanism, so an upper-envelope EMA
// makes ordinary low phases look like a stall. Use the median of the 48 fresh
// full-step samples immediately before the decision window instead. Keeping
// the baseline window behind the decision window prevents a real collapse
// from lowering its own reference while still following sustained rail-load
// changes.
class StallGuardDetector {
public:
    StallGuardDetector(uint32_t ignoreMs, uint8_t requiredSamples,
                       float triggerRatio = 0.75f)
        : m_ignoreMs(ignoreMs),
          m_requiredSamples(std::min<uint8_t>(
              kMaximumRequiredSamples,
              std::max<uint8_t>(1, requiredSamples))),
          m_triggerRatio(triggerRatio) {}

    bool update(uint16_t sample, uint32_t elapsedMs) {
        // The caller must pass only CRC/framing-validated UART samples. A
        // checked zero is a legitimate extreme-load result.
        if (m_historyCount < m_history.size()) {
            m_history[m_historyCount++] = sample;
        } else {
            std::move(m_history.begin() + 1, m_history.end(),
                      m_history.begin());
            m_history.back() = sample;
        }

        const size_t decisionWindowSize =
            2U * static_cast<size_t>(m_requiredSamples) - 1U;
        const size_t requiredHistory = kBaselineSamples + decisionWindowSize;
        if (m_historyCount < requiredHistory) return false;

        std::array<uint16_t, kBaselineSamples> baselineWindow{};
        const size_t baselineStart =
            m_historyCount - decisionWindowSize - kBaselineSamples;
        std::copy_n(m_history.begin() + baselineStart, kBaselineSamples,
                    baselineWindow.begin());
        std::sort(baselineWindow.begin(), baselineWindow.end());
        m_baseline = (static_cast<float>(
                          baselineWindow[kBaselineSamples / 2U - 1U]) +
                      static_cast<float>(
                          baselineWindow[kBaselineSamples / 2U])) * 0.5f;

        if (elapsedMs < m_ignoreMs || m_baseline < 4.0f) return false;

        const float softThreshold = m_baseline * m_triggerRatio;
        const float confirmedThreshold = m_baseline *
            std::min(m_triggerRatio, kConfirmedCollapseRatio);
        const float hardThreshold = m_baseline * kHardCollapseRatio;
        uint8_t softLowCount = 0;
        uint8_t confirmedLowCount = 0;
        uint8_t consecutiveSoftLow = 0;
        uint8_t maximumConsecutiveSoftLow = 0;
        uint16_t minimum = UINT16_MAX;
        const size_t decisionStart = m_historyCount - decisionWindowSize;
        for (size_t index = decisionStart; index < m_historyCount; ++index) {
            const uint16_t value = m_history[index];
            minimum = std::min(minimum, value);
            if (value <= softThreshold) {
                ++softLowCount;
                ++consecutiveSoftLow;
                maximumConsecutiveSoftLow = std::max(
                    maximumConsecutiveSoftLow, consecutiveSoftLow);
            } else {
                consecutiveSoftLow = 0;
            }
            if (value <= confirmedThreshold) ++confirmedLowCount;
        }

        // A relative majority alone is insufficient: the normal trace can
        // supply four low electrical phases and a tight section can supply the
        // fifth. Require both the soft vote and a deeper collapse signature.
        // The nine-sample window at 50 full steps/mm spans about 0.18 mm, well
        // inside the commissioning allowance for confirming a hard stop.
        const bool clusteredCollapse =
            maximumConsecutiveSoftLow >= 3 &&
            minimum <= m_baseline * kClusteredCollapseRatio;
        const bool deepCollapse =
            confirmedLowCount >= m_requiredSamples && minimum <= hardThreshold;
        // The votes may include an older low cluster even when SG_RESULT has
        // recovered by the final sample. Stop only on a current low reading.
        return sample <= softThreshold &&
               softLowCount >= m_requiredSamples &&
               (clusteredCollapse || deepCollapse);
    }

    uint16_t baseline() const { return static_cast<uint16_t>(m_baseline); }
    uint16_t threshold() const {
        return static_cast<uint16_t>(m_baseline * m_triggerRatio);
    }

private:
    static constexpr uint8_t kMaximumRequiredSamples = 50;
    static constexpr size_t kMaximumDecisionWindow =
        2U * kMaximumRequiredSamples - 1U;
    // Forty-eight fresh samples span about 2 mm at the measured UART cadence.
    // Recorded-trace replay showed no false-trigger regression versus 64,
    // while it leaves enough observations to arm a bounded near-home pass.
    static constexpr size_t kBaselineSamples = 48;
    static constexpr float kConfirmedCollapseRatio = 0.70f;
    static constexpr float kClusteredCollapseRatio = 0.58f;
    // CW's bounded real-stop trace reached 0.307 of its lagged median while
    // supplying six soft-low votes. Every recorded non-stop constriction was
    // rejected by the independent five-of-nine soft vote even at 0.50 here.
    static constexpr float kHardCollapseRatio = 0.35f;

    uint32_t m_ignoreMs;
    uint8_t m_requiredSamples;
    float m_triggerRatio;
    float m_baseline = 0.0f;
    // Keep the largest supported decision window plus its lagged reference.
    std::array<uint16_t, kBaselineSamples + kMaximumDecisionWindow> m_history{};
    size_t m_historyCount = 0;
};
