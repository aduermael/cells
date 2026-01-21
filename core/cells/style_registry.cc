// =============================================================================
// Style Registry Implementation
// =============================================================================

#include "core/cells/style_registry.h"

#include "core/cells/id.h"
#include "core/cells/style_types.h"
#include "core/cells/types.h"

namespace cells {

ID StyleRegistry::registerStyle(const CellStyle& style, const ID& proposedId, bool* wasCreated) {
    // Check if an identical style already exists via content hash
    const size_t hash = style.hash();
    auto it = _hashToId.find(hash);

    if (it != _hashToId.end()) {
        // Found by hash - verify it's actually equal (hash collisions are possible)
        const ID& existingId = it->second;
        auto styleIt = _styles.find(existingId);
        if (styleIt != _styles.end() && styleIt->second == style) {
            // Exact match - return existing ID
            if (wasCreated) {
                *wasCreated = false;
            }
            return existingId;
        }
        // Hash collision - different style with same hash, continue to create new
    }

    // No duplicate found - create new style entry
    ID styleId = proposedId.isNull() ? generate_id() : proposedId;

    // Check if proposed ID already exists
    if (!proposedId.isNull() && _styles.count(proposedId) > 0) {
        // ID already taken, update the existing style
        _styles[proposedId] = style;
        updateHashIndex(proposedId, style);
        if (wasCreated) {
            *wasCreated = false;
        }
        return proposedId;
    }

    _styles[styleId] = style;
    updateHashIndex(styleId, style);

    // Initialize refcount to 0 (caller should call addRef when applying)
    _refCount[styleId] = 0;

    if (wasCreated) {
        *wasCreated = true;
    }
    return styleId;
}

ID StyleRegistry::findStyleByContent(const CellStyle& style) const {
    // Lookup by content hash (no registration)
    const size_t hash = style.hash();
    auto it = _hashToId.find(hash);

    if (it != _hashToId.end()) {
        // Found by hash - verify it's actually equal (hash collisions are possible)
        const ID& existingId = it->second;
        auto styleIt = _styles.find(existingId);
        if (styleIt != _styles.end() && styleIt->second == style) {
            return existingId;
        }
    }

    // Not found
    return ID();
}

bool StyleRegistry::hasStyle(const ID& styleId) const {
    return _styles.count(styleId) > 0;
}

const CellStyle* StyleRegistry::getStyle(const ID& styleId) const {
    auto it = _styles.find(styleId);
    return it != _styles.end() ? &it->second : nullptr;
}

void StyleRegistry::addRef(const ID& styleId) {
    if (styleId.isNull() || !hasStyle(styleId)) {
        return;
    }
    _refCount[styleId]++;
}

void StyleRegistry::release(const ID& styleId) {
    if (styleId.isNull()) {
        return;
    }

    auto refIt = _refCount.find(styleId);
    if (refIt == _refCount.end()) {
        return;
    }

    if (refIt->second > 0) {
        refIt->second--;
    }

    // Garbage collect if refcount hits 0
    if (refIt->second == 0) {
        auto styleIt = _styles.find(styleId);
        if (styleIt != _styles.end()) {
            removeFromHashIndex(styleId, styleIt->second);
            _styles.erase(styleIt);
        }
        _refCount.erase(refIt);
    }
}

uint32_t StyleRegistry::getRefCount(const ID& styleId) const {
    auto it = _refCount.find(styleId);
    return it != _refCount.end() ? it->second : 0;
}

ID StyleRegistry::getOrCloneForModification(const ID& styleId, const CellStyle& newStyle) {
    if (styleId.isNull() || !hasStyle(styleId)) {
        // Style doesn't exist - register the new style
        return registerStyle(newStyle);
    }

    const uint32_t refCount = getRefCount(styleId);

    if (refCount <= 1) {
        // Not shared - modify in place
        auto it = _styles.find(styleId);
        if (it != _styles.end()) {
            removeFromHashIndex(styleId, it->second);
            it->second = newStyle;
            updateHashIndex(styleId, newStyle);
        }
        return styleId;
    }

    // Shared - need to clone
    // First check if newStyle already exists (might be able to reuse existing)
    const size_t hash = newStyle.hash();
    auto hashIt = _hashToId.find(hash);
    if (hashIt != _hashToId.end()) {
        auto styleIt = _styles.find(hashIt->second);
        if (styleIt != _styles.end() && styleIt->second == newStyle) {
            // The new style already exists - use it
            return hashIt->second;
        }
    }

    // Create new style entry for the modification
    ID newId = generate_id();
    _styles[newId] = newStyle;
    updateHashIndex(newId, newStyle);
    _refCount[newId] = 0;  // Caller should addRef after getting the ID

    return newId;
}

bool StyleRegistry::registerStyleDirect(const ID& styleId, const CellStyle& style) {
    auto it = _styles.find(styleId);
    if (it != _styles.end()) {
        // Already exists - update style
        removeFromHashIndex(styleId, it->second);
        it->second = style;
        updateHashIndex(styleId, style);
        return false;
    }

    // New style
    _styles[styleId] = style;
    updateHashIndex(styleId, style);
    _refCount[styleId] = 0;
    return true;
}

void StyleRegistry::clear() {
    _styles.clear();
    _hashToId.clear();
    _refCount.clear();
}

void StyleRegistry::updateHashIndex(const ID& styleId, const CellStyle& style) {
    const size_t hash = style.hash();
    // Only add to hash index if no entry exists for this hash
    // (first style with this hash wins)
    if (_hashToId.count(hash) == 0) {
        _hashToId[hash] = styleId;
    }
}

void StyleRegistry::removeFromHashIndex(const ID& styleId, const CellStyle& style) {
    const size_t hash = style.hash();
    auto it = _hashToId.find(hash);
    if (it != _hashToId.end() && it->second == styleId) {
        _hashToId.erase(it);
        // Note: if another style has the same hash, we'd need to re-add it
        // For simplicity, we don't handle this edge case (hash collisions are rare)
    }
}

}  // namespace cells
