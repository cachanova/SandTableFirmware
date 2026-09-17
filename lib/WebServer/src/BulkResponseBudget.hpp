#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace BulkResponseBudget {
inline bool isBulkPath(const char* path) {
    return std::strcmp(path, "/") == 0 || std::strcmp(path, "/settings") == 0 ||
        std::strcmp(path, "/manual") == 0 || std::strcmp(path, "/files") == 0 ||
        std::strcmp(path, "/api/pattern/image") == 0 ||
        std::strcmp(path, "/api/pattern/download") == 0;
}
inline bool canStart(uint32_t inflight, size_t free8Bit, size_t largest8Bit) {
    return inflight == 0 && free8Bit >= 24576 && largest8Bit >= 8192;
}
} // namespace BulkResponseBudget
