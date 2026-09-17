#include "JsonPersistence.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

struct StagedFile {
    std::string bytes;
    size_t limit = 65536;
    bool flushFails = false, flushed = false;
    size_t write(uint8_t value) { return write(&value, 1); }
    size_t write(const uint8_t* data, size_t length) {
        const size_t count = std::min(length, limit - bytes.size());
        bytes.append(reinterpret_cast<const char*>(data), count);
        return count;
    }
    void flush() { flushed = true; }
    int getWriteError() const { return flushed && flushFails; }
    size_t size() const { return bytes.size(); }
};
struct RefusingAllocator : ArduinoJson::Allocator {
    bool refuse = false;
    void* allocate(size_t size) override { return refuse ? nullptr : std::malloc(size); }
    void deallocate(void* value) override { std::free(value); }
    void* reallocate(void* value, size_t size) override { return refuse ? nullptr : std::realloc(value, size); }
};
int main() {
    JsonDocument doc;
    doc["speed"] = 5;
    doc["items"][0]["file"] = "Spiral7.thr";
    for (bool pretty : {false, true}) {
        StagedFile good;
        assert(writeCompleteJson(doc, good, pretty) && good.flushed);
        JsonDocument parsed;
        assert(!deserializeJson(parsed, good.bytes));
        assert(parsed["items"][0]["file"] == "Spiral7.thr");
        StagedFile shortWrite; shortWrite.limit = 8;
        assert(!writeCompleteJson(doc, shortWrite, pretty) && !shortWrite.bytes.empty());
        StagedFile flushFailure; flushFailure.flushFails = true;
        assert(!writeCompleteJson(doc, flushFailure, pretty));
        StagedFile stale; stale.bytes = "old staged data";
        assert(!writeCompleteJson(doc, stale, pretty));
    }
    RefusingAllocator allocator;
    JsonDocument incomplete(&allocator);
    incomplete["schemaVersion"] = 1;
    allocator.refuse = true;
    incomplete["large"] = std::string(4096, 'x');
    assert(incomplete.overflowed());
    StagedFile untouched;
    assert(!writeCompleteJson(incomplete, untouched));
    assert(untouched.bytes.empty() && !untouched.flushed);
    std::cout << "PASS: compact/pretty exact writes; short write, flush failure, stale temp and partial JSON refused\n";
}
