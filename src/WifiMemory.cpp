#include <esp_wifi.h>

extern "C" esp_err_t __real_esp_wifi_init(const wifi_init_config_t* config);

// Arduino's binary SDK fixes the default buffer counts, and WiFiGeneric only
// exposes an all-or-nothing static-buffer switch. Set a modest explicit budget
// at the public IDF initialization boundary instead of patching the framework.
// Dynamic TX allocation failed under SD/web heap fragmentation even while the
// station remained associated; reserve those DMA packets before serving HTTP.
extern "C" esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t* config) {
    if (!config) return __real_esp_wifi_init(config);
    wifi_init_config_t bounded = *config;
    bounded.static_rx_buf_num = 6;
    // Allocation tracing under concurrent HTTP/SSE load found bursts of
    // 2308-byte Wi-Fi allocations consuming the web/control reserve.
    // Bound the outstanding receive packets, including the AMPDU window.
    // Six remains >= the static RX count and can still fill the TCP window.
    bounded.dynamic_rx_buf_num = 6;
    bounded.rx_ba_win = 6;
    bounded.tx_buf_type = 0;
    bounded.static_tx_buf_num = 6;
    bounded.dynamic_tx_buf_num = 0;
    bounded.cache_tx_buf_num = 2;
    return __real_esp_wifi_init(&bounded);
}
