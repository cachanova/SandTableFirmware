#include "LEDController.hpp"
#include "Logger.hpp"
#include <driver/gpio.h>
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

    // Set the pull after LEDC attachment so peripheral setup cannot clear it.
    if (gpio_set_pull_mode(static_cast<gpio_num_t>(m_pin), GPIO_PULLDOWN_ONLY) != ESP_OK) {
        LOG("ERROR: Failed to enable LED pull-down on GPIO %d\r\n", m_pin);
        return;
    }

    // Set initial brightness
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(m_pin, m_brightness);
#else
    ledcWrite(PWM_CHANNEL, m_brightness);
#endif

    LOG("LED Controller initialized on GPIO %d with brightness %d, internal pull-down enabled\r\n", m_pin, m_brightness.load());
}

void LEDController::setBrightness(uint8_t brightness) {
    m_targetBrightness.store(brightness);
    setOutputBrightness(brightness);
}

void LEDController::setOutputBrightness(uint8_t brightness) {
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
