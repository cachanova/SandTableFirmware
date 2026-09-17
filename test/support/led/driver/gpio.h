#pragma once
using gpio_num_t = int;
enum gpio_pull_mode_t { GPIO_PULLDOWN_ONLY };
constexpr int ESP_OK = 0;
inline int gpio_set_pull_mode(gpio_num_t, gpio_pull_mode_t) { return ESP_OK; }
