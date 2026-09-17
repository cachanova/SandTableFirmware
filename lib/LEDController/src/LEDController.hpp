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
    static constexpr uint32_t kFadeDurationMs = 1000;
    LEDController(uint8_t pin = 4);
    void begin();
    bool setBrightness(uint8_t brightness, uint32_t nowMs); // User target, 0-255; output fades via update()
    bool setOutputBrightness(uint8_t brightness); // Fade output; never changes target
    void update(uint32_t nowMs); // Tick fade; writes PWM only when the 8-bit value changes
    bool isFading() const { return m_fading; }
    uint8_t getBrightness();
    uint8_t getTargetBrightness() const { return m_targetBrightness.load(); }
    uint8_t getPin() const { return m_pin; }
    // Serialize writes and diagnostics with the web server's LED-only mutex.
    Diagnostics getDiagnostics() const;

private:
    uint8_t m_pin;
    std::atomic<uint8_t> m_brightness;
    std::atomic<uint8_t> m_targetBrightness{128};
    bool m_fading = false;
    uint8_t m_fadeStart = 0;
    uint32_t m_fadeStartedAtMs = 0;
    Diagnostics m_diagnostics;
    static constexpr int PWM_CHANNEL = 0;
    static constexpr int PWM_FREQ = 5000;
    static constexpr int PWM_RESOLUTION = 8; // 8-bit = 0-255
};
