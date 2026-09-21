#pragma once
#include <Arduino.h>
#include <atomic>

class LEDController {
public:
    explicit LEDController(uint8_t pin = 4) : m_pin(pin) {}
    void begin();
    bool setOn(bool on);
    bool setOutputOn(bool on); // Presence restoration does not change the manual selection.
    bool isOn() const { return m_on.load(); }
    bool targetOn() const { return m_targetOn.load(); }
    bool isReady() const { return m_ready.load(); }
    uint8_t getPin() const { return m_pin; }

private:
    const uint8_t m_pin;
    std::atomic<bool> m_ready{false};
    std::atomic<bool> m_on{false};
    std::atomic<bool> m_targetOn{false};
};
