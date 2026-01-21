// =============================================================================
// Format Registry Implementation
// =============================================================================

#include "core/cells/format_registry.h"

#include "core/cells/id.h"
#include "core/cells/types.h"

namespace cells {

bool FormatRegistry::registerFormat(const ID& formatId, const std::string& formatCode) {
    auto it = _formats.find(formatId);
    if (it != _formats.end()) {
        // Already exists - update format code and code index
        const std::string& oldCode = it->second;
        if (oldCode != formatCode) {
            // Remove old code from index
            _codeToId.erase(oldCode);
            // Update format
            it->second = formatCode;
            // Add new code to index (if not already taken by another ID)
            if (_codeToId.count(formatCode) == 0) {
                _codeToId[formatCode] = formatId;
            }
        }
        return false;
    }

    // New format
    _formats[formatId] = formatCode;
    // Add to code index if not already taken
    if (_codeToId.count(formatCode) == 0) {
        _codeToId[formatCode] = formatId;
    }
    _refCount[formatId] = 0;
    return true;
}

ID FormatRegistry::findOrRegisterFormat(const std::string& formatCode, const ID& proposedId,
                                        bool* wasCreated) {
    // Check if an identical code already exists
    auto it = _codeToId.find(formatCode);
    if (it != _codeToId.end()) {
        // Found existing format with same code
        if (wasCreated) {
            *wasCreated = false;
        }
        return it->second;
    }

    // No duplicate found - create new format entry
    ID formatId = proposedId.isNull() ? generate_id() : proposedId;

    // Check if proposed ID already exists
    if (!proposedId.isNull() && _formats.count(proposedId) > 0) {
        // ID already taken - update the existing format
        _formats[proposedId] = formatCode;
        _codeToId[formatCode] = proposedId;
        if (wasCreated) {
            *wasCreated = false;
        }
        return proposedId;
    }

    _formats[formatId] = formatCode;
    _codeToId[formatCode] = formatId;
    _refCount[formatId] = 0;

    if (wasCreated) {
        *wasCreated = true;
    }
    return formatId;
}

ID FormatRegistry::findFormatByCode(const std::string& formatCode) const {
    // Lookup only, no registration
    auto it = _codeToId.find(formatCode);
    if (it != _codeToId.end()) {
        return it->second;
    }
    return {};
}

bool FormatRegistry::hasFormat(const ID& formatId) const {
    return _formats.count(formatId) > 0;
}

std::string FormatRegistry::getFormatCode(const ID& formatId) const {
    auto it = _formats.find(formatId);
    return it != _formats.end() ? it->second : "";
}

void FormatRegistry::addRef(const ID& formatId) {
    if (formatId.isNull() || !hasFormat(formatId)) {
        return;
    }
    _refCount[formatId]++;
}

void FormatRegistry::release(const ID& formatId) {
    if (formatId.isNull()) {
        return;
    }

    auto refIt = _refCount.find(formatId);
    if (refIt == _refCount.end()) {
        return;
    }

    if (refIt->second > 0) {
        refIt->second--;
    }

    // Garbage collect if refcount hits 0
    if (refIt->second == 0) {
        auto formatIt = _formats.find(formatId);
        if (formatIt != _formats.end()) {
            // Remove from code index
            _codeToId.erase(formatIt->second);
            _formats.erase(formatIt);
        }
        _refCount.erase(refIt);
    }
}

uint32_t FormatRegistry::getRefCount(const ID& formatId) const {
    auto it = _refCount.find(formatId);
    return it != _refCount.end() ? it->second : 0;
}

void FormatRegistry::clear() {
    _formats.clear();
    _codeToId.clear();
    _refCount.clear();
}

}  // namespace cells
