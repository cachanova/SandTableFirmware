// g++ -std=c++17 -Wall -Wextra -Werror -Itest/support/sd_progress test/test_sd_progress.cpp -o /tmp/test_sd_progress
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "../src/SDProgress.cpp"
static ff_diskio_impl_t registered{};
static const ff_diskio_impl_t* forwarded;
static BYTE registeredDrive;
static DRESULT result = RES_OK;
static int resets = 0, yields = 0, reads = 0, writes = 0;
static TickType_t ticks = 0;
static bool subscribed = true;
int esp_task_wdt_status(void*) { return subscribed ? ESP_OK : -1; }
int esp_task_wdt_reset() { ++resets; return ESP_OK; }
TickType_t xTaskGetTickCount() { return ticks; }
void vTaskDelay(TickType_t delay) { assert(delay == 1); ++yields; }
extern "C" void __real_ff_diskio_register(BYTE drive, const ff_diskio_impl_t* impl) {
    registeredDrive = drive; forwarded = impl;
    if (impl) registered = *impl;
}
DRESULT ff_sd_read(BYTE drive, BYTE* data, DWORD sector, UINT count) {
    assert(drive == 1 && sector == 123 && count == 2);
    data[0] = 42; ++reads; return result;
}
DRESULT ff_sd_write(BYTE drive, const BYTE* data, DWORD sector, UINT count) {
    assert(drive == 1 && data[0] == 42 && sector == 456 && count == 3);
    ++writes; return result;
}
static DSTATUS init(BYTE) { return 7; }
static DRESULT ioctl(BYTE, BYTE, void*) { return RES_PARERR; }
int main() {
    ff_diskio_impl_t sd{init, init, ff_sd_read, ff_sd_write, ioctl};
    __wrap_ff_diskio_register(1, &sd);
    assert(registeredDrive == 1 && registered.init == init && registered.status == init && registered.ioctl == ioctl);
    BYTE data[1024]{};
    assert(registered.read(1, data, 123, 2) == RES_OK && data[0] == 42);
    assert(reads == 1 && resets == 1 && yields == 0);
    ticks = 20;
    assert(registered.write(1, data, 456, 3) == RES_OK);
    assert(writes == 1 && resets == 2 && yields == 1);
    for (DRESULT failure : {RES_ERROR, RES_WRPRT, RES_NOTRDY, RES_PARERR}) {
        result = failure; ticks += 20;
        assert(registered.read(1, data, 123, 2) == failure);
        assert(registered.write(1, data, 456, 3) == failure);
    }
    assert(resets == 2 && yields == 1); // failed I/O cannot keep the watchdog alive
    result = RES_OK; subscribed = false;
    registered.read(1, data, 123, 2);
    assert(resets == 2 && yields == 2); // idle task can still run
    subscribed = true;
    ticks = UINT32_MAX - 10; registered.read(1, data, 123, 2);
    ticks = 10; registered.read(1, data, 123, 2);
    assert(resets == 4 && yields == 4); // tick rollover
    ff_diskio_impl_t other{init, init, nullptr, nullptr, ioctl};
    __wrap_ff_diskio_register(0, &other);
    assert(forwarded == &other && registeredDrive == 0);
    __wrap_ff_diskio_register(1, nullptr);
    assert(forwarded == nullptr && registeredDrive == 1);
    puts("SD callbacks preserve I/O and feed/yield only on successful progress: PASS");
}
