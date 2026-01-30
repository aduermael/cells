// =============================================================================
// CRDT Operations
// =============================================================================
//
// Defines the Operation struct and OpType enum for CRDT-based collaborative
// editing. Every workbook mutation is represented as an immutable operation
// that can be applied, serialized, and synchronized across peers.
//
// Key responsibilities:
// - Define all operation types (cell, column, row, sheet, range)
// - Provide serialization to/from string (file format) and JSON (network)
// - Support operation ordering via HLC timestamp comparison
//
// Operation types follow the unified SET + DELETE pattern:
// - SET creates entity if needed, updates only provided properties
// - DELETE marks entity as deleted (can be resurrected by later SET)
//
// Categories:
// - Cell: CELL_SET, CELL_DELETE
// - Column: COL_SET, COL_DELETE
// - Row: ROW_SET, ROW_DELETE
// - Sheet: SHEET_SET, SHEET_DELETE
// - Range: RANGE_SET, RANGE_DELETE
// - Workbook: WORKBOOK_SET
// - Named Range: NAMED_RANGE_SET, NAMED_RANGE_DELETE
//
// Serialization format: "wall.logical.node OP_TYPE target_id sheetId payload"
// JSON format used for WebSocket/WebRTC transport
//
// Dependencies: hlc.h, types.h
// Used by: crdt.cc, oplog.h, sync_manager.cc, bindings.cc
//
// =============================================================================

#ifndef CELLS_OPERATION_H_
#define CELLS_OPERATION_H_

#include <cstdint>

#include <string>

#include "core/cells/hlc.h"
#include "core/cells/types.h"

namespace cells {

// Operation types for CRDT operations on the workbook.
// Uses unified SET + DELETE pattern for all entity types.
enum class OpType : uint8_t {
    // Cell operations
    CELL_SET = 0,     // Create/update cell (col, row, t, v, sty, fmt)
    CELL_DELETE = 1,  // Delete/clear cell

    // Column operations
    COL_SET = 10,     // Create/update column (pos, size, name, sty, fmt, hidden)
    COL_DELETE = 11,  // Delete column

    // Row operations
    ROW_SET = 20,     // Create/update row (pos, size, sty, fmt, hidden)
    ROW_DELETE = 21,  // Delete row

    // Sheet operations
    SHEET_SET = 30,     // Create/update sheet (name, pos)
    SHEET_DELETE = 31,  // Delete sheet

    // Workbook operations
    WORKBOOK_SET = 40,  // Update workbook properties (name)

    // Named range operations
    NAMED_RANGE_SET = 50,     // Create/update named range
    NAMED_RANGE_DELETE = 51,  // Delete named range

    // Range operations (style/format ranges)
    RANGE_SET = 60,     // Create/update range (corners, flags, sty, fmt)
    RANGE_DELETE = 61,  // Delete range
};

// Convert OpType to string for serialization
const char* opTypeToString(OpType type);

// Convert string to OpType for deserialization
// Returns CELL_SET as default if not recognized
OpType stringToOpType(const std::string& str);

// Operation represents a single CRDT operation on the workbook.
// Operations are immutable and identified by their HLC timestamp.
// Format for serialization: "wall.logical.node OP_TYPE target_id sheetId payload"
struct Operation {
    HLC hlc;              // Hybrid logical clock timestamp (unique identifier)
    OpType type;          // Type of operation
    ID target_id;         // ID of entity being modified (cell, axis, or sheet)
    ID sheetId;           // Sheet context for multi-sheet operations (null for workbook-level ops)
    std::string payload;  // JSON payload with operation-specific data

    // Default constructor
    Operation();

    // Construct without sheetId (for workbook-level operations)
    Operation(const HLC& hlc, OpType type, const ID& target, std::string payload);

    // Construct with sheetId (for sheet-specific operations)
    Operation(const HLC& hlc, OpType type, const ID& target, const ID& sheetId,
              std::string payload);

    // Check if this is a null/empty operation
    [[nodiscard]] bool isNull() const;

    // Comparison based on HLC (for ordering)
    bool operator<(const Operation& other) const;
    bool operator==(const Operation& other) const;

    // Serialize to string format for file storage
    // Format: "wall.logical.node OP_TYPE target_id payload"
    [[nodiscard]] std::string toString() const;

    // Deserialize from string format
    // Returns null operation if parsing fails
    static Operation fromString(const std::string& str);

    // Serialize to JSON format for network transport
    [[nodiscard]] std::string toJSON() const;

    // Deserialize from JSON format
    // Returns null operation if parsing fails
    static Operation fromJSON(const std::string& json);
};

}  // namespace cells

#endif  // CELLS_OPERATION_H_
