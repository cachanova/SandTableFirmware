#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// The last trusted ball position, kept in internal NVS so a boot can skip
// sensorless homing. One record carries both facts: whether the position is
// trusted and what it is. Motion starts write valid=false before the first
// step; settling writes the position with valid=true. NVS blob writes are
// atomic, so a power loss leaves either the old record or the new one.
//
// The codec is dependency free so it can be tested on the host; the NVS
// adapter below is implemented in PositionMemory.cpp for the device.
namespace PositionMemory {

constexpr uint16_t kSchemaVersion = 1;

struct Record {
    bool valid;    // false while any motion, including homing, is under way
    double theta;  // radians in the planner frame, unwrapped
    double rho;    // millimetres
};

// [schema u16 LE][valid u8][reserved u8][theta f64 LE][rho f64 LE]
constexpr size_t kEncodedSize = 2 + 1 + 1 + 8 + 8;

inline void storeDouble(uint8_t* out, double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    for (size_t i = 0; i < 8; ++i) out[i] = static_cast<uint8_t>(bits >> (8 * i));
}

inline double loadDouble(const uint8_t* in) {
    uint64_t bits = 0;
    for (size_t i = 0; i < 8; ++i) bits |= static_cast<uint64_t>(in[i]) << (8 * i);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

inline void encode(const Record& record, uint8_t out[kEncodedSize]) {
    out[0] = static_cast<uint8_t>(kSchemaVersion & 0xFFU);
    out[1] = static_cast<uint8_t>(kSchemaVersion >> 8);
    out[2] = record.valid ? 1U : 0U;
    out[3] = 0U;
    storeDouble(out + 4, record.theta);
    storeDouble(out + 12, record.rho);
}

// Returns false for a record that cannot be trusted at all: wrong size,
// another schema, or coordinates that are not finite or outside the table.
// A well-formed record with valid=false decodes successfully; the caller
// treats it as "the table was moving when power was lost".
inline bool decode(const uint8_t* bytes, size_t length, double maxRho, Record& out) {
    if (!bytes || length != kEncodedSize) return false;
    const uint16_t schema = static_cast<uint16_t>(bytes[0] | (bytes[1] << 8));
    if (schema != kSchemaVersion) return false;
    const double theta = loadDouble(bytes + 4);
    const double rho = loadDouble(bytes + 12);
    if (!std::isfinite(theta) || !std::isfinite(rho)) return false;
    if (rho < 0.0 || rho > maxRho) return false;
    out.valid = bytes[2] == 1U;
    out.theta = theta;
    out.rho = rho;
    return true;
}

// Reduce an unwrapped angle to [-pi, pi). Patterns start on the ball's
// current lap, so the turn count carries no information worth keeping, and
// dropping it keeps the planner's step counters bounded across many boots.
inline double normalizeTheta(double theta) {
    constexpr double kTwoPi = 2.0 * 3.14159265358979323846;
    double reduced = std::remainder(theta, kTwoPi);  // (-pi, pi]
    if (reduced >= kTwoPi / 2.0) reduced -= kTwoPi;
    return reduced;
}

// NVS adapter. Every method is safe to call when NVS is unavailable; it then
// reports failure and the caller falls back to homing.
class Store {
public:
    // Write the record. Returns false if NVS refused the write.
    bool save(const Record& record);
    // Read and decode the record. Returns false when none is stored or it is
    // corrupt, which the caller treats like an untrusted position.
    bool load(double maxRho, Record& out);
    // Remove any record so the next boot must home.
    bool clear();
};

}  // namespace PositionMemory
