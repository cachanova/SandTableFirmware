#include "PresenceSensor.hpp"

#include <Logger.hpp>
#include <Preferences.h>
#include <WiFi.h>
#include <algorithm>
#include <cstring>
#include <esp_err.h>
#include <esp_wifi.h>
#include <lwip/ip_addr.h>
#include <ping/ping_sock.h>

PresenceSensor::PresenceSensor() = default;

bool PresenceSensor::begin(const IPAddress& pingTarget) {
    loadAction();
    if (WiFi.status() != WL_CONNECTED) {
        LOG("Presence sensing unavailable: Wi-Fi is not connected\r\n");
        return false;
    }

    m_queue = xQueueCreate(16, sizeof(CsiSample));
    m_detectorMutex = xSemaphoreCreateMutex();
    if (m_queue == nullptr || m_detectorMutex == nullptr) {
        LOG("Presence sensing unavailable: queue or mutex allocation failed\r\n");
        return false;
    }

    updateAccessPoint();

    wifi_csi_config_t config{};
    config.lltf_en = true;
    config.htltf_en = false;
    config.stbc_htltf2_en = false;
    config.ltf_merge_en = false;
    config.channel_filter_en = false;
    config.manu_scale = false;
    config.shift = 0;

    esp_err_t error = esp_wifi_set_csi_rx_cb(&PresenceSensor::csiCallback, this);
    if (error == ESP_OK) error = esp_wifi_set_csi_config(&config);
    if (error == ESP_OK) error = esp_wifi_set_csi(true);
    if (error != ESP_OK) {
        esp_wifi_set_csi_rx_cb(nullptr, nullptr);
        LOG("Presence sensing unavailable: CSI setup failed (%s)\r\n",
            esp_err_to_name(error));
        return false;
    }

    m_available.store(true);
    m_suppressedUntilMs = millis() + kSettleMs;
    xSemaphoreTake(m_detectorMutex, portMAX_DELAY);
    m_detector.setSuppressed(true);
    xSemaphoreGive(m_detectorMutex);

    if (!startPing(pingTarget)) {
        LOG("Presence CSI enabled without ping keepalive; sampling may be sparse\r\n");
    }
    LOG("Presence CSI enabled; calibrate with the room empty after settling\r\n");
    return true;
}

void PresenceSensor::loadAction() {
    Preferences preferences;
    if (!preferences.begin("presence", false)) {
        LOG("No saved presence action; defaulting to fade_light_on\r\n");
        return;
    }
    const uint8_t stored = preferences.getUChar(
        "action", static_cast<uint8_t>(PresenceAction::FADE_LIGHT_ON));
    preferences.end();
    if (stored <= static_cast<uint8_t>(PresenceAction::FADE_LIGHT_ON)) {
        m_action.store(stored);
    } else {
        LOG("Ignoring invalid saved presence action %u\r\n",
            static_cast<unsigned>(stored));
    }
}

bool PresenceSensor::startPing(const IPAddress& target) {
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    IP_ADDR4(&config.target_addr, target[0], target[1], target[2], target[3]);
    config.count = ESP_PING_COUNT_INFINITE;
    config.interval_ms = 100;
    config.timeout_ms = 500;
    config.data_size = 32;
    config.task_prio = 1;

    esp_ping_callbacks_t callbacks{};
    callbacks.cb_args = this;
    esp_ping_handle_t handle = nullptr;
    if (esp_ping_new_session(&config, &callbacks, &handle) != ESP_OK) return false;
    if (esp_ping_start(handle) != ESP_OK) {
        esp_ping_delete_session(handle);
        return false;
    }
    m_pingHandle = handle;
    return true;
}

void PresenceSensor::updateAccessPoint() {
    wifi_ap_record_t accessPoint{};
    if (esp_wifi_sta_get_ap_info(&accessPoint) == ESP_OK) {
        std::memcpy(m_bssid, accessPoint.bssid, sizeof(m_bssid));
        m_haveBssid = true;
    }
}

void PresenceSensor::csiCallback(void* context, wifi_csi_info_t* info) {
    auto* sensor = static_cast<PresenceSensor*>(context);
    if (sensor == nullptr || info == nullptr || info->buf == nullptr ||
        sensor->m_queue == nullptr || !sensor->m_available.load()) {
        return;
    }

    CsiSample sample{};
    sample.atMs = millis();
    sample.rssi = info->rx_ctrl.rssi;
    sample.len = static_cast<uint16_t>(std::min<size_t>(info->len, kCsiBytes));
    std::memcpy(sample.source, info->mac, sizeof(sample.source));
    std::memcpy(sample.bytes, info->buf, sample.len);
    sensor->m_packets.fetch_add(1);
    if (xQueueSend(sensor->m_queue, &sample, 0) != pdTRUE) {
        sensor->m_dropped.fetch_add(1);
    }
}

bool PresenceSensor::processSample(const CsiSample& sample) {
    if (!m_haveBssid || std::memcmp(sample.source, m_bssid, sizeof(m_bssid)) != 0)
        return false;
    if (sample.len < 12 ||
        static_cast<uint32_t>(sample.atMs - m_lastAcceptedAtMs) <
            kMinimumSampleIntervalMs) {
        return false;
    }

    float powers[PresenceDetector::kMaxBins];
    size_t count = 0;
    // The first two complex LLTF bins are discarded on every packet because
    // older ESP32 revisions can mark those four bytes invalid.
    for (size_t i = 4; i + 1 < sample.len &&
         count < PresenceDetector::kMaxBins; i += 2) {
        const int real = sample.bytes[i + 1];
        const int imaginary = sample.bytes[i];
        powers[count++] = static_cast<float>(real * real + imaginary * imaginary);
    }
    if (count == 0) return false;

    m_lastAcceptedAtMs = sample.atMs;
    m_lastSampleMs.store(sample.atMs);
    m_lastRssi.store(sample.rssi);

    xSemaphoreTake(m_detectorMutex, portMAX_DELAY);
    const bool accepted = m_detector.addPowers(powers, count, sample.atMs);
    xSemaphoreGive(m_detectorMutex);
    return accepted;
}

void PresenceSensor::loop(bool mechanismMoving) {
    if (!m_available.load()) return;
    const uint32_t now = millis();

    if (static_cast<uint32_t>(now - m_lastApRefreshMs) >= 5000U) {
        updateAccessPoint();
        m_lastApRefreshMs = now;
    }

    if (mechanismMoving != m_mechanismMoving) {
        m_mechanismMoving = mechanismMoving;
        xSemaphoreTake(m_detectorMutex, portMAX_DELAY);
        m_detector.setSuppressed(true);
        xSemaphoreGive(m_detectorMutex);
        if (m_queue != nullptr) xQueueReset(m_queue);
        if (!mechanismMoving) m_suppressedUntilMs = now + kSettleMs;
    }

    if (!m_mechanismMoving &&
        static_cast<int32_t>(now - m_suppressedUntilMs) >= 0) {
        xSemaphoreTake(m_detectorMutex, portMAX_DELAY);
        m_detector.setSuppressed(false);
        xSemaphoreGive(m_detectorMutex);
    }

    CsiSample sample{};
    for (uint8_t processed = 0; processed < 8 &&
         xQueueReceive(m_queue, &sample, 0) == pdTRUE; ++processed) {
        processSample(sample);
    }
}

bool PresenceSensor::startCalibration() {
    if (!m_available.load() || m_detectorMutex == nullptr) return false;
    xSemaphoreTake(m_detectorMutex, portMAX_DELAY);
    const bool started = m_detector.startCalibration();
    xSemaphoreGive(m_detectorMutex);
    return started;
}

PresenceAction PresenceSensor::getAction() const {
    return static_cast<PresenceAction>(m_action.load());
}

bool PresenceSensor::setAction(PresenceAction action) {
    const uint8_t value = static_cast<uint8_t>(action);
    if (value > static_cast<uint8_t>(PresenceAction::FADE_LIGHT_ON)) {
        return false;
    }
    Preferences preferences;
    if (!preferences.begin("presence", false)) {
        LOG("Failed to open presence settings storage\r\n");
        return false;
    }
    const size_t written = preferences.putUChar("action", value);
    preferences.end();
    if (written != sizeof(uint8_t)) {
        LOG("Failed to save presence action\r\n");
        return false;
    }
    m_action.store(value);
    LOG("Presence action saved: %s\r\n",
        action == PresenceAction::FADE_LIGHT_ON ? "fade_light_on" : "none");
    return true;
}

PresenceStatus PresenceSensor::getStatus() const {
    PresenceStatus result;
    const uint32_t now = millis();
    result.available = m_available.load();
    result.packets = m_packets.load();
    result.dropped = m_dropped.load();
    result.rssi = m_lastRssi.load();
    const uint32_t lastSample = m_lastSampleMs.load();
    result.sampleAgeMs = lastSample == 0
        ? -1 : static_cast<int32_t>(now - lastSample);
    result.receiving = result.sampleAgeMs >= 0 &&
        static_cast<uint32_t>(result.sampleAgeMs) <= kSampleFreshMs;

    if (m_detectorMutex == nullptr) return result;
    xSemaphoreTake(m_detectorMutex, portMAX_DELAY);
    const PresenceDetector::Status detector = m_detector.status(now);
    xSemaphoreGive(m_detectorMutex);
    result.calibrated = detector.phase == PresenceDetector::Phase::READY;
    result.calibrating = detector.phase == PresenceDetector::Phase::BASELINE ||
        detector.phase == PresenceDetector::Phase::NOISE;
    result.suppressed = detector.suppressed;
    result.motion = detector.motion;
    result.occupied = detector.occupied;
    result.calibrationProgress = detector.calibrationProgress;
    result.score = detector.score;
    result.threshold = detector.threshold;
    result.acceptedSamples = detector.acceptedSamples;
    result.lastMotionMs = detector.lastMotionMs;
    return result;
}
