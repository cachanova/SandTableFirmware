#include "PresenceSensor.hpp"
#include <CsiFeatures.hpp>

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

bool PresenceSensor::begin() {
    if (m_available.load()) return true;
    loadAction();
    if (WiFi.status() != WL_CONNECTED) {
        LOG("Presence sensing unavailable: Wi-Fi is not connected\r\n");
        return false;
    }

    m_queue = xQueueCreate(16, sizeof(CsiSample));
    m_detectorMutex = xSemaphoreCreateMutex();
    if (m_queue == nullptr || m_detectorMutex == nullptr) {
        if (m_queue) vQueueDelete(m_queue);
        if (m_detectorMutex) vSemaphoreDelete(m_detectorMutex);
        m_queue = nullptr;
        m_detectorMutex = nullptr;
        LOG("Presence sensing unavailable: queue or mutex allocation failed\r\n");
        return false;
    }

    wifi_csi_config_t config{};
    config.lltf_en = true;
    config.htltf_en = false;
    config.stbc_htltf2_en = false;
    config.ltf_merge_en = false;
    config.channel_filter_en = false;
    config.manu_scale = false;
    config.shift = 0;

    // Keep a single subcarrier layout; HT40 LLTF indexing is different.
    esp_err_t error = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
    if (error == ESP_OK)
        error = esp_wifi_set_csi_rx_cb(&PresenceSensor::csiCallback, this);
    if (error == ESP_OK) error = esp_wifi_set_csi_config(&config);
    if (error == ESP_OK) error = esp_wifi_set_csi(true);
    if (error != ESP_OK) {
        esp_wifi_set_csi_rx_cb(nullptr, nullptr);
        vQueueDelete(m_queue);
        vSemaphoreDelete(m_detectorMutex);
        m_queue = nullptr;
        m_detectorMutex = nullptr;
        LOG("Presence sensing unavailable: CSI setup failed (%s)\r\n",
            esp_err_to_name(error));
        return false;
    }

    m_suppressedUntilMs = millis() + kSettleMs;
    xSemaphoreTake(m_detectorMutex, portMAX_DELAY);
    m_detector.setSuppressed(true);
    updateAccessPoint();
    xSemaphoreGive(m_detectorMutex);
    m_available.store(true);
    LOG("Presence CSI enabled; calibrate with the room empty after settling\r\n");
    return true;
}

void PresenceSensor::loadAction() {
    Preferences preferences;
    if (!preferences.begin("presence", true)) {
        LOG("No saved presence action; defaulting to fade_light_on\r\n");
        return;
    }
    const uint8_t stored = preferences.isKey("action")
        ? preferences.getUChar("action", static_cast<uint8_t>(PresenceAction::NONE))
        : static_cast<uint8_t>(PresenceAction::FADE_LIGHT_ON);
    preferences.end();
    if (stored <= static_cast<uint8_t>(PresenceAction::FADE_LIGHT_ON)) {
        m_action.store(stored);
    } else {
        m_action.store(static_cast<uint8_t>(PresenceAction::NONE));
        LOG("Ignoring invalid saved presence action %u\r\n",
            static_cast<unsigned>(stored));
    }
}

bool PresenceSensor::startPing(const IPAddress& target) {
    if (target == IPAddress()) return false;
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

void PresenceSensor::stopPing() {
    if (!m_pingHandle) return;
    esp_ping_stop(m_pingHandle);
    esp_ping_delete_session(m_pingHandle);
    m_pingHandle = nullptr;
}

void PresenceSensor::updateAccessPoint() {
    wifi_ap_record_t accessPoint{};
    const bool connected = WiFi.status() == WL_CONNECTED &&
        esp_wifi_sta_get_ap_info(&accessPoint) == ESP_OK;
    const IPAddress gateway = WiFi.gatewayIP();
    if (connected == m_haveBssid && (!connected ||
        (std::memcmp(m_bssid, accessPoint.bssid, sizeof(m_bssid)) == 0 &&
         m_channel == accessPoint.primary && gateway == m_pingTarget))) return;

    // Calibration is specific to this AP/channel. Reconnection must never
    // revive a previously latched detection or consume queued old frames.
    m_detector.invalidate();
    m_haveSample = false;
    m_sampleBoundaryMs = millis();
    xQueueReset(m_queue);
    m_haveBssid = connected;
    stopPing();
    if (!connected) return;
    std::memcpy(m_bssid, accessPoint.bssid, sizeof(m_bssid));
    m_channel = accessPoint.primary;
    m_pingTarget = gateway;
    if (!startPing(m_pingTarget)) {
        LOG("Presence CSI enabled without ping keepalive; sampling may be sparse\r\n");
    }
}

void PresenceSensor::csiCallback(void* context, wifi_csi_info_t* info) {
    auto* sensor = static_cast<PresenceSensor*>(context);
    if (sensor == nullptr || info == nullptr || info->buf == nullptr ||
        !sensor->m_available.load() || sensor->m_queue == nullptr) {
        return;
    }
    sensor->m_packets.fetch_add(1);
    if (info->len != kCsiBytes || info->rx_ctrl.rx_state != 0 ||
        info->rx_ctrl.cwb != 0 || info->rx_ctrl.secondary_channel != 0 ||
        info->rx_ctrl.sig_mode > 1) return;

    CsiSample sample{};
    sample.atMs = millis();
    sample.rssi = info->rx_ctrl.rssi;
    sample.channel = info->rx_ctrl.channel;
    sample.len = static_cast<uint16_t>(std::min<size_t>(info->len, kCsiBytes));
    std::memcpy(sample.source, info->mac, sizeof(sample.source));
    std::memcpy(sample.bytes, info->buf, sample.len);
    if (xQueueSend(sensor->m_queue, &sample, 0) != pdTRUE) {
        sensor->m_dropped.fetch_add(1);
    }
}

bool PresenceSensor::processSample(const CsiSample& sample) {
    if (!m_haveBssid || sample.channel != m_channel ||
        std::memcmp(sample.source, m_bssid, sizeof(m_bssid)) != 0)
        return false;
    const uint32_t now = millis();
    if (now - sample.atMs > 250U ||
        (now - m_sampleBoundaryMs <= 250U &&
         static_cast<int32_t>(sample.atMs - m_sampleBoundaryMs) <= 0) ||
        (m_haveSample && sample.atMs - m_lastAcceptedAtMs < kMinimumSampleIntervalMs)) {
        return false;
    }

    float powers[CsiFeatures::kBins];
    if (!CsiFeatures::extract(sample.bytes, sample.len, powers)) return false;

    const bool suppressed = m_detector.status(sample.atMs).suppressed;
    const bool accepted = !suppressed &&
        m_detector.addPowers(powers, CsiFeatures::kBins, sample.atMs);
    if (!suppressed && !accepted) return false;

    m_lastAcceptedAtMs = sample.atMs;
    m_lastSampleMs = sample.atMs;
    m_lastRssi = sample.rssi;
    m_haveSample = true;
    return accepted;
}

void PresenceSensor::loop(bool mechanismMoving) {
    if (!m_available.load()) return;
    const uint32_t now = millis();
    xSemaphoreTake(m_detectorMutex, portMAX_DELAY);

    if ((m_haveBssid && WiFi.status() != WL_CONNECTED) ||
        static_cast<uint32_t>(now - m_lastApRefreshMs) >= 1000U) {
        updateAccessPoint();
        m_lastApRefreshMs = now;
    }

    if (mechanismMoving != m_mechanismMoving) {
        m_mechanismMoving = mechanismMoving;
        m_detector.setSuppressed(true);
        m_sampleBoundaryMs = now;
        if (m_queue != nullptr) xQueueReset(m_queue);
        if (!mechanismMoving) {
            m_suppressedUntilMs = now + kSettleMs;
            m_settling = true;
        }
    }

    if (!m_mechanismMoving && m_settling &&
        static_cast<int32_t>(now - m_suppressedUntilMs) >= 0) {
        m_detector.setSuppressed(false);
        m_settling = false;
        m_sampleBoundaryMs = now;
        xQueueReset(m_queue);
    }

    CsiSample sample{};
    for (uint8_t processed = 0; processed < 8 &&
         xQueueReceive(m_queue, &sample, 0) == pdTRUE; ++processed) {
        processSample(sample);
    }
    m_detector.tick(millis());
    xSemaphoreGive(m_detectorMutex);
}

bool PresenceSensor::startCalibration() {
    if (!m_available.load() || m_detectorMutex == nullptr) return false;
    xSemaphoreTake(m_detectorMutex, portMAX_DELAY);
    const uint32_t now = millis();
    const bool started = m_haveBssid && WiFi.status() == WL_CONNECTED &&
        m_haveSample && now - m_lastSampleMs <= kSampleFreshMs &&
        m_detector.startCalibration(now);
    if (started) {
        m_sampleBoundaryMs = now;
        xQueueReset(m_queue);
    }
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
    result.available = m_available.load();
    result.packets = m_packets.load();
    result.dropped = m_dropped.load();
    if (m_detectorMutex == nullptr) return result;
    xSemaphoreTake(m_detectorMutex, portMAX_DELAY);
    const uint32_t now = millis();
    result.rssi = m_lastRssi;
    result.sampleAgeMs = !m_haveSample
        ? -1 : static_cast<int32_t>(std::min<uint32_t>(now - m_lastSampleMs, INT32_MAX));
    result.receiving = result.sampleAgeMs >= 0 &&
        static_cast<uint32_t>(result.sampleAgeMs) <= kSampleFreshMs;

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
