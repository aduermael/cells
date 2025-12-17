#ifndef CELLS_SERIALIZER_H_
#define CELLS_SERIALIZER_H_

#include <ostream>
#include <string>

#include "core/cells/model.h"
#include "core/cells/types.h"

namespace cells {

// Serializer for .cells v1 text format
//
// Produces output matching the format specification in docs/persistence.md:
//   #cells v1
//   D <id> "<name>"
//   S <id> "<name>"
//   #cols
//   C <id> <prev>[:<gap>] <next>[:<gap>] [props...]
//   #rows
//   R <id> <prev>[:<gap>] <next>[:<gap>] [props...]
//   #cells
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

    // Serialize a link (ID with optional gap)
    // Format: "~" for null, "<id>" or "<id>:<gap>" for non-null
    static void serializeLink(const ID& id, uint32_t gap, std::ostream& out);

    // Serialize cell value based on type
    void serializeCellValue(const CellValue& value, const Cell& cell, std::ostream& out) const;
};

// Escape a string for the .cells format (handles quotes, newlines, etc.)
// Returns the escaped string WITHOUT surrounding quotes
[[nodiscard]] std::string escapeString(const std::string& str);

// Convenience functions for one-shot serialization
[[nodiscard]] std::string serialize(const Workbook& workbook);
void serialize(const Workbook& workbook, std::ostream& out);

}  // namespace cells

#endif  // CELLS_SERIALIZER_H_
