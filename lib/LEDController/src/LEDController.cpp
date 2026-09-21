#include "LEDController.hpp"
#include "Logger.hpp"
#include <driver/gpio.h>

void LEDController::begin() {
    m_ready.store(false);
    const auto pin = static_cast<gpio_num_t>(m_pin);
    // Set the output latch low before enabling the driver to avoid a startup flash.
    if (gpio_set_level(pin, 0) != ESP_OK) return;
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << m_pin;
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_ENABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&config) != ESP_OK) {
        LOG("ERROR: Failed to configure light GPIO %d\r\n", m_pin);
        return;
    }
    m_on.store(false);
    m_targetOn.store(false);
    m_ready.store(true);
    LOG("Light initialized off on GPIO %d, internal pull-down enabled\r\n", m_pin);
}

bool LEDController::setOutputOn(bool on) {
    if (!m_ready.load()) return false;
    if (on == m_on.load()) return true;
    if (gpio_set_level(static_cast<gpio_num_t>(m_pin), on ? 1 : 0) != ESP_OK)
        return false;
    m_on.store(on);
    return true;
}

bool LEDController::setOn(bool on) {
    if (!setOutputOn(on)) return false;
    m_targetOn.store(on);
    return true;
}
