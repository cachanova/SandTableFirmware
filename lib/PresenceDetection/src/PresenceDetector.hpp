#pragma once

#include <cstddef>
#include <cstdint>

class PresenceDetector {
public:
    static constexpr size_t kMaxBins = 64;
    static constexpr uint16_t kBaselineSamples = 80;
    static constexpr uint16_t kNoiseSamples = 80;
    static constexpr uint32_t kOccupancyHoldMs = 60000;

    enum class Phase : uint8_t {
        UNCALIBRATED,
        BASELINE,
        NOISE,
        READY
    };

    struct Status {
        Phase phase = Phase::UNCALIBRATED;
        bool suppressed = false;
        bool motion = false;
        bool occupied = false;
        uint8_t calibrationProgress = 0;
        float score = 0.0f;
        float threshold = 0.0f;
        uint32_t lastMotionMs = 0;
        uint32_t acceptedSamples = 0;
    };

    PresenceDetector();

    bool startCalibration();
    void setSuppressed(bool suppressed);
    bool addPowers(const float* powers, size_t count, uint32_t nowMs);
    Status status(uint32_t nowMs) const;

private:
    static constexpr float kMinimumThreshold = 0.02f;
    static constexpr float kBaselineAdaptation = 0.001f;
    static constexpr uint8_t kMotionEnterSamples = 3;
    static constexpr uint8_t kMotionExitSamples = 8;

    bool normalize(const float* powers, size_t count, float* normalized) const;
    float distanceFromBaseline(const float* normalized) const;
    void resetRuntime();

    Phase m_phase = Phase::UNCALIBRATED;
    bool m_suppressed = false;
    bool m_motion = false;
    bool m_everDetectedMotion = false;
    size_t m_binCount = 0;
    float m_baseline[kMaxBins]{};
    uint16_t m_phaseSamples = 0;
    float m_noiseMean = 0.0f;
    float m_noiseM2 = 0.0f;
    float m_threshold = 0.0f;
    float m_smoothedDistance = 0.0f;
    bool m_hasSmoothedDistance = false;
    uint8_t m_enterCount = 0;
    uint8_t m_exitCount = 0;
    uint32_t m_lastMotionMs = 0;
    uint32_t m_acceptedSamples = 0;
};
