#pragma once
#include <Arduino.h>
#include <atomic>

class LEDController {
public:
    struct Diagnostics {
        bool ready = false;
        uint32_t duty = 0;
        uint32_t frequencyHz = 0;
        uint32_t writes = 0;
        uint32_t errors = 0;
        uint32_t lastWriteUs = 0;
        uint32_t maxWriteUs = 0;
    };
    LEDController(uint8_t pin = 4);
    void begin();
    bool setBrightness(uint8_t brightness); // User-selected target and output, 0-255
    bool setOutputBrightness(uint8_t brightness); // Fade output; never changes target
    uint8_t getBrightness();
    uint8_t getTargetBrightness() const { return m_targetBrightness.load(); }
    uint8_t getPin() const { return m_pin; }
    // Serialize writes and diagnostics with the web server's LED-only mutex.
    Diagnostics getDiagnostics() const;

private:
    uint8_t m_pin;
    std::atomic<uint8_t> m_brightness;
    std::atomic<uint8_t> m_targetBrightness{128};
    Diagnostics m_diagnostics;
    static constexpr int PWM_CHANNEL = 0;
    static constexpr int PWM_FREQ = 5000;
    static constexpr int PWM_RESOLUTION = 8; // 8-bit = 0-255
};
