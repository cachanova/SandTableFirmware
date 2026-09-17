#include "PresenceAutomation.hpp"

void PresenceAutomation::setAction(PresenceAction action) {
    if (m_action == action) return;
    m_action = action;
    if (m_action == PresenceAction::NONE) cancelFade();
}

void PresenceAutomation::cancelFade() {
    m_fading = false;
    // A manual override also consumes any as-yet-unobserved active event.
    m_previousMovement = true;
}

bool PresenceAutomation::update(bool movementDetected,
                                uint8_t currentBrightness,
                                uint8_t targetBrightness,
                                uint32_t nowMs,
                                uint8_t& nextBrightness) {
    // A changed user target cancels an in-flight fade; never finish at an old
    // higher target or underflow the unsigned interpolation range.
    if (m_fading && targetBrightness != m_fadeTarget) cancelFade();
    const bool movementStarted = movementDetected && !m_previousMovement;
    m_previousMovement = movementDetected;

    if (movementStarted && !m_fading && m_action == PresenceAction::FADE_LIGHT_ON &&
        currentBrightness < targetBrightness) {
        m_fading = true;
        m_fadeStart = currentBrightness;
        m_fadeTarget = targetBrightness;
        m_fadeStartedAtMs = nowMs;
    }

    if (!m_fading) return false;

    const uint32_t elapsed = nowMs - m_fadeStartedAtMs;
    if (elapsed >= kFadeDurationMs) {
        m_fading = false;
        nextBrightness = m_fadeTarget;
        return currentBrightness != nextBrightness;
    }

    const uint32_t range = static_cast<uint32_t>(m_fadeTarget - m_fadeStart);
    nextBrightness = static_cast<uint8_t>(m_fadeStart +
        (range * elapsed) / kFadeDurationMs);
    return currentBrightness != nextBrightness;
}
