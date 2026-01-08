// =============================================================================
// ID Generation
// =============================================================================
//
// Generates cryptographically random 8-character base62 identifiers (UUIDs).
// These IDs are the primary key for all entities: cells, columns, rows, sheets.
//
// Key responsibilities:
// - Generate collision-resistant random IDs (62^8 = 218 trillion combinations)
// - Validate ID format (8 chars, base62 alphabet only)
// - Provide thread-safe generation via thread-local RNG
//
// Design notes:
// - Uses rejection sampling to avoid modulo bias in random generation
// - Base62 alphabet: 0-9, A-Z, a-z (URL-safe, case-sensitive)
// - IDs are immutable once created; no sequential/predictable patterns
// - The ID struct itself is defined in types.h; this module provides generation
//
// Dependencies: types.h
// Used by: crdt.cc (creating new entities), file importers, all entity creation
//
// =============================================================================

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
