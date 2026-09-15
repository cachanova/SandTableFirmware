#pragma once
#include <algorithm>
#include <cstdint>

// Independent test reference only. Production must not infer a home
// coordinate from an arbitrary 425 mm starting label.
inline uint32_t rhoStartupHomeCoordinate(int32_t knownStart,
                                        uint32_t inwardProbe,
                                        uint32_t outwardRunway,
                                        uint32_t maximumTravel) {
    const int64_t afterProbe = std::max<int64_t>(
        0, static_cast<int64_t>(knownStart) - inwardProbe);
    return static_cast<uint32_t>(std::min<int64_t>(
        maximumTravel, afterProbe + outwardRunway));
}
