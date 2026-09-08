#pragma once

#include <array>
#include <cstddef>

namespace RhoAcousticProfile {

static constexpr float kExcursionMm = 400.0f;
static const std::array<float, 2> kContinuousOffsetsMm = {
    kExcursionMm, 0.0f,
};
static const std::array<float, 8> kGatedOffsetsMm = {
    kExcursionMm, 0.0f, kExcursionMm, 0.0f,
    kExcursionMm, 0.0f, kExcursionMm, 0.0f,
};
static const std::array<float, 38> kStressOffsetsMm = {
    1.0f, 0.0f, 2.0f, 0.0f, 5.0f, 0.0f, 10.0f, 0.0f,
    20.0f, 0.0f, 40.0f, 0.0f, 75.0f, 0.0f, 100.0f, 0.0f,
    150.0f, 0.0f, 200.0f, 0.0f, 300.0f, 0.0f, kExcursionMm, 0.0f,
    10.0f, 160.0f, 5.0f, 320.0f, 20.0f, kExcursionMm, 2.0f,
    240.0f, 1.0f, 120.0f, 360.0f, 60.0f, 280.0f, 0.0f,
};

}  // namespace RhoAcousticProfile
