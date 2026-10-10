#include "PositionMemory.hpp"

#include <Preferences.h>

namespace PositionMemory {

namespace {
constexpr const char kNamespace[] = "position";
constexpr const char kKey[] = "last";
}  // namespace

bool Store::save(const Record& record) {
    uint8_t bytes[kEncodedSize];
    encode(record, bytes);
    Preferences preferences;
    if (!preferences.begin(kNamespace, false)) return false;
    // NVS skips the flash write when the stored blob is already identical.
    const size_t written = preferences.putBytes(kKey, bytes, sizeof(bytes));
    preferences.end();
    return written == sizeof(bytes);
}

bool Store::load(double maxRho, Record& out) {
    Preferences preferences;
    if (!preferences.begin(kNamespace, true)) return false;
    uint8_t bytes[kEncodedSize];
    const size_t length = preferences.isKey(kKey)
        ? preferences.getBytes(kKey, bytes, sizeof(bytes)) : 0;
    preferences.end();
    return decode(bytes, length, maxRho, out);
}

bool Store::clear() {
    Preferences preferences;
    if (!preferences.begin(kNamespace, false)) return false;
    const bool cleared = !preferences.isKey(kKey) || preferences.remove(kKey);
    preferences.end();
    return cleared;
}

}  // namespace PositionMemory
