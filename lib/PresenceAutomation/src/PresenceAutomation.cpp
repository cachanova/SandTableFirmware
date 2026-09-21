#include "PresenceAutomation.hpp"

void PresenceAutomation::setAction(PresenceAction action) {
    if (m_action == action) return;
    m_action = action;
    if (action == PresenceAction::NONE) manualOverride();
}

void PresenceAutomation::manualOverride() {
    // Consume an active event so it cannot immediately undo a manual selection.
    m_previousMovement = true;
}

bool PresenceAutomation::update(bool movementDetected, bool currentOn,
                                bool targetOn, bool& nextOn) {
    const bool movementStarted = movementDetected && !m_previousMovement;
    m_previousMovement = movementDetected;
    if (!movementStarted || m_action != PresenceAction::RESTORE_LIGHT ||
        currentOn || !targetOn) return false;
    nextOn = true;
    return true;
}
