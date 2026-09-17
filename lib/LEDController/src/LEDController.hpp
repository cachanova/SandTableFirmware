#pragma once
#include <Arduino.h>
#include <atomic>

class LEDController {
public:
    LEDController(uint8_t pin = 2);
    void begin();
    void setBrightness(uint8_t brightness); // 0-255
    uint8_t getBrightness();

private:
    uint8_t m_pin;
    std::atomic<uint8_t> m_brightness;
    static constexpr int PWM_CHANNEL = 0;
    static constexpr int PWM_FREQ = 5000;
    static constexpr int PWM_RESOLUTION = 8; // 8-bit = 0-255
};
