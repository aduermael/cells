#include "core/cells/oplog.h"

#include <algorithm>

namespace cells {

OpLog::OpLog() = default;

bool OpLog::addOperation(const Operation& op) {
    // Check for duplicate
    const std::string hlc_str = op.hlc.toString();
    if (_hlc_index.find(hlc_str) != _hlc_index.end()) {
        return false;  // Already exists
    }

    // Find insertion point to maintain sorted order
    const size_t insert_pos = findInsertionPoint(op.hlc);

    // Insert operation
    _operations.insert(_operations.begin() + static_cast<ptrdiff_t>(insert_pos), op);

    // Update indices for operations after insertion point
    for (auto& [key, value] : _hlc_index) {
        if (value >= insert_pos) {
            value++;
        }
    }
    for (auto& [entity, indices] : _by_entity) {
        for (auto& idx : indices) {
            if (idx >= insert_pos) {
                idx++;
            }
        }
    }

    // Add to HLC index
    _hlc_index[hlc_str] = insert_pos;

    // Add to entity index
    _by_entity[op.target_id].push_back(insert_pos);

    return true;
}

std::vector<Operation> OpLog::getOperationsSince(const HLC& since) const {
    std::vector<Operation> result;

    // Find first operation with HLC > since
    for (const auto& op : _operations) {
        if (op.hlc > since) {
            result.push_back(op);
        }
    }

    return result;
}

std::vector<Operation> OpLog::getOperationsForEntity(const ID& entity_id) const {
    std::vector<Operation> result;

    auto it = _by_entity.find(entity_id);
    if (it == _by_entity.end()) {
        return result;
    }

    // Collect operations and sort by HLC
    for (const size_t idx : it->second) {
        if (idx < _operations.size()) {
            result.push_back(_operations[idx]);
        }
    }

    // Sort by HLC (indices may not be in order after insertions)
    std::sort(result.begin(), result.end(),
              [](const Operation& a, const Operation& b) { return a.hlc < b.hlc; });

    return result;
}

Operation OpLog::getLatestOperationForEntity(const ID& entity_id) const {
    auto ops = getOperationsForEntity(entity_id);
    if (ops.empty()) {
        return {};
    }
    return ops.back();
}

const std::vector<Operation>& OpLog::getAllOperations() const {
    return _operations;
}

HLC OpLog::getCurrentHLC() const {
    if (_operations.empty()) {
        return {};
    }
    return _operations.back().hlc;
}

bool OpLog::hasOperation(const HLC& hlc) const {
    return _hlc_index.find(hlc.toString()) != _hlc_index.end();
}

size_t OpLog::size() const {
    return _operations.size();
}

bool OpLog::empty() const {
    return _operations.empty();
}

void OpLog::clear() {
    _operations.clear();
    _by_entity.clear();
    _hlc_index.clear();
}

size_t OpLog::findInsertionPoint(const HLC& hlc) const {
    // Binary search for insertion point
    size_t left = 0;
    size_t right = _operations.size();

    while (left < right) {
        const size_t mid = left + (right - left) / 2;
        if (_operations[mid].hlc < hlc) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    return left;
}

}  // namespace cells
