#ifndef CELLS_OPLOG_H_
#define CELLS_OPLOG_H_

#include <unordered_map>
#include <vector>

#include "core/cells/operation.h"
#include "core/cells/types.h"

namespace cells {

// OpLog is an append-only log of operations for CRDT synchronization.
// Operations are stored in HLC order and indexed by entity ID for efficient lookup.
struct OpLog {
    OpLog();

    // Add an operation to the log.
    // Returns true if added, false if duplicate (same HLC already exists).
    bool addOperation(const Operation& op);

    // Get all operations since a given HLC (exclusive).
    // Returns operations in HLC order.
    [[nodiscard]] std::vector<Operation> getOperationsSince(const HLC& since) const;

    // Get all operations for a specific entity ID.
    // Returns operations in HLC order.
    [[nodiscard]] std::vector<Operation> getOperationsForEntity(const ID& entity_id) const;

    // Get the most recent operation for an entity.
    // Returns null operation if no operations exist for the entity.
    [[nodiscard]] Operation getLatestOperationForEntity(const ID& entity_id) const;

    // Get all operations in HLC order.
    [[nodiscard]] const std::vector<Operation>& getAllOperations() const;

    // Get the current (highest) HLC in the log.
    // Returns zero HLC if log is empty.
    [[nodiscard]] HLC getCurrentHLC() const;

    // Check if an operation with the given HLC already exists.
    [[nodiscard]] bool hasOperation(const HLC& hlc) const;

    // Get the number of operations in the log.
    [[nodiscard]] size_t size() const;

    // Check if the log is empty.
    [[nodiscard]] bool empty() const;

    // Clear all operations (mainly for testing).
    void clear();

private:
    // All operations in HLC order
    std::vector<Operation> _operations;

    // Index: entity ID -> indices into _operations
    std::unordered_map<ID, std::vector<size_t>> _by_entity;

    // Set of HLC strings for deduplication
    std::unordered_map<std::string, size_t> _hlc_index;

    // Find insertion point to maintain sorted order
    size_t findInsertionPoint(const HLC& hlc) const;
};

}  // namespace cells

#endif  // CELLS_OPLOG_H_
