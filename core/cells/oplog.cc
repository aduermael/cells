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

size_t OpLog::pruneOperationsBefore(const HLC& threshold) {
    if (_operations.empty()) {
        return 0;
    }

    // Find first operation with HLC > threshold (binary search)
    size_t keep_from = 0;
    size_t left = 0;
    size_t right = _operations.size();

    while (left < right) {
        const size_t mid = left + (right - left) / 2;
        if (_operations[mid].hlc <= threshold) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    keep_from = left;

    if (keep_from == 0) {
        return 0;  // Nothing to prune
    }

    const size_t pruned_count = keep_from;

    // Remove from HLC index and entity index for pruned operations
    for (size_t i = 0; i < keep_from; ++i) {
        const auto& op = _operations[i];
        _hlc_index.erase(op.hlc.toString());

        // Remove from entity index
        auto entity_it = _by_entity.find(op.target_id);
        if (entity_it != _by_entity.end()) {
            auto& indices = entity_it->second;
            indices.erase(std::remove(indices.begin(), indices.end(), i), indices.end());
            if (indices.empty()) {
                _by_entity.erase(entity_it);
            }
        }
    }

    // Remove pruned operations from vector
    _operations.erase(_operations.begin(), _operations.begin() + static_cast<ptrdiff_t>(keep_from));

    // Update all indices (shift down by pruned_count)
    std::unordered_map<std::string, size_t> new_hlc_index;
    for (auto& [hlc_str, idx] : _hlc_index) {
        new_hlc_index[hlc_str] = idx - pruned_count;
    }
    _hlc_index = std::move(new_hlc_index);

    for (auto& [entity, indices] : _by_entity) {
        for (auto& idx : indices) {
            idx -= pruned_count;
        }
    }

    return pruned_count;
}

size_t OpLog::pruneKeeping(size_t minToKeep) {
    if (_operations.size() <= minToKeep) {
        return 0;  // Already at or below minimum
    }

    // Calculate how many to prune
    const size_t toPrune = _operations.size() - minToKeep;

    // Get the HLC threshold (we want to prune operations with index < toPrune)
    // The threshold is the HLC of the last operation we want to prune
    const HLC threshold = _operations[toPrune - 1].hlc;

    return pruneOperationsBefore(threshold);
}

size_t OpLog::pruneBeforeKeeping(const HLC& threshold, size_t minToKeep) {
    if (_operations.empty()) {
        return 0;
    }

    // Find how many operations would be pruned by threshold
    size_t wouldPrune = 0;
    size_t left = 0;
    size_t right = _operations.size();

    while (left < right) {
        const size_t mid = left + (right - left) / 2;
        if (_operations[mid].hlc <= threshold) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    wouldPrune = left;

    if (wouldPrune == 0) {
        return 0;  // Nothing to prune
    }

    // Check if pruning would leave us below minimum
    const size_t remaining = _operations.size() - wouldPrune;
    if (remaining < minToKeep) {
        // Adjust: only prune enough to leave minToKeep
        if (_operations.size() <= minToKeep) {
            return 0;  // Can't prune anything
        }
        const size_t maxToPrune = _operations.size() - minToKeep;
        if (maxToPrune == 0) {
            return 0;
        }
        // Use the HLC just before where we need to keep
        const HLC adjustedThreshold = _operations[maxToPrune - 1].hlc;
        return pruneOperationsBefore(adjustedThreshold);
    }

    // Safe to prune up to threshold
    return pruneOperationsBefore(threshold);
}

}  // namespace cells
