#include "PresenceDetector.hpp"

#include <algorithm>
#include <cmath>

PresenceDetector::PresenceDetector() = default;

bool PresenceDetector::startCalibration() {
    if (m_suppressed) return false;

    m_phase = Phase::BASELINE;
    m_binCount = 0;
    m_phaseSamples = 0;
    m_noiseMean = 0.0f;
    m_noiseM2 = 0.0f;
    m_threshold = 0.0f;
    m_acceptedSamples = 0;
    std::fill(m_baseline, m_baseline + kMaxBins, 0.0f);
    resetRuntime();
    m_everDetectedMotion = false;
    m_lastMotionMs = 0;
    return true;
}

void PresenceDetector::setSuppressed(bool suppressed) {
    if (m_suppressed == suppressed) return;
    m_suppressed = suppressed;
    resetRuntime();
}

bool PresenceDetector::normalize(const float* powers, size_t count,
                                 float* normalized) const {
    if (powers == nullptr || normalized == nullptr || count == 0 ||
        count > kMaxBins) {
        return false;
    }

    float mean = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        if (!std::isfinite(powers[i]) || powers[i] < 0.0f) return false;
        mean += powers[i];
    }
    mean /= static_cast<float>(count);
    if (!std::isfinite(mean) || mean <= 0.0f) return false;

    for (size_t i = 0; i < count; ++i) normalized[i] = powers[i] / mean;
    return true;
}

float PresenceDetector::distanceFromBaseline(const float* normalized) const {
    float squaredError = 0.0f;
    for (size_t i = 0; i < m_binCount; ++i) {
        const float delta = normalized[i] - m_baseline[i];
        squaredError += delta * delta;
    }
    return std::sqrt(squaredError / static_cast<float>(m_binCount));
}

void PresenceDetector::resetRuntime() {
    m_motion = false;
    m_smoothedDistance = 0.0f;
    m_hasSmoothedDistance = false;
    m_enterCount = 0;
    m_exitCount = 0;
}

bool PresenceDetector::addPowers(const float* powers, size_t count,
                                 uint32_t nowMs) {
    if (m_suppressed) return false;

    float normalized[kMaxBins];
    if (!normalize(powers, count, normalized)) return false;

    if (m_phase == Phase::BASELINE) {
        if (m_binCount == 0) m_binCount = count;
        if (count != m_binCount) return false;

        ++m_phaseSamples;
        const float divisor = static_cast<float>(m_phaseSamples);
        for (size_t i = 0; i < m_binCount; ++i) {
            m_baseline[i] += (normalized[i] - m_baseline[i]) / divisor;
        }
        ++m_acceptedSamples;
        if (m_phaseSamples >= kBaselineSamples) {
            m_phase = Phase::NOISE;
            m_phaseSamples = 0;
            m_noiseMean = 0.0f;
            m_noiseM2 = 0.0f;
        }
        return true;
    }

    if (m_phase == Phase::NOISE) {
        if (count != m_binCount) return false;
        const float distance = distanceFromBaseline(normalized);
        ++m_phaseSamples;
        const float delta = distance - m_noiseMean;
        m_noiseMean += delta / static_cast<float>(m_phaseSamples);
        m_noiseM2 += delta * (distance - m_noiseMean);
        ++m_acceptedSamples;
        if (m_phaseSamples >= kNoiseSamples) {
            const float variance = m_phaseSamples > 1
                ? m_noiseM2 / static_cast<float>(m_phaseSamples - 1)
                : 0.0f;
            const float standardDeviation = std::sqrt(std::max(0.0f, variance));
            m_threshold = std::max(kMinimumThreshold,
                std::max(m_noiseMean + 4.0f * standardDeviation,
                         m_noiseMean * 2.5f));
            m_phase = Phase::READY;
            resetRuntime();
        }
        return true;
    }

    if (m_phase != Phase::READY || count != m_binCount) return false;

    const float distance = distanceFromBaseline(normalized);
    if (!m_hasSmoothedDistance) {
        m_smoothedDistance = distance;
        m_hasSmoothedDistance = true;
    } else {
        m_smoothedDistance = 0.8f * m_smoothedDistance + 0.2f * distance;
    }
    ++m_acceptedSamples;

    const float enterThreshold = m_threshold;
    const float exitThreshold = m_threshold * 0.6f;
    if (!m_motion) {
        m_exitCount = 0;
        m_enterCount = m_smoothedDistance >= enterThreshold
            ? static_cast<uint8_t>(m_enterCount + 1) : 0;
        if (m_enterCount >= kMotionEnterSamples) {
            m_motion = true;
            m_everDetectedMotion = true;
            m_lastMotionMs = nowMs;
            m_enterCount = 0;
        }
    } else {
        if (m_smoothedDistance >= enterThreshold) m_lastMotionMs = nowMs;
        m_enterCount = 0;
        m_exitCount = m_smoothedDistance <= exitThreshold
            ? static_cast<uint8_t>(m_exitCount + 1) : 0;
        if (m_exitCount >= kMotionExitSamples) {
            m_motion = false;
            m_exitCount = 0;
        }
    }

    if (!m_motion && m_smoothedDistance < exitThreshold * 0.5f) {
        for (size_t i = 0; i < m_binCount; ++i) {
            m_baseline[i] += kBaselineAdaptation *
                (normalized[i] - m_baseline[i]);
        }
    }
    return true;
}

PresenceDetector::Status PresenceDetector::status(uint32_t nowMs) const {
    Status result;
    result.phase = m_phase;
    result.suppressed = m_suppressed;
    result.motion = m_motion;
    result.occupied = m_everDetectedMotion &&
        static_cast<uint32_t>(nowMs - m_lastMotionMs) < kOccupancyHoldMs;
    result.threshold = m_threshold;
    result.score = m_threshold > 0.0f ? m_smoothedDistance / m_threshold : 0.0f;
    result.lastMotionMs = m_lastMotionMs;
    result.acceptedSamples = m_acceptedSamples;

    if (m_phase == Phase::BASELINE) {
        result.calibrationProgress = static_cast<uint8_t>(
            (static_cast<uint32_t>(m_phaseSamples) * 50U) / kBaselineSamples);
    } else if (m_phase == Phase::NOISE) {
        result.calibrationProgress = static_cast<uint8_t>(50U +
            (static_cast<uint32_t>(m_phaseSamples) * 50U) / kNoiseSamples);
    } else if (m_phase == Phase::READY) {
        result.calibrationProgress = 100;
    }
    return result;
}
