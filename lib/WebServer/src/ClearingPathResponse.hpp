#pragma once
#include <ClearingPatternGen.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include "KnownLengthResponse.hpp"

// The clearing sweep's own (theta, rho) points as little-endian float32
// pairs, for the dashboard to draw. Points come straight from the generator,
// so the preview cannot drift from what the table runs; the body is walked
// once up front to learn its length and again while sending.
class ClearingPathResponse : public KnownLengthResponse {
    // Drops points the drawing does not need: the browser interpolates in
    // polar space like the planner, so only sharp detail needs every point.
    class Sampler {
    public:
        Sampler(ClearingPattern pattern, float maxRho) : m_gen(pattern, maxRho) {}
        bool next(PolarCord_t& out) {
            for (PolarCord_t p = m_gen.getNextPos(); !p.isNan(); p = m_gen.getNextPos()) {
                const bool keep = !m_havePoint ||
                    std::fabs(p.theta - m_last.theta) >= 0.059 ||
                    std::fabs(p.rho - m_last.rho) >= 4.0;
                m_pending = p;
                m_havePending = true;
                if (keep) return emit(out);
            }
            // Always end on the generator's final point.
            return m_havePending && emit(out);
        }
    private:
        bool emit(PolarCord_t& out) {
            out = m_last = m_pending;
            m_havePoint = true;
            m_havePending = false;
            return true;
        }
        ClearingPatternGen m_gen;
        PolarCord_t m_last{0, 0}, m_pending{0, 0};
        bool m_havePoint = false, m_havePending = false;
    };

public:
    static constexpr size_t kPointBytes = 2 * sizeof(float);

    ClearingPathResponse(ClearingPattern pattern, float maxRho) : m_sampler(pattern, maxRho) {
        size_t points = 0;
        Sampler counter(pattern, maxRho);
        for (PolarCord_t p; counter.next(p);) ++points;
        _code = 200;
        _contentType = "application/octet-stream";
        _contentLength = points * kPointBytes;
        _sendContentLength = true;
        _chunked = false;
    }

    // The library's default reports no source and answers 500 instead.
    bool _sourceValid() const override { return true; }

    void _addResponseHeaders() override {
        addHeader("Cache-Control", "max-age=3600", false);
    }

    size_t _fillBuffer(uint8_t* data, size_t length) override {
        size_t written = 0;
        while (written < length) {
            if (m_carryOffset == m_carryLength) {
                PolarCord_t p;
                if (!m_sampler.next(p)) break;
                const float pair[2] = {static_cast<float>(p.theta), static_cast<float>(p.rho)};
                std::memcpy(m_carry, pair, kPointBytes);  // ESP32 and browsers are little-endian
                m_carryOffset = 0;
                m_carryLength = kPointBytes;
            }
            const size_t count = std::min(length - written, m_carryLength - m_carryOffset);
            std::memcpy(data + written, m_carry + m_carryOffset, count);
            m_carryOffset += count;
            written += count;
        }
        return written;
    }

private:
    Sampler m_sampler;
    uint8_t m_carry[kPointBytes];
    size_t m_carryOffset = 0, m_carryLength = 0;
};
