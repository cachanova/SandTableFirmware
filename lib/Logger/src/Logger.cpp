#include "Logger.hpp"

#ifndef NATIVE_BUILD
#include <cstdio>
#include <cstring>

RuntimeLog& RuntimeLog::instance() {
    static RuntimeLog log;
    return log;
}

void RuntimeLog::printf(const char* format, ...) {
    char buffer[kTextBytes];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Keep serial output for bench recovery, but make Wi-Fi the normal debug
    // transport once the mechanism is connected.
    Serial.print(buffer);
    append(buffer);
}

void RuntimeLog::append(const char* text) {
    if (!text) return;
    portENTER_CRITICAL(&m_mutex);
    if (m_size == kMaxEntries) {
        ++m_dropped;
    } else {
        ++m_size;
    }
    Entry& entry = m_entries[m_head];
    entry.id = m_nextId++;
    entry.tsMs = millis();
    strncpy(entry.text, text, sizeof(entry.text) - 1);
    entry.text[sizeof(entry.text) - 1] = '\0';
    m_head = (m_head + 1) % kMaxEntries;
    portEXIT_CRITICAL(&m_mutex);
}

void RuntimeLog::clear() {
    portENTER_CRITICAL(&m_mutex);
    m_head = 0;
    m_size = 0;
    m_dropped = 0;
    portEXIT_CRITICAL(&m_mutex);
}

uint32_t RuntimeLog::nextId() const {
    portENTER_CRITICAL(&m_mutex);
    const uint32_t value = m_nextId;
    portEXIT_CRITICAL(&m_mutex);
    return value;
}

uint32_t RuntimeLog::droppedCount() const {
    portENTER_CRITICAL(&m_mutex);
    const uint32_t value = m_dropped;
    portEXIT_CRITICAL(&m_mutex);
    return value;
}

bool RuntimeLog::snapshotEntry(size_t index, uint32_t expectedId, Entry& out) const {
    portENTER_CRITICAL(&m_mutex);
    const Entry& entry = m_entries[index];
    const bool matches = entry.id == expectedId;
    if (matches) out = entry;
    portEXIT_CRITICAL(&m_mutex);
    return matches;
}

void RuntimeLog::writeJsonString(Print& out, const char* value) const {
    out.print('"');
    for (const char* p = value; p && *p; ++p) {
        switch (*p) {
            case '"': case '\\': out.print('\\'); out.print(*p); break;
            case '\n': out.print("\\n"); break;
            case '\r': out.print("\\r"); break;
            case '\t': out.print("\\t"); break;
            default:
                if (static_cast<uint8_t>(*p) >= 0x20) out.print(*p);
                break;
        }
    }
    out.print('"');
}

void RuntimeLog::writeJson(Print& out, uint32_t sinceId, size_t limit) const {
    size_t size;
    size_t start;
    uint32_t firstId;
    uint32_t next;
    uint32_t dropped;
    portENTER_CRITICAL(&m_mutex);
    size = m_size;
    start = (m_head + kMaxEntries - m_size) % kMaxEntries;
    firstId = size ? m_entries[start].id : m_nextId;
    next = m_nextId;
    dropped = m_dropped;
    portEXIT_CRITICAL(&m_mutex);

    if (limit > kMaxEntries) limit = kMaxEntries;
    out.print("{\"nextId\":"); out.print(next);
    out.print(",\"dropped\":"); out.print(dropped);
    out.print(",\"entries\":[");
    bool first = true;
    size_t written = 0;
    for (size_t offset = 0; offset < size && written < limit; ++offset) {
        const uint32_t expectedId = firstId + static_cast<uint32_t>(offset);
        if (expectedId <= sinceId) continue;
        Entry entry;
        if (!snapshotEntry((start + offset) % kMaxEntries, expectedId, entry)) continue;
        if (!first) out.print(',');
        first = false;
        ++written;
        out.print("{\"id\":"); out.print(entry.id);
        out.print(",\"tsMs\":"); out.print(entry.tsMs);
        out.print(",\"text\":"); writeJsonString(out, entry.text);
        out.print('}');
    }
    out.print("]}");
}

void RuntimeLog::writeText(Print& out, uint32_t sinceId, size_t limit) const {
    size_t size;
    size_t start;
    uint32_t firstId;
    portENTER_CRITICAL(&m_mutex);
    size = m_size;
    start = (m_head + kMaxEntries - m_size) % kMaxEntries;
    firstId = size ? m_entries[start].id : m_nextId;
    portEXIT_CRITICAL(&m_mutex);

    if (limit > kMaxEntries) limit = kMaxEntries;
    size_t written = 0;
    for (size_t offset = 0; offset < size && written < limit; ++offset) {
        const uint32_t expectedId = firstId + static_cast<uint32_t>(offset);
        if (expectedId <= sinceId) continue;
        Entry entry;
        if (!snapshotEntry((start + offset) % kMaxEntries, expectedId, entry)) continue;
        out.printf("[%lu ms #%lu] ", static_cast<unsigned long>(entry.tsMs),
                   static_cast<unsigned long>(entry.id));
        out.print(entry.text);
        const size_t len = strlen(entry.text);
        if (len == 0 || (entry.text[len - 1] != '\n' && entry.text[len - 1] != '\r')) {
            out.print('\n');
        }
        ++written;
    }
}
#endif
