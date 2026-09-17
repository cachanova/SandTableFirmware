#include "LEDController.hpp"
#include "Logger.hpp"
#include <esp_arduino_version.h>

LEDController::LEDController(uint8_t pin) : m_pin(pin), m_brightness(128) {
}

void LEDController::begin() {
    // Configure LEDC peripheral for PWM control
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    if (!ledcAttachChannel(m_pin, PWM_FREQ, PWM_RESOLUTION, PWM_CHANNEL)) {
        LOG("ERROR: Failed to attach LED PWM on GPIO %d\r\n", m_pin);
        return;
    }
#else
    ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(m_pin, PWM_CHANNEL);
#endif

    // Set initial brightness
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(m_pin, m_brightness);
#else
    ledcWrite(PWM_CHANNEL, m_brightness);
#endif

    LOG("LED Controller initialized on GPIO %d with brightness %d\r\n", m_pin, m_brightness.load());
}

void LEDController::setBrightness(uint8_t brightness) {
    m_brightness = brightness;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(m_pin, m_brightness);
#else
    ledcWrite(PWM_CHANNEL, m_brightness);
#endif
}

uint8_t LEDController::getBrightness() {
    return m_brightness;
}
