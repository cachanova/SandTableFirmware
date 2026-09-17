#pragma once

#include <Arduino.h>
#include <PresenceAutomation.hpp>
#include <PresenceDetector.hpp>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_wifi.h>

struct PresenceStatus {
    bool available = false;
    bool receiving = false;
    bool calibrated = false;
    bool calibrating = false;
    bool suppressed = false;
    bool motion = false;
    bool occupied = false;
    uint8_t calibrationProgress = 0;
    float score = 0.0f;
    float threshold = 0.0f;
    int8_t rssi = 0;
    uint32_t packets = 0;
    uint32_t dropped = 0;
    uint32_t acceptedSamples = 0;
    int32_t sampleAgeMs = -1;
    uint32_t lastMotionMs = 0;
};

class PresenceSensor {
public:
    PresenceSensor();

    bool begin(const IPAddress& pingTarget);
    void loop(bool mechanismMoving);
    bool startCalibration();
    PresenceAction getAction() const;
    bool setAction(PresenceAction action);
    PresenceStatus getStatus() const;

private:
    static constexpr size_t kCsiBytes = 128;
    static constexpr uint32_t kSettleMs = 5000;
    static constexpr uint32_t kSampleFreshMs = 3000;
    static constexpr uint32_t kMinimumSampleIntervalMs = 50;

    struct CsiSample {
        uint32_t atMs;
        uint16_t len;
        int8_t rssi;
        uint8_t source[6];
        int8_t bytes[kCsiBytes];
    };

    static void csiCallback(void* context, wifi_csi_info_t* info);
    bool startPing(const IPAddress& target);
    void loadAction();
    void updateAccessPoint();
    bool processSample(const CsiSample& sample);

    QueueHandle_t m_queue = nullptr;
    SemaphoreHandle_t m_detectorMutex = nullptr;
    PresenceDetector m_detector;
    void* m_pingHandle = nullptr;
    std::atomic<bool> m_available{false};
    std::atomic<uint8_t> m_action{
        static_cast<uint8_t>(PresenceAction::FADE_LIGHT_ON)};
    std::atomic<uint32_t> m_packets{0};
    std::atomic<uint32_t> m_dropped{0};
    std::atomic<uint32_t> m_lastSampleMs{0};
    std::atomic<int8_t> m_lastRssi{0};
    uint8_t m_bssid[6]{};
    bool m_haveBssid = false;
    bool m_mechanismMoving = false;
    uint32_t m_suppressedUntilMs = 0;
    uint32_t m_lastAcceptedAtMs = 0;
    uint32_t m_lastApRefreshMs = 0;
};
