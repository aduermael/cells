#ifndef CELLS_ID_H_
#define CELLS_ID_H_

#include "core/cells/types.h"

namespace cells {

// Base62 character set used for ID generation
// Order: digits, uppercase, lowercase (same as in persistence.md)
constexpr const char* BASE62_CHARS =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

// Generate a new random 8-character base62 ID
// Uses rejection sampling to avoid modulo bias
// Thread-safe (uses thread-local RNG state)
ID generate_id();

// Validate that a string is a valid 8-character base62 ID
// Returns true if valid, false otherwise
bool is_valid_id(const char* str);
bool is_valid_id(const ID& id);

}  // namespace cells

#endif  // CELLS_ID_H_
