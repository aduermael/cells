// =============================================================================
// .zcd File Parser
// =============================================================================
//
// Parses the native .zcd (Cells Document) text format into a Workbook model.
// This format is designed for git-friendly diffs with one entity per line.
//
// Key responsibilities:
// - Parse document, sheet, column, row, cell, and format definitions
// - Handle UUID-based references and shared formulas
// - Parse operation log for CRDT synchronization
// - Report detailed errors with line numbers
//
// File format (line types):
// - D <id> "<name>": Document header
// - F <id> "<format-code>": Custom number format
// - S <id> "<name>": Sheet definition
// - C <id> <pos> [props]: Column axis
// - R <id> <pos> [props]: Row axis
// - X <id> <col> <row> <type> <value>: Cell data
// - O <hlc> <op-type> <target-id> <payload>: Operation
//
// Dependencies: model.h, oplog.h, types.h
// Used by: bindings.cc (file loading), CLI tools
//
// =============================================================================

#ifndef CELLS_PARSER_H_
#define CELLS_PARSER_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/cells/model.h"
#include "core/cells/oplog.h"
#include "core/cells/types.h"

namespace cells {

// Parser error information
struct ParseError {
    int line{0};            // 1-based line number
    int column{0};          // 1-based column (0 if not applicable)
    std::string message{};  // Human-readable error message

    ParseError() = default;
    ParseError(int line, std::string msg) : line(line), message(std::move(msg)) {}
    ParseError(int line, int col, std::string msg)
        : line(line), column(col), message(std::move(msg)) {}

    [[nodiscard]] std::string toString() const;
};

// Result of parsing a .zcd file
struct ParseResult {
    std::unique_ptr<Workbook> workbook{};  // Non-null on success
    std::optional<ParseError> error{};     // Present on failure

    [[nodiscard]] bool ok() const { return workbook != nullptr && !error.has_value(); }
    [[nodiscard]] explicit operator bool() const { return ok(); }
};

// Main parser class
class Parser {
public:
    Parser();

    // Parse a .zcd file from string content
    // Returns ParseResult with workbook on success, error on failure
    ParseResult parse(const std::string& content);
    ParseResult parse(std::string_view content);

private:
    // Internal state
    int lineNum_{0};
    Workbook* workbook_{nullptr};   // Current workbook being built
    Sheet* currentSheet_{nullptr};  // Current sheet being parsed
    std::string errorMsg_;          // Error message if parsing failed

    // Shared formula tracking: subscriber cell ID -> master cell UUID string
    std::unordered_map<ID, std::string, IDHash> pendingSharedFormulas_;
    // Cell ID -> Cell* for resolving references after parsing
    std::unordered_map<ID, Cell*, IDHash> cellsByIdForResolution_;

    // Reset parser state for new parse
    void reset();

    // Resolve shared formula references after all cells are parsed
    bool resolveSharedFormulas();

    // Set error and return false
    bool setError(const std::string& message);
    bool setError(int line, const std::string& message);

    // Parse a single line (dispatches to specific parsers)
    bool parseLine(std::string_view line);

    // Line type parsers
    bool parseDocument(std::string_view line);    // D <id> "<name>"
    bool parseFormat(std::string_view line);      // F <id> "<format-code>"
    bool parseStyle(std::string_view line);       // Y <id> <json-props>
    bool parseNamedRange(std::string_view line);  // N "<name>" <scope> <target>
    bool parseSheet(std::string_view line);       // S <id> "<name>"
    bool parseSheetView(std::string_view line);   // V <properties...>
    bool parseColumn(std::string_view line);      // C <id> <pos> [props]
    bool parseRow(std::string_view line);         // R <id> <pos> [props]
    bool parseCell(std::string_view line);        // X <id> <col> <row> <type> <value>
    bool parseRange(std::string_view line);       // RG <id> <corners> <flags> [sty:<styleId>]
    bool parseOperation(std::string_view line);   // O <hlc> <op-type> <target-id> <payload>

    // Helper parsers
    static bool parseQuotedString(std::string_view input, std::string& out, size_t& consumed);
    static bool parseAxisProps(std::string_view props, Axis& axis, ID* outStyleId = nullptr,
                               ID* outFormatId = nullptr);
    // Non-static because it needs access to workbook_ for format/style storage
    bool parseCellProps(std::string_view props, Cell& cell);
    bool parseCellValue(std::string_view value, char type, CellValue& out, size_t& consumed);
};

// Convenience function for one-shot parsing
ParseResult parse(const std::string& content);
ParseResult parse(std::string_view content);

}  // namespace cells

#endif  // CELLS_PARSER_H_
