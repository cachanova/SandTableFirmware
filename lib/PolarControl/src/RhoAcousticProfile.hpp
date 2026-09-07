#pragma once

#include <array>
#include <cstddef>

namespace RhoAcousticProfile {

static constexpr float kExcursionMm = 200.0f;
static const std::array<float, 2> kContinuousOffsetsMm = {
    kExcursionMm, 0.0f,
};
static const std::array<float, 34> kStressOffsetsMm = {
    1.0f, 0.0f, 2.0f, 0.0f, 5.0f, 0.0f, 10.0f, 0.0f,
    20.0f, 0.0f, 40.0f, 0.0f, 75.0f, 0.0f, 100.0f, 0.0f,
    150.0f, 0.0f, 200.0f, 0.0f,
    10.0f, 80.0f, 5.0f, 160.0f, 20.0f, 200.0f, 2.0f,
    120.0f, 1.0f, 60.0f, 180.0f, 30.0f, 140.0f, 0.0f,
};

}  // namespace RhoAcousticProfile
