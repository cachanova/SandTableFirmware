#pragma once

#include <cstddef>
#include <cstdint>

// ESP32, 20 MHz LLTF: indices 0..31, -32..-1, imaginary then real.
// Use only active subcarriers common to legacy and HT packets. Skip DC,
// guard carriers and the first two complex bins (possibly invalid in hardware).
namespace CsiFeatures {
constexpr size_t kBytes = 128;
constexpr size_t kBins = 51;

inline bool extract(const int8_t* bytes, size_t length, float* powers) {
    if (!bytes || !powers || length != kBytes) return false;
    size_t output = 0;
    uint32_t total = 0;
    for (size_t bin = 2; bin < 64; ++bin) {
        if (bin > 26 && bin < 38) continue;
        const int imaginary = bytes[bin * 2];
        const int real = bytes[bin * 2 + 1];
        const int power = real * real + imaginary * imaginary;
        powers[output++] = static_cast<float>(power);
        total += power;
    }
    return output == kBins && total > 0;
}
}
