#pragma once
#include <cstdint>

// Fixed budgets for optional sensing; never compete with control/network work
// for the last heap blocks. Hysteresis prevents repeated stop/start at the edge.
namespace PresenceBudget {
constexpr uint32_t kSampleIntervalMs = 50;
constexpr uint32_t kQueueDepth = 4;
constexpr uint32_t kPauseFreeBytes = 16 * 1024;
constexpr uint32_t kResumeFreeBytes = 24 * 1024;
constexpr uint32_t kPingLargestBlockBytes = 4096;

inline bool memoryLimited(bool paused, uint32_t freeBytes, uint32_t largestBlock) {
    return freeBytes < (paused ? kResumeFreeBytes : kPauseFreeBytes) ||
        largestBlock < kPingLargestBlockBytes;
}

// Owned exclusively by the Wi-Fi callback. Apply before copying/enqueueing,
// so an HTTP/image burst cannot turn the callback into an unbounded producer.
template<uint32_t IntervalMs>
class RateLimit {
public:
    bool accept(uint32_t nowMs) {
        if (m_started && nowMs - m_lastMs < IntervalMs) return false;
        m_started = true;
        m_lastMs = nowMs;
        return true;
    }
private:
    bool m_started = false;
    uint32_t m_lastMs = 0;
};
using CaptureRateLimit = RateLimit<kSampleIntervalMs>;
using PingRateLimit = RateLimit<100>;
}
