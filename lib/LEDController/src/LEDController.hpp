#pragma once
#include <Arduino.h>
#include <atomic>

class LEDController {
public:
    LEDController(uint8_t pin = 4);
    void begin();
    void setBrightness(uint8_t brightness); // User-selected target and output, 0-255
    void setOutputBrightness(uint8_t brightness); // Fade output; never changes target
    uint8_t getBrightness();
    uint8_t getTargetBrightness() const { return m_targetBrightness.load(); }

private:
    uint8_t m_pin;
    std::atomic<uint8_t> m_brightness;
    std::atomic<uint8_t> m_targetBrightness{128};
    static constexpr int PWM_CHANNEL = 0;
    static constexpr int PWM_FREQ = 5000;
    static constexpr int PWM_RESOLUTION = 8; // 8-bit = 0-255
};
