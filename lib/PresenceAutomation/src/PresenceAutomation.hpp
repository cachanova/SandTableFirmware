#pragma once

#include <cstdint>

enum class PresenceAction : uint8_t {
    NONE = 0,
    FADE_LIGHT_ON = 1
};

class PresenceAutomation {
public:
    static constexpr uint32_t kFadeDurationMs = 2000;
    static constexpr uint8_t kFadeTarget = 255;

    void setAction(PresenceAction action);
    void cancelFade();

    // Returns true when brightness should be written to the LED controller.
    bool update(bool movementDetected, uint8_t currentBrightness,
                uint32_t nowMs, uint8_t& nextBrightness);

    bool isFading() const { return m_fading; }

private:
    PresenceAction m_action = PresenceAction::NONE;
    bool m_previousMovement = false;
    bool m_fading = false;
    uint8_t m_fadeStart = 0;
    uint32_t m_fadeStartedAtMs = 0;
};
