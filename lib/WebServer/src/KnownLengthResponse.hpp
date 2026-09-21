#pragma once
#include <ESPAsyncWebServer.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>

// Known-length, non-template responses only. ESPAsyncWebServer 3.9.5 marks
// its final source chunk finished before TCP accepts it; a short add() under
// memory pressure can therefore close the connection with body bytes pending.
// Keep source reads and TCP acceptance separate, without SDK-private state.
class KnownLengthResponse : public AsyncWebServerResponse {
public:
    // Two full TCP segments keep delayed ACKs moving without filling the
    // SDK's 5760-byte send buffer on every concurrent HTTP connection.
    static constexpr size_t kMaxInFlight = 2 * TCP_MSS;
    using BufferAllocator = void* (*)(size_t);
    explicit KnownLengthResponse(BufferAllocator allocator = std::malloc)
        : m_allocator(allocator) {}
    ~KnownLengthResponse() override { std::free(m_heapBuffer); }
    KnownLengthResponse(const KnownLengthResponse&) = delete;
    KnownLengthResponse& operator=(const KnownLengthResponse&) = delete;

    void _respond(AsyncWebServerRequest* request) final {
        if (_chunked || !_sendContentLength) { fail(request); return; }
        try {
            addHeader("Connection", "close", false);
            _addResponseHeaders();
            _assembleHead(m_headers, request->version());
        } catch (const std::bad_alloc&) {
            fail(request);
            return;
        }
        const size_t length = m_headers.length();
        if (length < 4 || std::memcmp(m_headers.c_str() + length - 4, "\r\n\r\n", 4)) {
            fail(request);
            return;
        }
        _state = RESPONSE_HEADERS;
        pump(request);
    }

    size_t _ack(AsyncWebServerRequest* request, size_t length, uint32_t) final {
        _ackedLength += std::min(length, _writtenLength - _ackedLength);
        return pump(request);
    }

    // Subclass headers belong here rather than in a constructor: adding one
    // allocates, and a refused allocation must fail the response instead of
    // escaping the route handler that constructed it.
    virtual void _addResponseHeaders() {}

    virtual size_t _fillBuffer(uint8_t* data, size_t length) = 0;

private:
    void fail(AsyncWebServerRequest* request) {
        _state = RESPONSE_FAILED;
        // close() synchronously destroys this response AND its request. The
        // SDK still accesses both after _ack/_respond returns. Let its ACK
        // handler close a finished response, or its next timeout close a
        // failed initial response/poll with no further ACKs outstanding.
        request->client()->setRxTimeout(1);
    }

    size_t pump(AsyncWebServerRequest* request) {
        if (_state == RESPONSE_END || _state == RESPONSE_FAILED) return 0;
        if (!_sourceValid()) { fail(request); return 0; }
        auto* client = request->client();
        size_t accepted = 0;
        // Bound work to four 1 KB body writes (at most four 5 ms prefetch
        // waits), while allowing enough queued data to prompt timely TCP ACKs.
        for (unsigned attempt = 0; attempt < 4; ++attempt) {
            const size_t outstanding = _writtenLength - _ackedLength;
            const size_t space = std::min(client->space(), kMaxInFlight - outstanding);
            if (!space) break;

            if (_state == RESPONSE_HEADERS) {
                const size_t wanted = std::min(space, m_headers.length() - m_headerOffset);
                const size_t count = client->add(m_headers.c_str() + m_headerOffset, wanted);
                m_headerOffset += count;
                _writtenLength += count;
                accepted += count;
                if (count < wanted || m_headerOffset < m_headers.length()) break;
                m_headers = String();
                _state = _contentLength ? RESPONSE_CONTENT : RESPONSE_END;
                if (_state == RESPONSE_END) break;
                continue;
            }

            if (_state != RESPONSE_CONTENT) break;
            if (m_offset == m_length) {
                // Keep the response object small enough for fragmented heaps.
                // Try a larger staging block only once; allocation failure is
                // recoverable because the inline buffer still sends exact bytes.
                if (!m_allocationAttempted) {
                    m_allocationAttempted = true;
                    if (_contentLength > sizeof(m_inlineBuffer)) {
                        m_heapBuffer = static_cast<uint8_t*>(m_allocator(1024));
                    }
                }
                // A source may consume/free its own storage here. Keep the bytes
                // locally until add() has copied every one into TCP's queue.
                const size_t capacity = m_heapBuffer ? 1024 : sizeof(m_inlineBuffer);
                const size_t wanted = std::min(capacity,
                    std::min(space, _contentLength - _sentLength));
                const size_t count = _fillBuffer(buffer(), wanted);
                if (count == RESPONSE_TRY_AGAIN) break;
                if (!count || count > wanted) { fail(request); return accepted; }
                m_offset = 0;
                m_length = count;
            }

            const size_t wanted = std::min(space, m_length - m_offset);
            const size_t count = client->add(
                reinterpret_cast<const char*>(buffer() + m_offset), wanted);
            m_offset += count;
            _sentLength += count;
            _writtenLength += count;
            accepted += count;
            if (count < wanted) break;
            if (_sentLength == _contentLength) {
                _state = RESPONSE_END;
                break;
            }
        }
        // add() uses COPY semantics. A temporary tcp_output failure must not
        // discard our remaining bytes or turn a recoverable short write into EOF.
        client->send();
        return accepted;
    }

    uint8_t* buffer() { return m_heapBuffer ? m_heapBuffer : m_inlineBuffer; }

    String m_headers;
    size_t m_headerOffset = 0;
    BufferAllocator m_allocator;
    uint8_t* m_heapBuffer = nullptr;
    bool m_allocationAttempted = false;
    uint8_t m_inlineBuffer[256];
    size_t m_offset = 0;
    size_t m_length = 0;
};
