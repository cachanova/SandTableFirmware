/* g++ -std=c++17 -Wall -Wextra -Werror -Itest/support/known_length_response -Itest/support/clearing
   -Ilib/WebServer/src -Ilib/PolarControl/src test/test_clearing_path_response.cpp -o /tmp/test_clearing_path_response */
#include "ClearingPatternGen.cpp"
#include "ClearingPathResponse.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

struct Probe : ClearingPathResponse {
    using ClearingPathResponse::ClearingPathResponse;
    size_t contentLength() const { return _contentLength; }
};

// The preview stream must deliver exactly its advertised length however TCP
// splits it, and trace the same sweep the table runs.
int main() {
    for (int p = SPIRAL_OUTWARD; p <= PETAL_FLOWER; ++p) {
        const auto pattern = static_cast<ClearingPattern>(p);
        Probe response(pattern, 425.0f);
        const size_t length = response.contentLength();
        assert(length > 0 && length % ClearingPathResponse::kPointBytes == 0);
        assert(length < 64 * 1024);  // keep previews small over Wi-Fi

        std::vector<uint8_t> body;
        uint8_t chunk[1024];
        for (size_t want = 7; body.size() < length; want = want * 3 % 1021 + 1) {
            const size_t count = response._fillBuffer(chunk, std::min(want, length - body.size()));
            assert(count > 0);
            body.insert(body.end(), chunk, chunk + count);
        }
        assert(response._fillBuffer(chunk, sizeof(chunk)) == 0);

        std::vector<float> values(body.size() / sizeof(float));
        std::memcpy(values.data(), body.data(), body.size());
        ClearingPatternGen gen(pattern, 425.0f);
        PolarCord_t first = gen.getNextPos(), last = first;
        for (PolarCord_t q = first; !q.isNan(); q = gen.getNextPos()) last = q;
        assert(values[0] == static_cast<float>(first.theta) && values[1] == static_cast<float>(first.rho));
        assert(values[values.size() - 2] == static_cast<float>(last.theta) &&
               values.back() == static_cast<float>(last.rho));
        std::printf("%-20s %zu bytes\n", ClearingPatternGen::displayName(pattern), length);
    }
    std::printf("PASS: clearing previews stream their exact length and the generator's own endpoints\n");
}
