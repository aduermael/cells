// =============================================================================
// .zcd File Serializer
// =============================================================================
//
// Serializes Workbook model to the native .zcd text format.
// Produces git-friendly output with one entity per line.
//
// Key responsibilities:
// - Output document header and custom formats
// - Serialize sheets with columns, rows, and cells
// - Handle formula serialization with UUID references
// - Serialize operation log for CRDT persistence
//
// Output format matches docs/persistence.md specification:
// - D <id> "<name>": Document header
// - F <id> "<format-code>": Custom number format
// - S <id> "<name>": Sheet header
// - C <id> <pos> [props]: Column definition
// - R <id> <pos> [props]: Row definition
// - X <id> <col> <row> <type> <value>: Cell data
//
// Dependencies: model.h, oplog.h, types.h
// Used by: bindings.cc (file saving), CLI tools
//
// =============================================================================

#ifndef CELLS_SERIALIZER_H_
#define CELLS_SERIALIZER_H_

#include <ostream>
#include <string>

#include "core/cells/model.h"
#include "core/cells/oplog.h"
#include "core/cells/types.h"

namespace cells {

// Serializer for .zcd v1 text format
//
// Produces output matching the format specification in docs/persistence.md:
//   D <id> "<name>"
//   F <id> "<format-code>"     (custom number format)
//   S <id> "<name>"
//   C <id> <position> [props...]
//   R <id> <position> [props...]
//   X <id> <col> <row> <type> <value>
//
class Serializer {
public:
    Serializer();

    // Serialize workbook to string
    [[nodiscard]] std::string serialize(const Workbook& workbook) const;

    // Serialize workbook to output stream
    void serialize(const Workbook& workbook, std::ostream& out) const;

private:
    // Serialize document header
    void serializeHeader(const Workbook& workbook, std::ostream& out) const;

    // Serialize custom number formats
    void serializeCustomFormats(const Workbook& workbook, std::ostream& out) const;

    // Serialize cell styles
    void serializeStyles(const Workbook& workbook, std::ostream& out) const;

    // Serialize a single sheet
    void serializeSheet(const Sheet& sheet, std::ostream& out) const;

    // Serialize columns section
    void serializeColumns(const Sheet& sheet, std::ostream& out) const;

    // Serialize rows section
    void serializeRows(const Sheet& sheet, std::ostream& out) const;

    // Serialize cells section
    void serializeCells(const Sheet& sheet, std::ostream& out) const;

    // Serialize a single axis (column or row)
    void serializeAxis(const Axis& axis, char prefix, std::ostream& out) const;

    // Serialize a single cell
    void serializeCell(const Cell& cell, std::ostream& out) const;

    // Serialize cell value based on type
    void serializeCellValue(const CellValue& value, const Cell& cell, std::ostream& out) const;

    // Serialize operation log section
    void serializeOpLog(const OpLog& oplog, std::ostream& out) const;

    // Serialize a single operation
    void serializeOperation(const Operation& op, std::ostream& out) const;
};

// Escape a string for the .zcd format (handles quotes, newlines, etc.)
// Returns the escaped string WITHOUT surrounding quotes
[[nodiscard]] std::string escapeString(const std::string& str);

// Convenience functions for one-shot serialization
[[nodiscard]] std::string serialize(const Workbook& workbook);
void serialize(const Workbook& workbook, std::ostream& out);

}  // namespace cells

#endif  // CELLS_SERIALIZER_H_
