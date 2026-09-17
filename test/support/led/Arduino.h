#pragma once
#include <cstdint>
inline uint32_t testPwmDuty = 0;
inline void ledcSetup(int, int, int) {}
inline void ledcAttachPin(uint8_t, int) {}
inline bool ledcAttachChannel(uint8_t, int, int, int) { return true; }
inline void ledcWrite(int, uint32_t duty) { testPwmDuty = duty; }
