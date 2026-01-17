// =============================================================================
// Style Registry - Content-Addressed Style Storage with Reference Counting
// =============================================================================
//
// Manages CellStyle definitions with:
// - Deduplication via content-based hashing (identical styles share one ID)
// - Reference counting for styles used by cells and ranges
// - Garbage collection of unreferenced styles
//
// This ensures efficient memory usage and makes style comparison trivial
// (just compare IDs instead of all style properties).
//
// Usage:
// - registerStyle(style) - returns existing ID if duplicate, or creates new
// - addRef(styleId) - increment reference count when style is applied
// - release(styleId) - decrement count, garbage collect if hits 0
//
// =============================================================================

#ifndef CELLS_STYLE_REGISTRY_H_
#define CELLS_STYLE_REGISTRY_H_

#include <unordered_map>

#include "core/cells/style_types.h"
#include "core/cells/types.h"

namespace cells {

class StyleRegistry {
public:
    StyleRegistry() = default;
    ~StyleRegistry() = default;

    // Non-copyable
    StyleRegistry(const StyleRegistry&) = delete;
    StyleRegistry& operator=(const StyleRegistry&) = delete;

    // Movable
    StyleRegistry(StyleRegistry&&) = default;
    StyleRegistry& operator=(StyleRegistry&&) = default;

    // ========================================================================
    // Core operations
    // ========================================================================

    // Register a style, returns existing ID if duplicate
    // If an identical style exists (by content hash), returns existing ID.
    // Otherwise creates a new style entry with the provided ID (or generates one).
    // Returns the style ID (existing or newly assigned).
    // If wasCreated is provided, it will be set to true if a new style was created.
    ID registerStyle(const CellStyle& style, const ID& proposedId = ID(),
                     bool* wasCreated = nullptr);

    // Check if a style is registered
    [[nodiscard]] bool hasStyle(const ID& styleId) const;

    // Get a style by ID (returns nullptr if not found)
    [[nodiscard]] const CellStyle* getStyle(const ID& styleId) const;

    // Get all styles (for serialization)
    [[nodiscard]] const std::unordered_map<ID, CellStyle, IDHash>& getStyles() const {
        return _styles;
    }

    // ========================================================================
    // Reference counting
    // ========================================================================

    // Increment reference count for a style
    // Call when applying a style to a cell or range
    void addRef(const ID& styleId);

    // Decrement reference count for a style
    // If count reaches 0, the style is garbage collected
    void release(const ID& styleId);

    // Get current reference count (for debugging/testing)
    [[nodiscard]] uint32_t getRefCount(const ID& styleId) const;

    // ========================================================================
    // Style modification (copy-on-write)
    // ========================================================================

    // Get a style ID for modification
    // If the style is shared (refcount > 1), clones it first and returns new ID.
    // If not shared (refcount <= 1), modifies in place and returns same ID.
    // The newStyle replaces the current style content.
    ID getOrCloneForModification(const ID& styleId, const CellStyle& newStyle);

    // ========================================================================
    // Direct operations (for file loading / CRDT replay)
    // ========================================================================

    // Directly register a style with a specific ID (bypasses deduplication)
    // Used when loading from file or replaying CRDT ops where ID must be preserved.
    // Returns true if newly added, false if ID already existed (style updated).
    bool registerStyleDirect(const ID& styleId, const CellStyle& style);

    // Clear all styles and reset state
    void clear();

    // Get count of registered styles
    [[nodiscard]] size_t size() const { return _styles.size(); }

private:
    // Primary storage: ID → Style
    std::unordered_map<ID, CellStyle, IDHash> _styles;

    // Content hash → ID for deduplication lookup
    std::unordered_map<size_t, ID> _hashToId;

    // Reference counts: ID → count
    std::unordered_map<ID, uint32_t, IDHash> _refCount;

    // Update the hash index when style is added/removed
    void updateHashIndex(const ID& styleId, const CellStyle& style);
    void removeFromHashIndex(const ID& styleId, const CellStyle& style);
};

}  // namespace cells

#endif  // CELLS_STYLE_REGISTRY_H_
