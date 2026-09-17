#include "LEDController.hpp"
#include "Logger.hpp"
#include <driver/gpio.h>
#include <esp_arduino_version.h>
#if ESP_ARDUINO_VERSION_MAJOR < 3
#include <driver/ledc.h>
#endif

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
    if (ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION) == 0) {
        LOG("ERROR: Failed to configure LED PWM\r\n");
        return;
    }
    ledcAttachPin(m_pin, PWM_CHANNEL);
#endif

    // Set the pull after LEDC attachment so peripheral setup cannot clear it.
    if (gpio_set_pull_mode(static_cast<gpio_num_t>(m_pin), GPIO_PULLDOWN_ONLY) != ESP_OK) {
        LOG("ERROR: Failed to enable LED pull-down on GPIO %d\r\n", m_pin);
        return;
    }

    m_diagnostics.ready = true;
    if (!setOutputBrightness(m_brightness.load())) {
        m_diagnostics.ready = false;
        LOG("ERROR: Failed to set initial LED brightness\r\n");
        return;
    }

    LOG("LED Controller initialized on GPIO %d with brightness %d, internal pull-down enabled\r\n", m_pin, m_brightness.load());
}

bool LEDController::setBrightness(uint8_t brightness) {
    if (!setOutputBrightness(brightness)) return false;
    m_targetBrightness.store(brightness);
    return true;
}

bool LEDController::setOutputBrightness(uint8_t brightness) {
    const uint32_t startedUs = micros();
    bool success = false;
    if (m_diagnostics.ready) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        success = ledcWrite(m_pin, brightness);
#else
        // Arduino 2's void ledcWrite discards driver errors. Keep its full-on
        // mapping, but check both operations before publishing applied brightness.
        const uint32_t duty = brightness == 255 ? 256 : brightness;
        success = ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, duty) == ESP_OK &&
                  ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0) == ESP_OK;
#endif
    }
    m_diagnostics.lastWriteUs = micros() - startedUs;
    if (m_diagnostics.lastWriteUs > m_diagnostics.maxWriteUs)
        m_diagnostics.maxWriteUs = m_diagnostics.lastWriteUs;
    if (!success) {
        ++m_diagnostics.errors;
        return false;
    }
    m_brightness.store(brightness);
    ++m_diagnostics.writes;
    return true;
}

LEDController::Diagnostics LEDController::getDiagnostics() const {
    Diagnostics result = m_diagnostics;
    if (result.ready) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        result.duty = ledcRead(m_pin);
        result.frequencyHz = ledcReadFreq(m_pin);
#else
        result.duty = ledcRead(PWM_CHANNEL);
        result.frequencyHz = ledc_get_freq(LEDC_HIGH_SPEED_MODE, LEDC_TIMER_0);
#endif
    }
    return result;
}

uint8_t LEDController::getBrightness() {
    return m_brightness;
}
