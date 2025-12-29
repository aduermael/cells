#include "core/cells/axis_index.h"

namespace cells {

// ============================================================================
// Insert/Remove operations
// ============================================================================

bool AxisIndex::insert(const ID& axisId, size_t position, uint32_t size) {
    if (position > _tree.count()) {
        return false;
    }
    return _tree.insertAt(position, axisId, size) != nullptr;
}

bool AxisIndex::append(const ID& axisId, uint32_t size) {
    return _tree.append(axisId, size) != nullptr;
}

bool AxisIndex::remove(const ID& axisId) {
    return _tree.remove(axisId);
}

// ============================================================================
// Lookup operations
// ============================================================================

std::optional<AxisLookupResult> AxisIndex::pixelToAxis(uint32_t pixelOffset) const {
    const FindResult result = _tree.findByOffset(pixelOffset);
    if (!result.found()) {
        return std::nullopt;
    }
    const size_t position = _tree.getPosition(result.node);
    return AxisLookupResult(result.node->id, result.offsetInNode, position);
}

std::optional<uint32_t> AxisIndex::axisToPixel(const ID& axisId) const {
    const OSNode* node = _tree.find(axisId);
    if (node == nullptr) {
        return std::nullopt;
    }
    return _tree.getOffset(node);
}

std::optional<size_t> AxisIndex::getPosition(const ID& axisId) const {
    const OSNode* node = _tree.find(axisId);
    if (node == nullptr) {
        return std::nullopt;
    }
    return _tree.getPosition(node);
}

std::optional<uint32_t> AxisIndex::getSize(const ID& axisId) const {
    const OSNode* node = _tree.find(axisId);
    if (node == nullptr) {
        return std::nullopt;
    }
    return node->size;
}

std::optional<ID> AxisIndex::getAxisAt(size_t position) const {
    const OSNode* node = _tree.at(position);
    if (node == nullptr) {
        return std::nullopt;
    }
    return node->id;
}

bool AxisIndex::contains(const ID& axisId) const {
    return _tree.find(axisId) != nullptr;
}

// ============================================================================
// Modification operations
// ============================================================================

bool AxisIndex::resize(const ID& axisId, uint32_t newSize) {
    OSNode* node = _tree.find(axisId);
    if (node == nullptr) {
        return false;
    }
    _tree.updateSize(node, newSize);
    return true;
}

bool AxisIndex::move(const ID& axisId, size_t newPosition) {
    OSNode* node = _tree.find(axisId);
    if (node == nullptr) {
        return false;
    }
    return _tree.move(node, newPosition);
}

// ============================================================================
// Utility operations
// ============================================================================

uint32_t AxisIndex::totalSize() const {
    return _tree.totalSize();
}

size_t AxisIndex::count() const {
    return _tree.count();
}

bool AxisIndex::empty() const {
    return _tree.empty();
}

void AxisIndex::clear() {
    _tree.clear();
}

void AxisIndex::forEach(
    const std::function<void(const ID&, size_t, uint32_t, uint32_t)>& callback) const {
    uint32_t pixelOffset = 0;
    _tree.forEach([&callback, &pixelOffset](OSNode* node, size_t position) {
        callback(node->id, position, node->size, pixelOffset);
        pixelOffset += node->size;
    });
}

bool AxisIndex::verify() const {
    return _tree.verify();
}

}  // namespace cells
