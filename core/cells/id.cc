#include "core/cells/id.h"

#include <cstdint>
#include <cstring>

#include <random>

namespace cells {

namespace {

// Thread-local random device and engine for ID generation
// Uses std::random_device for seeding (cryptographically secure on most platforms)
thread_local std::random_device rd;
thread_local std::mt19937 rng(rd());
// MSVC rejects uniform_int_distribution over uint8_t (N4950 [rand.req.genl]/1.5).
thread_local std::uniform_int_distribution<unsigned int> byte_dist(0, 255);

// Get a random byte
uint8_t random_byte() {
    return static_cast<uint8_t>(byte_dist(rng));
}

}  // namespace

ID generate_id() {
    ID id;

    // Rejection threshold: 248 = 62 * 4
    // Values 0-247 map uniformly to 0-61 via modulo
    // Values 248-255 would cause modulo bias (extra hits for 0-7)
    constexpr uint8_t REJECTION_THRESHOLD = 248;

    for (char& c : id.data) {
        uint8_t r = 0;
        do {
            r = random_byte();
        } while (r >= REJECTION_THRESHOLD);

        c = BASE62_CHARS[r % 62];
    }

    return id;
}

bool is_valid_id(const char* str) {
    if (str == nullptr) {
        return false;
    }

    // Must be exactly ID_LENGTH characters
    const size_t len = std::strlen(str);
    if (len != ID_LENGTH) {
        return false;
    }

    // Each character must be in base62 charset
    for (size_t i = 0; i < ID_LENGTH; i++) {
        const char c = str[i];
        const bool valid =
            (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (!valid) {
            return false;
        }
    }

    return true;
}

bool is_valid_id(const ID& id) {
    // Null IDs are not valid (they represent "no ID")
    if (id.isNull()) {
        return false;
    }

    // Check each character
    for (const char c : id.data) {
        const bool valid =
            (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (!valid) {
            return false;
        }
    }

    return true;
}

}  // namespace cells
