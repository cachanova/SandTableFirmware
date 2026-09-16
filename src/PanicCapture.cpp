// Preserve a small backtrace across a panic reset when USB serial is absent.
// No filesystem, allocation, logging, or network calls are made in the handler.
#include <Arduino.h>
#include <esp_debug_helpers.h>
#include <esp_private/panic_internal.h>
#include <xtensa/xtensa_context.h>
#include <Logger.hpp>

namespace {
constexpr uint32_t kMagic = 0x53595350;
struct Capture {
    uint32_t magic;
    uint32_t core;
    uint32_t exception;
    uint32_t cause;
    uint32_t address;
    uint32_t count;
    uint32_t pc[12];
    uint32_t sp[12];
};
RTC_NOINIT_ATTR Capture previousPanic;
}

extern "C" void __real_esp_panic_handler(panic_info_t* info);
extern "C" void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t* info) {
    previousPanic.magic = 0;
    previousPanic.count = 0;
    if (info && info->frame) {
        const auto* frame = static_cast<const XtExcFrame*>(info->frame);
        previousPanic.core = info->core;
        previousPanic.exception = info->exception;
        previousPanic.cause = frame->exccause;
        previousPanic.address = frame->excvaddr;
        esp_backtrace_frame_t backtrace{static_cast<uint32_t>(frame->pc),
            static_cast<uint32_t>(frame->a1), static_cast<uint32_t>(frame->a0), frame};
        for (unsigned i = 0; i < 12; ++i) {
            previousPanic.pc[i] = backtrace.pc;
            previousPanic.sp[i] = backtrace.sp;
            previousPanic.count = i + 1;
            if (!backtrace.next_pc || !esp_backtrace_get_next_frame(&backtrace)) break;
        }
        previousPanic.magic = kMagic;
    }
    __real_esp_panic_handler(info);
}

void reportPreviousPanic() {
    if (esp_reset_reason() == ESP_RST_PANIC && previousPanic.magic == kMagic &&
        previousPanic.count <= 12) {
        LOG("Previous panic: core=%lu exception=%lu cause=%lu address=%08lx\r\n",
            static_cast<unsigned long>(previousPanic.core),
            static_cast<unsigned long>(previousPanic.exception),
            static_cast<unsigned long>(previousPanic.cause),
            static_cast<unsigned long>(previousPanic.address));
        for (unsigned i = 0; i < previousPanic.count; ++i) {
            LOG("Panic frame %u: pc=%08lx sp=%08lx\r\n", i,
                static_cast<unsigned long>(previousPanic.pc[i]),
                static_cast<unsigned long>(previousPanic.sp[i]));
        }
    }
    previousPanic.magic = 0;
}
