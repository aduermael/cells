// =============================================================================
// Operation Log (OpLog)
// =============================================================================
//
// Append-only log of CRDT operations for synchronization and persistence.
// All operations are stored in HLC order with efficient lookup by entity ID.
//
// Key responsibilities:
// - Store operations in causally-ordered sequence
// - Deduplicate operations (same HLC = same operation)
// - Provide efficient queries: since HLC, by entity, latest for entity
// - Support garbage collection of acknowledged operations
//
// Data structures:
// - Primary: vector<Operation> sorted by HLC
// - Index: entity ID -> operation indices (for per-entity queries)
// - Dedup: HLC string -> index (for duplicate detection)
//
// Used for:
// - Initial sync: send all ops since peer's last known HLC
// - Conflict resolution: last-writer-wins based on HLC ordering
// - Undo/redo: operations can be reversed or replayed
//
// Dependencies: operation.h, types.h
// Used by: sync_manager.cc, crdt.cc, model.h (Workbook owns OpLog)
//
// =============================================================================

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

    // Prune operations with HLC <= threshold.
    // Used to garbage collect old operations that all peers have received.
    // Returns number of operations pruned.
    size_t pruneOperationsBefore(const HLC& threshold);

private:
    // All operations in HLC order
    std::vector<Operation> _operations;

    // Index: entity ID -> indices into _operations
    std::unordered_map<ID, std::vector<size_t>> _by_entity;

    // Set of HLC strings for deduplication
    std::unordered_map<std::string, size_t> _hlc_index;

    // Find insertion point to maintain sorted order
    [[nodiscard]] size_t findInsertionPoint(const HLC& hlc) const;
};

}  // namespace cells

#endif  // CELLS_OPLOG_H_
