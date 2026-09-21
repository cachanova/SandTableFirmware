#pragma once
#include <cstdint>

enum class PresenceAction : uint8_t {
    NONE = 0,
    RESTORE_LIGHT = 1 // Keep the stored value of the former fade action.
};

class PresenceAutomation {
public:
    void setAction(PresenceAction action);
    void manualOverride();
    bool update(bool movementDetected, bool currentOn, bool targetOn, bool& nextOn);

private:
    PresenceAction m_action = PresenceAction::NONE;
    bool m_previousMovement = false;
};
