// =============================================================================
// Format Registry - Content-Addressed Format Storage with Reference Counting
// =============================================================================
//
// Manages custom number format definitions with:
// - Deduplication via format code (identical codes share one ID)
// - Reference counting for formats used by cells, ranges, and axes
// - Garbage collection of unreferenced formats
//
// This mirrors StyleRegistry but for format codes (strings) instead of
// CellStyle objects. Since format codes are simple strings, deduplication
// is done via exact string matching rather than content hashing.
//
// Usage:
// - registerFormat(formatId, code) - register with specific ID (for CRDT replay)
// - findOrRegisterFormat(code) - returns existing ID if duplicate, or creates new
// - addRef(formatId) - increment reference count when format is applied
// - release(formatId) - decrement count, garbage collect if hits 0
//
// =============================================================================

#ifndef CELLS_FORMAT_REGISTRY_H_
#define CELLS_FORMAT_REGISTRY_H_

#include <string>
#include <unordered_map>

#include "core/cells/types.h"

namespace cells {

class FormatRegistry {
public:
    FormatRegistry() = default;
    ~FormatRegistry() = default;

    // Non-copyable
    FormatRegistry(const FormatRegistry&) = delete;
    FormatRegistry& operator=(const FormatRegistry&) = delete;

    // Movable
    FormatRegistry(FormatRegistry&&) = default;
    FormatRegistry& operator=(FormatRegistry&&) = default;

    // ========================================================================
    // Core operations
    // ========================================================================

    // Register a format with a specific ID (for CRDT replay / file loading)
    // Returns true if newly added, false if ID already existed (format updated)
    bool registerFormat(const ID& formatId, const std::string& formatCode);

    // Find or register a format by code, returns the format ID
    // If an identical code exists, returns existing ID.
    // Otherwise creates a new format entry with the provided ID (or generates one).
    // If wasCreated is provided, it will be set to true if a new format was created.
    ID findOrRegisterFormat(const std::string& formatCode, const ID& proposedId = ID(),
                            bool* wasCreated = nullptr);

    // Check if a format is registered
    [[nodiscard]] bool hasFormat(const ID& formatId) const;

    // Get a format's code (returns empty string if not found)
    [[nodiscard]] std::string getFormatCode(const ID& formatId) const;

    // Get all formats (for serialization)
    [[nodiscard]] const std::unordered_map<ID, std::string, IDHash>& getFormats() const {
        return _formats;
    }

    // ========================================================================
    // Reference counting
    // ========================================================================

    // Increment reference count for a format
    // Call when applying a format to a cell, range, or axis
    void addRef(const ID& formatId);

    // Decrement reference count for a format
    // If count reaches 0, the format is garbage collected
    void release(const ID& formatId);

    // Get current reference count (for debugging/testing)
    [[nodiscard]] uint32_t getRefCount(const ID& formatId) const;

    // ========================================================================
    // Utility
    // ========================================================================

    // Clear all formats and reset state
    void clear();

    // Get count of registered formats
    [[nodiscard]] size_t size() const { return _formats.size(); }

private:
    // Primary storage: format ID → format code
    std::unordered_map<ID, std::string, IDHash> _formats;

    // Code → ID for deduplication lookup (exact string match)
    std::unordered_map<std::string, ID> _codeToId;

    // Reference counts: ID → count
    std::unordered_map<ID, uint32_t, IDHash> _refCount;
};

}  // namespace cells

#endif  // CELLS_FORMAT_REGISTRY_H_
