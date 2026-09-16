#pragma once
#include <ESPAsyncWebServer.h>
#include <SDCard.hpp>
#include <freertos/stream_buffer.h>
#include <atomic>
#include <new>

// SD reads can take hundreds of milliseconds on older cards. AsyncTCP's
// callbacks must never wait for them: they also service every control request.
class PatternImageResponse : public AsyncAbstractResponse {
    struct State {
        State(const String& filename, size_t size) : path(filename), expected(size) {}
        ~State() { if (stream) vStreamBufferDelete(stream); }
        String path;
        size_t expected;
        StreamBufferHandle_t stream = nullptr;
        std::atomic<unsigned> references{1};
        std::atomic<bool> cancelled{false};
        std::atomic<bool> failed{false};
        static void release(State* state) {
            if (state->references.fetch_sub(1) == 1) delete state;
        }
    };
public:
    PatternImageResponse(const String& path, size_t size) {
        _code = 200;
        _contentType = "image/png";
        _contentLength = size;
        _sendContentLength = true;
        _chunked = false;
        if (ESP.getFreeHeap() < 24576) return;
        m_state = new (std::nothrow) State(path, size);
        if (!m_state) return;
        m_state->stream = xStreamBufferCreate(4096, 1);
        if (!m_state->stream) { State::release(m_state); m_state = nullptr; return; }
        m_state->references.fetch_add(1);
        if (xTaskCreatePinnedToCore(readImage, "ImageRead", 4096, m_state, 1,
                                    nullptr, 0) != pdPASS) {
            State::release(m_state);
            State::release(m_state);
            m_state = nullptr;
        }
    }
    ~PatternImageResponse() {
        if (m_state) {
            m_state->cancelled.store(true);
            State::release(m_state);
        }
    }
    bool _sourceValid() const override { return m_state && !m_state->failed.load(); }
    size_t _fillBuffer(uint8_t* data, size_t length) override {
        const size_t count = xStreamBufferReceive(m_state->stream, data, length, 0);
        return count ? count : RESPONSE_TRY_AGAIN;
    }
private:
    static void readImage(void* argument) {
        auto* state = static_cast<State*>(argument);
        {
            const uint32_t started = millis();
            uint32_t readUs = 0;
            size_t total = 0;
            File file = SD.open(state->path, FILE_READ);
            if (!file) state->failed.store(true);
            uint8_t buffer[1024];
            while (file && !state->cancelled.load()) {
                const uint32_t readStart = micros();
                const size_t count = file.read(buffer, sizeof(buffer));
                readUs += micros() - readStart;
                if (!count) break;
                total += count;
                size_t sent = 0;
                while (sent < count && !state->cancelled.load()) {
                    sent += xStreamBufferSend(state->stream, buffer + sent,
                                              count - sent, pdMS_TO_TICKS(20));
                }
                // Let pattern reads and the web logic run between SD chunks.
                vTaskDelay(1);
            }
            if (total != state->expected && !state->cancelled.load()) {
                state->failed.store(true);
            }
            LOG("Image read: %u bytes, SD %luus, wall %lums, SPI %luHz\r\n",
                static_cast<unsigned>(total), static_cast<unsigned long>(readUs),
                static_cast<unsigned long>(millis() - started),
                static_cast<unsigned long>(spiClockDivToFrequency(SPI.getClockDivider())));
        }
        State::release(state);
        vTaskDelete(nullptr);
    }
    State* m_state = nullptr;
};
