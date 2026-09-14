#pragma once

#include <cstdint>

struct RhoBoundedReturn {
    bool contactWithinWindow;
    uint32_t coarseOverrunSteps;
    uint32_t precisionCapSteps;
};

// After backing off from the first candidate, the second inward trigger
// must reproduce that command coordinate within the selected agreement
// window. This does not prove that either trigger was the physical end stop.
inline bool rhoApproachesAgree(uint32_t backoffSteps,
                               uint32_t returnSteps,
                               uint32_t agreementSteps) {
    const uint32_t separation = returnSteps > backoffSteps
        ? returnSteps - backoffSteps
        : backoffSteps - returnSteps;
    return separation <= agreementSteps;
}

// Keep the total commanded travel beyond the operator-confirmed zero within
// one shared tolerance across both inward approaches. The second approach
// starts after backing off from the first trigger, so it must spend any
// overrun already consumed by the coarse approach.
inline RhoBoundedReturn rhoBoundedReturn(
        uint32_t expectedContactSteps, uint32_t coarseSteps,
        uint32_t backoffSteps, uint32_t toleranceSteps) {
    const bool within =
        static_cast<uint64_t>(coarseSteps) + toleranceSteps >=
            expectedContactSteps &&
        coarseSteps <= static_cast<uint64_t>(expectedContactSteps) +
            toleranceSteps;
    const uint32_t overrun = coarseSteps > expectedContactSteps
        ? coarseSteps - expectedContactSteps : 0;
    return {within, overrun,
            within ? backoffSteps + toleranceSteps - overrun : 0};
}

// The final commanded position relative to the confirmed zero is the sum
// of both inward-pass contact errors. Checking each pass separately can
// accept a result two tolerances short of zero.
inline bool rhoReturnWithinWindow(
        uint32_t expectedContactSteps, uint32_t coarseSteps,
        uint32_t backoffSteps, uint32_t precisionSteps,
        uint32_t toleranceSteps) {
    const int64_t combinedError =
        static_cast<int64_t>(coarseSteps) + precisionSteps -
        expectedContactSteps - backoffSteps;
    return combinedError >= -static_cast<int64_t>(toleranceSteps) &&
        combinedError <= static_cast<int64_t>(toleranceSteps);
}
