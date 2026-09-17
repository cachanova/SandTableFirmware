#include <atomic>
#include <diskio_impl.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// The pinned Arduino SD driver exports these C++ callbacks. Wrap its FatFS
// registration rather than modifying the shared framework installation.
DRESULT ff_sd_read(uint8_t, uint8_t*, uint32_t, unsigned);
DRESULT ff_sd_write(uint8_t, const uint8_t*, uint32_t, unsigned);
extern "C" void __real_ff_diskio_register(BYTE, const ff_diskio_impl_t*);

namespace {
void diskProgress(DRESULT result) {
    if (result != RES_OK) return;
    // A single unlink can walk thousands of FAT sectors without returning to
    // AsyncTCP. Feed only after completed disk I/O; a stuck transfer still
    // trips the watchdog. Never subscribe/unsubscribe or extend its timeout.
    if (esp_task_wdt_status(nullptr) == ESP_OK) esp_task_wdt_reset();

    // SD's SPI lock has been released by this point. Let the idle task run
    // during a long chain walk, without adding a tick to every sector read.
    static std::atomic<TickType_t> lastYield{0};
    const TickType_t now = xTaskGetTickCount();
    TickType_t previous = lastYield.load(std::memory_order_relaxed);
    if (static_cast<TickType_t>(now - previous) >= pdMS_TO_TICKS(20) &&
        lastYield.compare_exchange_strong(previous, now, std::memory_order_relaxed)) {
        vTaskDelay(1);
    }
}

DRESULT readWithProgress(BYTE drive, BYTE* data, DWORD sector, UINT count) {
    const DRESULT result = ff_sd_read(drive, data, sector, count);
    diskProgress(result);
    return result;
}

DRESULT writeWithProgress(BYTE drive, const BYTE* data, DWORD sector, UINT count) {
    const DRESULT result = ff_sd_write(drive, data, sector, count);
    diskProgress(result);
    return result;
}
}

extern "C" void __wrap_ff_diskio_register(BYTE drive, const ff_diskio_impl_t* implementation) {
    if (implementation && implementation->read == ff_sd_read && implementation->write == ff_sd_write) {
        // IDF copies the callback table in ff_diskio_register. All other
        // callbacks, drive numbers, and unregistration keep their SDK behavior.
        ff_diskio_impl_t cooperative = *implementation;
        cooperative.read = readWithProgress;
        cooperative.write = writeWithProgress;
        __real_ff_diskio_register(drive, &cooperative);
    } else {
        __real_ff_diskio_register(drive, implementation);
    }
}
