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
        const float deepPulseThreshold = m_baseline * kDeepPulseRatio;
        uint8_t softLowCount = 0;
        uint8_t confirmedLowCount = 0;
        uint8_t deepPulseCount = 0;
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
            if (value <= deepPulseThreshold) ++deepPulseCount;
        }

        // A relative majority alone can mistake alternating electrical phases
        // for contact. Require a cluster or deep collapse. This proposes a
        // contact; the caller requires agreement across independent approaches.
        // UART cadence determines the distance spanned by this sample window.
        const bool clusteredCollapse = maximumConsecutiveSoftLow >= 3;
        const bool deepCollapse =
            confirmedLowCount >= m_requiredSamples && minimum <= hardThreshold;
        // At a hard stop, this motor can alternate near-zero SG_RESULT with
        // normal electrical-phase readings. Two distinct deep pulses in
        // the decision window establish contact without requiring adjacent
        // low phases. A single low spike still cannot trigger this path.
        const bool repeatedDeepPulses =
            deepPulseCount >= 2 && sample <= deepPulseThreshold;
        // The votes may include an older low cluster even when SG_RESULT has
        // recovered by the final sample. Stop only on a current low reading.
        return repeatedDeepPulses ||
               (sample <= softThreshold &&
                softLowCount >= m_requiredSamples &&
                (clusteredCollapse || deepCollapse));
    }

    uint16_t baseline() const { return static_cast<uint16_t>(m_baseline); }
    uint16_t threshold() const {
        return static_cast<uint16_t>(m_baseline * m_triggerRatio);
    }

private:
    static constexpr uint8_t kMaximumRequiredSamples = 50;
    static constexpr size_t kMaximumDecisionWindow =
        2U * kMaximumRequiredSamples - 1U;
    // Keep the baseline separate from the decision window. The distance
    // covered by these samples depends on velocity and actual UART cadence;
    // it is not a fixed physical length.
    static constexpr size_t kBaselineSamples = 48;
    static constexpr float kConfirmedCollapseRatio = 0.70f;
    // CW's bounded real-stop trace reached 0.307 of its lagged median while
    // supplying six soft-low votes. Every recorded non-stop constriction was
    // rejected by the independent five-of-nine soft vote even at 0.50 here.
    static constexpr float kHardCollapseRatio = 0.35f;
    static constexpr float kDeepPulseRatio = 0.10f;

    uint32_t m_ignoreMs;
    uint8_t m_requiredSamples;
    float m_triggerRatio;
    float m_baseline = 0.0f;
    // Keep the largest supported decision window plus its lagged reference.
    std::array<uint16_t, kBaselineSamples + kMaximumDecisionWindow> m_history{};
    size_t m_historyCount = 0;
};
