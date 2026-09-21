#pragma once
#include "KnownLengthResponse.hpp"

// ESP32 flash content is memory mapped. Keep only the caller's static pointer;
// KnownLengthResponse copies one bounded chunk at a time into its TCP buffer.
// The source must remain valid for the entire response lifetime.
class StaticContentResponse : public KnownLengthResponse {
public:
    // One memory-mapped span of the body.
    struct Segment {
        const uint8_t* data;
        size_t length;
    };

    StaticContentResponse(const char* contentType, const uint8_t* data, size_t length)
        : m_inline{data, length}, m_segments(&m_inline), m_count(1) {
        describe(contentType, length);
    }

    // Spans sent back to back as one body. Pages share their dialog markup
    // this way: the shared spans sit in flash once instead of once per page.
    StaticContentResponse(const char* contentType, const Segment* segments, size_t count)
        : m_segments(segments), m_count(count) {
        size_t total = 0;
        for (size_t i = 0; i < count; ++i) total += segments[i].length;
        describe(contentType, total);
    }

    bool _sourceValid() const override {
        for (size_t i = 0; i < m_count; ++i) {
            if (!m_segments[i].data && m_segments[i].length) return false;
        }
        return true;
    }

    size_t _fillBuffer(uint8_t* data, size_t length) override {
        size_t written = 0;
        while (written < length && m_index < m_count) {
            const Segment& segment = m_segments[m_index];
            const size_t count = std::min(length - written, segment.length - m_offset);
            if (count) std::memcpy(data + written, segment.data + m_offset, count);
            written += count;
            m_offset += count;
            if (m_offset == segment.length) {
                ++m_index;
                m_offset = 0;
            }
        }
        return written;
    }

private:
    void describe(const char* contentType, size_t length) {
        _code = 200;
        _contentType = contentType;
        _contentLength = length;
        _sendContentLength = true;
        _chunked = false;
    }
    const Segment m_inline{nullptr, 0};
    const Segment* const m_segments;
    const size_t m_count;
    size_t m_index = 0;
    size_t m_offset = 0;
};
