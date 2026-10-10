#include "PositionMemory.hpp"
#include "RhoPositionTracker.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

using PositionMemory::Record;

static const double kMaxRho = 425.0;

static bool roundTrip(const Record& in, Record& out) {
    uint8_t bytes[PositionMemory::kEncodedSize];
    PositionMemory::encode(in, bytes);
    return PositionMemory::decode(bytes, sizeof(bytes), kMaxRho, out);
}

int main() {
    // A settled position survives a round trip exactly.
    {
        Record out{};
        assert(roundTrip({true, 123.456789, 200.25}, out));
        assert(out.valid && out.theta == 123.456789 && out.rho == 200.25);
    }
    // A "moving" record decodes but reports the position as untrusted.
    {
        Record out{};
        assert(roundTrip({false, 1.0, 2.0}, out));
        assert(!out.valid);
    }
    // Wrong length (truncated or oversized) is rejected.
    {
        uint8_t bytes[PositionMemory::kEncodedSize + 1];
        PositionMemory::encode({true, 0.0, 0.0}, bytes);
        Record out{};
        assert(!PositionMemory::decode(bytes, PositionMemory::kEncodedSize - 1, kMaxRho, out));
        assert(!PositionMemory::decode(bytes, PositionMemory::kEncodedSize + 1, kMaxRho, out));
        assert(!PositionMemory::decode(nullptr, PositionMemory::kEncodedSize, kMaxRho, out));
    }
    // A record from a different schema is rejected even if well formed.
    {
        uint8_t bytes[PositionMemory::kEncodedSize];
        PositionMemory::encode({true, 0.0, 0.0}, bytes);
        bytes[0] ^= 0xFF;
        Record out{};
        assert(!PositionMemory::decode(bytes, sizeof(bytes), kMaxRho, out));
    }
    // Non-finite or out-of-range coordinates are rejected.
    {
        Record out{};
        assert(!roundTrip({true, NAN, 10.0}, out));
        assert(!roundTrip({true, INFINITY, 10.0}, out));
        assert(!roundTrip({true, 0.0, NAN}, out));
        assert(!roundTrip({true, 0.0, -0.5}, out));
        assert(!roundTrip({true, 0.0, kMaxRho + 0.01}, out));
        assert(roundTrip({true, 0.0, kMaxRho}, out));
        assert(roundTrip({true, 0.0, 0.0}, out));
    }
    // Theta is reduced to one turn so restored step counters stay bounded.
    {
        const double pi = 3.14159265358979323846;
        auto near = [](double a, double b) { return std::fabs(a - b) < 1e-12; };
        assert(near(PositionMemory::normalizeTheta(0.0), 0.0));
        assert(near(PositionMemory::normalizeTheta(1.0), 1.0));
        assert(near(PositionMemory::normalizeTheta(-1.0), -1.0));
        assert(near(PositionMemory::normalizeTheta(2 * pi + 0.5), 0.5));
        assert(near(PositionMemory::normalizeTheta(-2 * pi - 0.5), -0.5));
        assert(near(PositionMemory::normalizeTheta(7 * pi / 2), -pi / 2));
        assert(near(PositionMemory::normalizeTheta(1000 * 2 * pi + 3.0), 3.0));
        const double n = PositionMemory::normalizeTheta(pi);
        assert(n >= -pi && n < pi);
    }
    // Restoring a saved rho references both rho motors there, and later
    // executed deltas accumulate from that point.
    {
        RhoPositionTracker tracker(212.5);
        tracker.invalidate();
        tracker.reference(300.0);
        RhoPositionTracker::Sample s = tracker.get();
        assert(s.available && s.referenced);
        assert(s.mainRho == 300.0 && s.companionRho == 300.0);
        tracker.sample(305.0, RhoPositionTracker::Motor::BOTH, true, true);
        s = tracker.get();
        assert(s.mainRho == 305.0 && s.companionRho == 305.0);
    }
    std::cout << "PASS: position record round trip, rejection of corrupt/foreign/out-of-range records, theta normalization, rho tracker reference\n";
    return 0;
}
