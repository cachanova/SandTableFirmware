// Host stress test; elapsed time is NOT an ESP32 timing measurement.
#include <PresenceBudget.hpp>
#include <PresenceDetector.hpp>
#include <PresenceAutomation.hpp>
#include <CsiFeatures.hpp>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>

static bool forbidAllocation = false;
static size_t allocations = 0;
void* operator new(size_t size) {
    assert(!forbidAllocation);
    ++allocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

int main() {
    using namespace PresenceBudget;
    assert(memoryLimited(false, kPauseFreeBytes - 1, 8192));
    assert(!memoryLimited(false, kPauseFreeBytes, 8192));
    assert(memoryLimited(true, kResumeFreeBytes - 1, 8192));
    assert(!memoryLimited(true, kResumeFreeBytes, 8192));
    assert(memoryLimited(false, 50000, kPingLargestBlockBytes - 1));
    assert(!memoryLimited(true, 50000, kPingLargestBlockBytes));

    CaptureRateLimit rate;
    uint32_t accepted = 0;
    // 100,000 callbacks/sec for a simulated minute: exactly 20 copies/sec.
    for (uint32_t ms = 0; ms < 60000; ++ms)
        for (unsigned burst = 0; burst < 100; ++burst)
            accepted += rate.accept(ms);
    assert(accepted == 1200);
    CaptureRateLimit wrapping;
    assert(wrapping.accept(UINT32_MAX - 24));
    assert(!wrapping.accept(24));
    assert(wrapping.accept(25));
    PingRateLimit ping;
    assert(ping.accept(0));
    assert(!ping.accept(99));
    assert(ping.accept(100));
    assert(ping.accept(60000)); // delayed batch/reconnect must not catch up
    for (uint32_t ms = 60000; ms < 60100; ++ms) assert(!ping.accept(ms));
    assert(ping.accept(60100));

    PresenceDetector detector;
    PresenceAutomation automation;
    int8_t bytes[CsiFeatures::kBytes]{};
    float powers[CsiFeatures::kBins]{};
    for (auto& byte : bytes) byte = 10;
    uint32_t now = 0;
    uint8_t brightness = 0;
    const size_t before = allocations;
    const auto start = std::chrono::steady_clock::now();
    forbidAllocation = true;
    for (unsigned cycle = 0; cycle < 1000; ++cycle) {
        detector.invalidate();
        assert(detector.startCalibration(now));
        for (unsigned sample = 0; sample < 1000; ++sample) {
            now += 50;
            assert(CsiFeatures::extract(bytes, sizeof(bytes), powers));
            assert(detector.addPowers(powers, CsiFeatures::kBins, now));
            const auto status = detector.status(now);
            automation.update(status.motion, brightness, now, brightness);
        }
        detector.setSuppressed(true);
        detector.setSuppressed(false);
    }
    forbidAllocation = false;
    assert(allocations == before);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << "PASS: heap hysteresis, fragmented-heap guard, 6M callback gates, "
              << "clock wrap, 1M frames/1000 calibrations without C++ heap allocation; "
              << elapsed << " ms host time\n";
}
