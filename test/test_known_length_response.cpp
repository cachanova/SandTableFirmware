/* g++ -std=c++17 -Wall -Wextra -Werror -Itest/support/known_length_response
   -Ilib/WebServer/src test/test_known_length_response.cpp -o /tmp/test_known_length_response */
#include "KnownLengthResponse.hpp"
#include "StaticContentResponse.hpp"
#define PROGMEM
#include "UIPages.hpp"
#undef PROGMEM
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

class SourceResponse : public KnownLengthResponse {
public:
    explicit SourceResponse(std::string body, BufferAllocator allocator = std::malloc)
        : KnownLengthResponse(allocator), source(std::move(body)) {
        _contentLength = source.size();
        _contentType = "application/octet-stream";
    }
    ~SourceResponse() override { if (destroyed) *destroyed = true; }
    bool _sourceValid() const override { return valid; }
    size_t _fillBuffer(uint8_t* data, size_t length) override {
        ++reads;
        maxRead = std::max(maxRead, length);
        if (notReady) { --notReady; return RESPONSE_TRY_AGAIN; }
        if (earlyEof) return 0;
        const size_t count = std::min(length, source.size() - cursor);
        std::memcpy(data, source.data() + cursor, count);
        cursor += count;
        return count;
    }
    size_t sent() const { return _sentLength; }
    size_t written() const { return _writtenLength; }
    std::string source;
    size_t cursor = 0, reads = 0, maxRead = 0, notReady = 0;
    bool valid = true, earlyEof = false;
    bool* destroyed = nullptr;
};

// Mirror WebRequest::_onAck's public lifecycle: finished means close NOW.
static void ack(KnownLengthResponse& response, AsyncWebServerRequest& request) {
    auto* client = request.client();
    const size_t length = client->bytes.size() - client->acknowledged;
    client->acknowledged += length;
    if (!response._finished()) response._ack(&request, length, 0);
    if (response._finished()) request.client()->close();
}
static std::string body(const AsyncClient& client) {
    const size_t marker = client.bytes.find("\r\n\r\n");
    assert(marker != std::string::npos);
    return client.bytes.substr(marker + 4);
}
static void drain(KnownLengthResponse& response, AsyncWebServerRequest& request) {
    for (unsigned tries = 0; !response._finished() && tries < 1000; ++tries) {
        const auto calls = request.client()->addCalls;
        ack(response, request);
        assert(request.client()->addCalls - calls <= 4);
    }
    assert(response._finished() && !response._failed());
}
static std::string payload(size_t length) {
    std::string value(length, '\0');
    for (size_t i = 0; i < length; ++i) value[i] = static_cast<char>(i % 251);
    return value;
}
static size_t refusedAllocations = 0;
static void* refuseBuffer(size_t length) {
    assert(length == 1024);
    ++refusedAllocations;
    return nullptr;
}

int main() {
    const size_t all = std::numeric_limits<size_t>::max();
    {
        AsyncClient client;
        AsyncWebServerRequest request(client);
        SourceResponse response(payload(10000));
        response._respond(&request);
        assert(client.bytes.size() == KnownLengthResponse::kMaxInFlight);
        const auto reads = response.reads;
        for (unsigned poll = 0; poll < 20; ++poll) response._ack(&request, 0, 0);
        assert(client.bytes.size() == KnownLengthResponse::kMaxInFlight);
        assert(response.reads == reads); // polling cannot replenish ACK credit
        client.acknowledged = 17;
        response._ack(&request, 17, 0);
        assert(client.bytes.size() == KnownLengthResponse::kMaxInFlight + 17);
        response._ack(&request, 0, 0);
        assert(client.bytes.size() == KnownLengthResponse::kMaxInFlight + 17);
        drain(response, request);
        assert(body(client) == response.source);
        std::cout << "PASS: headers and body share an ACK-bound window; polls cannot grow the TCP backlog\n";
    }
    {
        AsyncClient client;
        AsyncWebServerRequest request(client);
        SourceResponse response(payload(256), refuseBuffer);
        response._respond(&request);
        drain(response, request);
        assert(body(client) == response.source && refusedAllocations == 0);
        std::cout << "PASS: small replies use only inline staging without a heap allocation\n";
    }
    {
        AsyncClient client;
        client.accept = {all, 0, 0, all};
        AsyncWebServerRequest request(client);
        SourceResponse response(std::string(816, 'S'));
        response._respond(&request);
        assert(response.cursor == 816 && response.sent() == 0);
        assert(body(client).empty() && !response._finished() && !client.closed);
        ack(response, request);
        assert(!response._finished() && !client.closed);
        assert(response.reads == 1); // the consumed source is NOT read again
        ack(response, request);
        assert(response._finished() && client.closed);
        assert(body(client) == response.source && response.reads == 1);
        std::cout << "PASS: final add(0) retains all 816 body bytes through SDK close-on-ACK lifecycle\n";
    }
    {
        AsyncClient client;
        client.accept = {all, 37, 11, 0, all};
        AsyncWebServerRequest request(client);
        SourceResponse response(payload(816));
        response._respond(&request);
        assert(response.sent() == 37 && !response._finished());
        ack(response, request);
        assert(response.sent() == 48 && !response._finished());
        ack(response, request);
        assert(!client.closed && response.sent() == 48);
        drain(response, request);
        assert(body(client) == response.source);
        assert(response.written() == client.bytes.size());
        std::cout << "PASS: partial final writes preserve offsets and exact byte accounting\n";
    }
    {
        AsyncClient client;
        client.accept = {5, 0, 7, all, all};
        AsyncWebServerRequest request(client);
        SourceResponse response("headers survive backpressure");
        response._respond(&request);
        assert(response.reads == 0 && client.bytes == "HTTP/");
        ack(response, request);
        assert(response.reads == 0 && !client.closed);
        drain(response, request);
        assert(client.bytes.find("HTTP/1.1 200 OK\r\n") == 0);
        assert(body(client) == response.source);
        std::cout << "PASS: zero/partial header writes finish before any source read\n";
    }
    {
        AsyncClient client;
        AsyncWebServerRequest request(client);
        SourceResponse response(payload(10000));
        response.notReady = 2;
        response._respond(&request);
        assert(response.sent() == 0 && !response._finished());
        ack(response, request);
        assert(response.sent() == 0 && !client.closed);
        const auto calls = client.addCalls;
        ack(response, request);
        assert(client.addCalls - calls == 3 && response.sent() == KnownLengthResponse::kMaxInFlight);
        drain(response, request);
        assert(body(client) == response.source && response.maxRead <= 1024);
        std::cout << "PASS: TRY_AGAIN yields, later polls recover, image pumping stays bounded\n";
    }
    {
        AsyncClient client;
        client.window = 0;
        AsyncWebServerRequest request(client);
        SourceResponse response("window reopens");
        response._respond(&request);
        assert(response.reads == 0 && !response._finished());
        client.window = 9;
        drain(response, request);
        assert(body(client) == response.source);
        std::cout << "PASS: zero and tiny TCP windows preserve complete headers/body\n";
    }
    {
        AsyncClient client;
        client.accept = {all, 0};
        AsyncWebServerRequest request(client);
        bool destroyed = false;
        {
            auto response = std::make_unique<SourceResponse>(std::string(816, 'C'));
            response->destroyed = &destroyed;
            response->_respond(&request);
            assert(!response->_finished());
            client.close(); // SDK disconnect destroys response; image subclass cancels its worker.
        }
        assert(destroyed && client.closed && body(client).empty());
        std::cout << "PASS: cancellation destroys a response with unsent bytes without deferred work\n";
    }
    {
        AsyncClient client;
        AsyncWebServerRequest request(client);
        SourceResponse response("");
        response._respond(&request);
        assert(response._finished() && response.reads == 0 && body(client).empty());
        SourceResponse failed("missing source");
        failed.valid = false;
        AsyncClient invalidClient;
        AsyncWebServerRequest invalidRequest(invalidClient);
        failed._respond(&invalidRequest);
        assert(failed._failed() && !invalidClient.closed && invalidClient.rxTimeout == 1 && invalidClient.bytes.empty());
        ack(failed, invalidRequest);
        assert(invalidClient.closed);
        std::cout << "PASS: empty bodies finish after headers; failed initial responses defer closure safely\n";
    }
    {
        AsyncClient client;
        AsyncWebServerRequest request(client);
        SourceResponse response("source truncated before declared content length");
        response.earlyEof = true;
        response._respond(&request);
        assert(response._failed() && !client.closed && client.rxTimeout == 1 && response.sent() == 0);
        assert(body(client).empty());
        std::cout << "PASS: premature source EOF fails instead of reporting a successful response\n";
    }
    {
        AsyncClient client;
        client.accept = {all, 101, 17, 0, 83, 3, all, 0, 251, 11, all};
        client.sendSucceeds = false;
        AsyncWebServerRequest request(client);
        SourceResponse response(payload(3073));
        response._respond(&request);
        assert(!response._finished() && response.sent() == 101);
        ack(response, request);
        assert(!response._finished() && response.sent() == 118);
        client.sendSucceeds = true;
        drain(response, request);
        assert(body(client) == response.source && response.written() == client.bytes.size());
        std::cout << "PASS: multiple partial buffers and temporary output failure preserve exact binary framing\n";
    }
    {
        // Every page is a run of flash spans; the joined document must come
        // out byte-for-byte whatever the TCP writes chop it into.
        const std::vector<std::pair<const StaticContentResponse::Segment*, size_t>> pages = {
            {WEB_UI_PAGE, std::size(WEB_UI_PAGE)},
            {MANUAL_UI_PAGE, std::size(MANUAL_UI_PAGE)},
            {SETTINGS_UI_PAGE, std::size(SETTINGS_UI_PAGE)},
            {FILE_UI_PAGE, std::size(FILE_UI_PAGE)}
        };
        for (const auto& page : pages) {
            std::string expected;
            for (size_t i = 0; i < page.second; ++i) {
                expected.append(reinterpret_cast<const char*>(page.first[i].data), page.first[i].length);
            }
            AsyncClient client;
            client.accept = {5, 0, 7, all, 101, 17, 0, 83, 3, all, 0, 251, 11, all};
            AsyncWebServerRequest request(client);
            StaticContentResponse response("text/html", page.first, page.second);
            response._respond(&request);
            assert(!response._finished());
            drain(response, request);
            assert(body(client) == expected);
            assert(client.bytes.find("Content-Length: " + std::to_string(expected.size()) + "\r\n") != std::string::npos);
            // The shared dialog spans are spliced in, not duplicated per page.
            assert(expected.find(UI_DIALOG_CSS) != std::string::npos);
            assert(expected.find(UI_DIALOG_JS) != std::string::npos);
            assert(expected.find(UI_DIALOG_JS) > expected.find(UI_DIALOG_CSS));
        }
        std::cout << "PASS: all four actual static HTML pages survive partial headers/body byte-for-byte\n";
    }
    {
        // A segment run must not lose or repeat bytes at its seams, including
        // empty spans and reads that end mid-segment.
        const char first[] = "alpha", second[] = "", third[] = "gamma-delta";
        const StaticContentResponse::Segment spans[] = {
            {reinterpret_cast<const uint8_t*>(first), sizeof(first) - 1},
            {reinterpret_cast<const uint8_t*>(second), 0},
            {reinterpret_cast<const uint8_t*>(third), sizeof(third) - 1}
        };
        AsyncClient client;
        client.accept = {all, 1, 0, 2, 3, all};
        AsyncWebServerRequest request(client);
        StaticContentResponse response("text/plain", spans, std::size(spans));
        assert(response._sourceValid());
        response._respond(&request);
        drain(response, request);
        assert(body(client) == "alphagamma-delta");
        assert(client.bytes.find("Content-Length: 16\r\n") != std::string::npos);

        const StaticContentResponse::Segment missing[] = {
            {reinterpret_cast<const uint8_t*>(first), sizeof(first) - 1},
            {nullptr, 4}
        };
        StaticContentResponse absent("text/plain", missing, std::size(missing));
        assert(!absent._sourceValid());
        std::cout << "PASS: multi-span bodies join exactly across empty spans and split reads\n";
    }
    {
        // The body staging buffer must not inflate every response allocation
        // beyond the small contiguous blocks left in a fragmented ESP32 heap.
        static_assert(sizeof(KnownLengthResponse) < 512, "response object must retain a small inline footprint");
        AsyncClient client;
        client.accept = {all, 101, 17, 0, 83, 3, all, 0, 251, 11, all};
        AsyncWebServerRequest request(client);
        SourceResponse response(payload(10000), refuseBuffer);
        assert(refusedAllocations == 0); // staging allocation is lazy
        response.notReady = 1;
        response._respond(&request);
        assert(refusedAllocations == 1 && !response._finished());
        for (unsigned tries = 0; !response._finished() && tries < 100; ++tries) {
            const auto calls = client.addCalls, reads = response.reads, sent = response.sent();
            ack(response, request);
            assert(client.addCalls - calls <= 4 && response.reads - reads <= 4);
            assert(response.sent() - sent <= 1024);
        }
        assert(response._finished() && !response._failed());
        assert(response.maxRead <= 256 && refusedAllocations == 1);
        assert(body(client) == response.source && response.written() == client.bytes.size());
        std::cout << "PASS: refused 1 KB allocation falls back to bounded 256-byte writes with exact binary framing\n";
    }
    {
        for (const auto fault : {AsyncWebServerResponse::HeaderFault::Add,
                                 AsyncWebServerResponse::HeaderFault::Assemble}) {
            AsyncClient client;
            AsyncWebServerRequest request(client);
            SourceResponse response(payload(816));
            response.headerFault = fault;
            response._respond(&request); // must contain either allocation exception
            assert(response._failed() && !client.closed && client.rxTimeout == 1);
            assert(client.bytes.empty() && client.addCalls == 0 && response.reads == 0);
            response._ack(&request, 0, 0);
            assert(client.bytes.empty() && response.reads == 0);
        }
        AsyncClient client;
        AsyncWebServerRequest request(client);
        SourceResponse response("next request still works");
        response._respond(&request);
        drain(response, request);
        assert(body(client) == response.source);
        std::cout << "PASS: header allocation failures arm timeout without destroying the active request\n";
    }
    {
        AsyncClient client;
        AsyncWebServerRequest request(client);
        auto response = std::make_unique<SourceResponse>(payload(10000));
        bool destroyed = false;
        response->destroyed = &destroyed;
        client.onClose = [&] { response.reset(); };
        response->_respond(&request);
        response->valid = false; // SD failure or a source replaced mid-stream.
        // The actual SDK rechecks _finished() after _ack(). Synchronous close
        // inside _ack() used to free that object before this recheck (ASan UAF).
        ack(*response, request);
        assert(destroyed && !response && client.closed && client.rxTimeout == 1);
        std::cout << "PASS: mid-response failure survives SDK recheck before synchronous destruction\n";
    }
}
