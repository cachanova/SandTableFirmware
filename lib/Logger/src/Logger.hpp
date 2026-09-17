#pragma once

#ifdef NATIVE_BUILD
#include <cstdio>

#define LOG(fmt, ...) do { \
    std::printf(fmt, ##__VA_ARGS__); \
} while(0)

#else
#include <Arduino.h>
#include <Print.h>
#include <cstdarg>

// A bounded mirror of the serial console. It is available before Wi-Fi starts,
// so the HTTP diagnostics endpoint can return boot messages after connecting.
class RuntimeLog {
public:
    static RuntimeLog& instance();

    void printf(const char* format, ...) __attribute__((format(printf, 2, 3)));
    void clear();
    void writeJson(Print& out, uint32_t sinceId = 0, size_t limit = 64) const;
    void writeText(Print& out, uint32_t sinceId = 0, size_t limit = 64) const;
    uint32_t nextId() const;
    uint32_t droppedCount() const;

private:
    // Each entry is 200 bytes on ESP32. Keep 6.25 KiB available for Wi-Fi/TCP
    // bursts instead of a longer console history; serial and ErrorLog retain
    // their existing behavior. Clients can poll by ID to preserve more history.
    static constexpr size_t kMaxEntries = 32;
    static constexpr size_t kTextBytes = 192;

    struct Entry {
        uint32_t id = 0;
        uint32_t tsMs = 0;
        char text[kTextBytes]{};
    };

    RuntimeLog() = default;
    void append(const char* text);
    void writeJsonString(Print& out, const char* value) const;
    bool snapshotEntry(size_t index, uint32_t expectedId, Entry& out) const;

    mutable portMUX_TYPE m_mutex = portMUX_INITIALIZER_UNLOCKED;
    Entry m_entries[kMaxEntries];
    size_t m_head = 0;
    size_t m_size = 0;
    uint32_t m_nextId = 1;
    uint32_t m_dropped = 0;
};

#define LOG(fmt, ...) do { \
    RuntimeLog::instance().printf(fmt, ##__VA_ARGS__); \
} while(0)

#endif
