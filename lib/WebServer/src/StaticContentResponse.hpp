#pragma once
#include "KnownLengthResponse.hpp"

// ESP32 flash content is memory mapped. Keep only the caller's static pointer;
// KnownLengthResponse copies one bounded chunk at a time into its TCP buffer.
// The source must remain valid for the entire response lifetime.
class StaticContentResponse : public KnownLengthResponse {
public:
    StaticContentResponse(const char* contentType, const uint8_t* data, size_t length)
        : m_data(data) {
        _code = 200;
        _contentType = contentType;
        _contentLength = length;
        _sendContentLength = true;
        _chunked = false;
    }
    bool _sourceValid() const override { return m_data || !_contentLength; }
    size_t _fillBuffer(uint8_t* data, size_t length) override {
        const size_t count = std::min(length, _contentLength - m_offset);
        if (count) std::memcpy(data, m_data + m_offset, count);
        m_offset += count;
        return count;
    }
private:
    const uint8_t* const m_data;
    size_t m_offset = 0;
};
