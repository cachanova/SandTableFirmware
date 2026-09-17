#pragma once
#include <cstdint>
inline uint32_t testPwmDuty = 0;
inline bool testPwmFailure = false;
inline uint32_t testMicros = 0;
inline uint32_t micros() { return ++testMicros; }
inline double ledcSetup(int, int frequency, int) { return frequency; }
inline void ledcAttachPin(uint8_t, int) {}
inline bool ledcAttachChannel(uint8_t, int, int, int) { return true; }
inline bool ledcWrite(int, uint32_t duty) {
    if (testPwmFailure) return false;
    testPwmDuty = duty == 255 ? 256 : duty;
    return true;
}
inline uint32_t ledcRead(int) { return testPwmDuty; }
inline uint32_t ledcReadFreq(int) { return 5000; }
