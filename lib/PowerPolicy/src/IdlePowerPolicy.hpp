#pragma once

#include <stdint.h>

// Keeps the controller at full speed during motion and while an HTTP request
// is being served. The grace period also covers short gaps between playlist
// stages and lets a browser finish its initial burst of requests.
class IdlePowerPolicy {
public:
    explicit IdlePowerPolicy(uint32_t nowMs, uint32_t settleMs = 5000)
        : m_lastActivityMs(nowMs), m_settleMs(settleMs) {}

    bool fullPower(bool motionBusy, uint32_t requestTotal,
                   uint32_t requestInflight, uint32_t nowMs) {
        if (motionBusy || requestInflight != 0 ||
            requestTotal != m_lastRequestTotal) {
            m_lastActivityMs = nowMs;
        }
        m_lastRequestTotal = requestTotal;
        return static_cast<uint32_t>(nowMs - m_lastActivityMs) < m_settleMs;
    }

private:
    uint32_t m_lastActivityMs;
    uint32_t m_lastRequestTotal = 0;
    uint32_t m_settleMs;
};
