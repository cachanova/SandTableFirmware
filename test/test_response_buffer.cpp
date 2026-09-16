#include "ResponseBuffer.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
static size_t allocations = 0, releases = 0, failAfter = SIZE_MAX;
static void* allocate(size_t size) {
    if (allocations >= failAfter) return nullptr;
    ++allocations;
    return std::malloc(size);
}
static void release(void* value) { ++releases; std::free(value); }
int main() {
    {
        ResponseBuffer buffer(65536, allocate, release);
        std::string expected;
        for (size_t i = 0; i < 20000; ++i) {
            uint8_t c = static_cast<uint8_t>(i % 251);
            expected.push_back(c);
            assert(buffer.append(&c, 1));
        }
        assert(allocations == 20); // no reallocations or copying prior output
        assert(buffer.size() == expected.size());
        std::string actual;
        uint8_t bytes[1460];
        for (size_t n; (n = buffer.read(bytes, sizeof bytes));) actual.append(reinterpret_cast<char*>(bytes), n);
        assert(actual == expected);
        assert(buffer.size() == 0 && allocations == releases);
    }
    {
        failAfter = allocations + 1;
        ResponseBuffer buffer(65536, allocate, release);
        std::vector<uint8_t> bytes(3000, 42);
        assert(!buffer.append(bytes.data(), bytes.size()));
        assert(buffer.failed() && buffer.size() == 0);
        assert(!buffer.append(bytes.data(), 1));
        assert(buffer.read(bytes.data(), bytes.size()) == 0);
        assert(allocations == releases); // failed partial response is discarded
    }
    {
        failAfter = SIZE_MAX;
        ResponseBuffer buffer(16, allocate, release);
        const uint8_t bytes[17]{};
        assert(buffer.append(bytes, 16));
        assert(!buffer.append(bytes, 1));
        assert(buffer.failed() && buffer.size() == 0 && allocations == releases);
    }
    {
        ResponseBuffer buffer(65536, allocate, release);
        std::vector<uint8_t> bytes(2048, 7);
        assert(buffer.append(bytes.data(), bytes.size()));
        assert(buffer.read(bytes.data(), 1) == 1);
        // Destruction after client cancellation frees remaining partial blocks.
    }
    assert(allocations == releases);
    std::cout << "PASS: fragmented output, bounded allocations, OOM cleanup, cancellation cleanup\n";
}
