#pragma once
#include <ESPAsyncWebServer.h>
#include <esp_heap_caps.h>
#include "ResponseBuffer.hpp"
#include "KnownLengthResponse.hpp"

static constexpr char kResponseUnavailable[] =
    "{\"success\":false,\"message\":\"Controller busy; retry shortly\"}";

// Arduino cbuf grows by as little as one byte, and its throwing new[] can
// terminate the firmware under concurrent image/API traffic. Never use it for
// assembled responses. Return a complete 503 document if allocation is refused.
class BufferedResponse : public KnownLengthResponse, public Print {
public:
    explicit BufferedResponse(const char* contentType, size_t = 0, bool control = false)
        : m_buffer(65536, control ? allocateControlBlock : allocateBlock) {
        _code = 200;
        _contentType = contentType;
        _sendContentLength = true;
        _chunked = false;
        _contentLength = 0;
    }
    using Print::write;
    size_t write(uint8_t byte) override { return write(&byte, 1); }
    size_t write(const uint8_t* data, size_t length) override {
        if (m_failed) return 0;
        if (!m_buffer.append(data, length)) {
            m_failed = true;
            _code = 503;
            _contentType = "application/json";
            _contentLength = sizeof(kResponseUnavailable) - 1;
            addHeader("Retry-After", "1");
            return 0;
        }
        _contentLength += length;
        return length;
    }
    bool _sourceValid() const override { return true; }
    size_t _fillBuffer(uint8_t* data, size_t length) override {
        if (!m_failed) return m_buffer.read(data, length);
        const size_t count = std::min(length, sizeof(kResponseUnavailable) - 1 - m_errorOffset);
        memcpy(data, kResponseUnavailable + m_errorOffset, count);
        m_errorOffset += count;
        return count;
    }
private:
    static void* allocateControlBlock(size_t size) {
        // Status and command acknowledgements may use the control reserve
        // that bulk diagnostics leave behind; retain room for TCP framing.
        if (heap_caps_get_free_size(MALLOC_CAP_8BIT) < size + 4096) return nullptr;
        return malloc(size);
    }
    static void* allocateBlock(size_t size) {
        // Keep headroom for TCP buffers, file IO, and control replies.
        if (heap_caps_get_free_size(MALLOC_CAP_8BIT) < size + 16384) return nullptr;
        return malloc(size);
    }
    ResponseBuffer m_buffer;
    bool m_failed = false;
    size_t m_errorOffset = 0;
};
