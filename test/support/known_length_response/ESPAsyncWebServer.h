#pragma once
// Minimal public SDK surface for exercising the production response transport.
// No ESP32 SDK internals are reimplemented here: TCP acceptance is fault-injected.
#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <new>
#include <string>

using String = std::string;
static constexpr size_t TCP_MSS = 1436; // pinned ESP32 SDK configuration
static constexpr size_t RESPONSE_TRY_AGAIN = 0xFFFFFFFF;
enum WebResponseState {
    RESPONSE_SETUP, RESPONSE_HEADERS, RESPONSE_CONTENT,
    RESPONSE_WAIT_ACK, RESPONSE_END, RESPONSE_FAILED
};

struct AsyncClient {
    std::deque<size_t> accept;
    std::string bytes;
    size_t window = 8192;
    size_t addCalls = 0, sendCalls = 0;
    size_t acknowledged = 0;
    bool closed = false, sendSucceeds = true;
    size_t space() const { return window; }
    size_t add(const char* data, size_t length) {
        ++addCalls;
        size_t count = std::min(length, window);
        if (!accept.empty()) { count = std::min(count, accept.front()); accept.pop_front(); }
        bytes.append(data, count);
        return count;
    }
    bool send() { ++sendCalls; return sendSucceeds; }
    void close() { closed = true; }
};

class AsyncWebServerRequest {
public:
    explicit AsyncWebServerRequest(AsyncClient& client) : m_client(client) {}
    AsyncClient* client() { return &m_client; }
    uint8_t version() const { return 1; }
private:
    AsyncClient& m_client;
};

class AsyncWebServerResponse {
protected:
    int _code = 200;
    String _contentType;
    size_t _contentLength = 0, _headLength = 0, _sentLength = 0;
    size_t _ackedLength = 0, _writtenLength = 0;
    bool _sendContentLength = true, _chunked = false;
    WebResponseState _state = RESPONSE_SETUP;
public:
    enum class HeaderFault { None, Add, Assemble };
    HeaderFault headerFault = HeaderFault::None;
    virtual ~AsyncWebServerResponse() = default;
    bool addHeader(const char*, const char*, bool = true) {
        if (headerFault == HeaderFault::Add) throw std::bad_alloc();
        return true;
    }
    void _assembleHead(String& output, uint8_t) {
        if (headerFault == HeaderFault::Assemble) throw std::bad_alloc();
        output = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(_contentLength) +
                 "\r\nConnection: close\r\n\r\n";
        _headLength = output.size();
    }
    virtual bool _sourceValid() const { return false; }
    virtual bool _finished() const { return _state > RESPONSE_WAIT_ACK; }
    virtual bool _failed() const { return _state == RESPONSE_FAILED; }
    virtual void _respond(AsyncWebServerRequest*) {}
    virtual size_t _ack(AsyncWebServerRequest*, size_t, uint32_t) { return 0; }
};
