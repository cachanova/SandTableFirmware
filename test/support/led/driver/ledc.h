#pragma once
#include <Arduino.h>
#include "gpio.h"
constexpr int LEDC_HIGH_SPEED_MODE = 0;
constexpr int LEDC_CHANNEL_0 = 0;
constexpr int LEDC_TIMER_0 = 0;
inline uint32_t testPendingDuty = 0;
inline int ledc_set_duty(int, int, uint32_t duty) {
    testPendingDuty = duty;
    return testPwmFailure ? -1 : ESP_OK;
}
inline int ledc_update_duty(int, int) {
    if (testPwmFailure) return -1;
    testPwmDuty = testPendingDuty;
    return ESP_OK;
}
inline uint32_t ledc_get_freq(int, int) { return 5000; }
