// =============================================================================
// CRDT Operations
// =============================================================================
//
// Defines the Operation struct and OpType enum for CRDT-based collaborative
// editing. Every workbook mutation is represented as an immutable operation
// that can be applied, serialized, and synchronized across peers.
//
// Key responsibilities:
// - Define all operation types (cell, column, row, sheet, format)
// - Provide serialization to/from string (file format) and JSON (network)
// - Support operation ordering via HLC timestamp comparison
//
// Operation categories:
// - Cell: SET_VALUE, CLEAR, SET_STYLE, SET_FORMAT
// - Column: INSERT, DELETE, MOVE, RESIZE, RENAME
// - Row: INSERT, DELETE, MOVE, RESIZE (no rename)
// - Sheet: CREATE, DELETE, RENAME
// - Format: DEFINE (custom number formats)
//
// Serialization format: "wall.logical.node OP_TYPE target_id payload"
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
// Each operation modifies a specific entity (cell, axis, or sheet).
enum class OpType : uint8_t {
    // Cell operations
    CELL_SET_VALUE = 0,   // Set cell value (number, string, boolean, formula)
    CELL_CLEAR = 1,       // Clear cell contents
    CELL_SET_STYLE = 2,   // Set cell style properties
    CELL_SET_FORMAT = 3,  // Set cell number format

    // Column operations (preferred over DIM_* operations)
    COL_INSERT = 10,  // Insert new column
    COL_DELETE = 11,  // Delete column
    COL_MOVE = 12,    // Move column to new position
    COL_RESIZE = 13,  // Resize column width
    COL_RENAME = 14,  // Rename column (set name like "A", "Revenue", etc.)

    // Row operations
    ROW_INSERT = 15,  // Insert new row
    ROW_DELETE = 16,  // Delete row
    ROW_MOVE = 17,    // Move row to new position
    ROW_RESIZE = 18,  // Resize row height
    // Note: ROW_RENAME intentionally omitted - rows cannot be renamed

    // Sheet operations
    SHEET_CREATE = 20,  // Create new sheet
    SHEET_DELETE = 21,  // Delete sheet
    SHEET_RENAME = 22,  // Rename sheet

    // Workbook operations
    WORKBOOK_RENAME = 30,  // Rename workbook

    // Format operations
    FORMAT_DEFINE = 40,  // Define a custom number format

    // Legacy operations (deprecated, kept for backwards compatibility parsing)
    // These map to COL_* or ROW_* based on isCol payload field
    DIM_INSERT_AXIS = 100,  // Use COL_INSERT or ROW_INSERT instead
    DIM_DELETE_AXIS = 101,  // Use COL_DELETE or ROW_DELETE instead
    DIM_MOVE_AXIS = 102,    // Use COL_MOVE or ROW_MOVE instead
    DIM_RESIZE_AXIS = 103,  // Use COL_RESIZE or ROW_RESIZE instead
    DIM_RENAME_AXIS = 104,  // Use COL_RENAME instead (rows cannot be renamed)
};

// Convert OpType to string for serialization
const char* opTypeToString(OpType type);

// Convert string to OpType for deserialization
// Returns CELL_SET_VALUE as default if not recognized
OpType stringToOpType(const std::string& str);

// Operation represents a single CRDT operation on the workbook.
// Operations are immutable and identified by their HLC timestamp.
// Format for serialization: "wall.logical.node OP_TYPE target_id payload"
struct Operation {
    HLC hlc;              // Hybrid logical clock timestamp (unique identifier)
    OpType type;          // Type of operation
    ID target_id;         // ID of entity being modified (cell, axis, or sheet)
    std::string payload;  // JSON payload with operation-specific data

    // Default constructor
    Operation();

    // Construct with all fields
    Operation(const HLC& hlc, OpType type, const ID& target, std::string payload);

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
