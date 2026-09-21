#pragma once
#include <cstdint>
using gpio_num_t = int;
constexpr int ESP_OK = 0;
constexpr int GPIO_MODE_OUTPUT = 1;
constexpr int GPIO_PULLUP_DISABLE = 0;
constexpr int GPIO_PULLDOWN_ENABLE = 1;
constexpr int GPIO_INTR_DISABLE = 0;
struct gpio_config_t {
    uint64_t pin_bit_mask = 0;
    int mode = 0, pull_up_en = 0, pull_down_en = 0, intr_type = 0;
};
inline gpio_config_t testConfig;
inline bool testGpioFailure = false, testConfigFailure = false;
inline int testOutput = 1;
inline unsigned testWrites = 0;
inline bool testConfiguredLow = false;
inline int gpio_set_level(gpio_num_t, uint32_t level) {
    if (testGpioFailure) return -1;
    testOutput = level;
    ++testWrites;
    return ESP_OK;
}
inline int gpio_config(const gpio_config_t* config) {
    testConfig = *config;
    testConfiguredLow = testOutput == 0;
    return testConfigFailure ? -1 : ESP_OK;
}
