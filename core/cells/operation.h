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
    CELL_SET_VALUE = 0,  // Set cell value (number, string, boolean, formula)
    CELL_CLEAR = 1,      // Clear cell contents
    CELL_SET_STYLE = 2,  // Set cell style properties

    // Axis (column/row) operations
    DIM_INSERT_AXIS = 10,  // Insert new column or row
    DIM_DELETE_AXIS = 11,  // Delete column or row
    DIM_MOVE_AXIS = 12,    // Move column or row to new position
    DIM_RESIZE_AXIS = 13,  // Resize column width or row height
    DIM_RENAME_AXIS = 14,  // Rename column or row

    // Sheet operations
    SHEET_CREATE = 20,  // Create new sheet
    SHEET_DELETE = 21,  // Delete sheet
    SHEET_RENAME = 22,  // Rename sheet

    // Workbook operations
    WORKBOOK_RENAME = 30,  // Rename workbook
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
