#ifndef CELLS_REF_CONVERTER_H_
#define CELLS_REF_CONVERTER_H_

#include <cstddef>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/cells/formula_ast.h"
#include "core/cells/model.h"
#include "core/cells/types.h"

namespace cells {

// Reference types
enum class ReferenceType : std::uint8_t {
    RELATIVE,  // A1 - both column and row are relative
    ABSOLUTE,  // $A$1 - both column and row are absolute
    COL_ABS,   // $A1 - column absolute, row relative
    ROW_ABS,   // A$1 - column relative, row absolute
};

// Parsed cell reference
struct CellRef {
    std::string colId;   // UUID or empty if not resolved
    std::string rowId;   // UUID or empty if not resolved
    size_t colIndex{0};  // 0-based column index
    size_t rowIndex{0};  // 0-based row index
    ReferenceType type{ReferenceType::RELATIVE};
    bool valid{false};

    [[nodiscard]] bool isAbsoluteCol() const {
        return type == ReferenceType::ABSOLUTE || type == ReferenceType::COL_ABS;
    }
    [[nodiscard]] bool isAbsoluteRow() const {
        return type == ReferenceType::ABSOLUTE || type == ReferenceType::ROW_ABS;
    }
};

// Parsed range reference (A1:C3)
struct RangeRef {
    CellRef start;
    CellRef end;
    bool valid{false};
};

// Reference converter - converts between UUID-based and A1 notation
// Used for Excel import/export
//
// Internal format uses cell UUIDs with absolute/relative markers:
//   $$cellId  ->  $A$1  (both absolute)
//   $~cellId  ->  $A1   (col absolute, row relative)
//   ~$cellId  ->  A$1   (col relative, row absolute)
//   cellId    ->  A1    (both relative, ~~ prefix omitted)
//
// Resolution: cellId -> Cell -> (colId, rowId) -> axis positions -> A1
class RefConverter {
public:
    RefConverter() = default;

    // Initialize with sheet context for lookups
    // Must be called before conversion if using UUID refs
    void setContext(const Sheet& sheet);

    // Clear context
    void clearContext();

    // ============================================================================
    // UUID to A1 conversion (for export)
    // ============================================================================

    // Convert a single cell reference from UUID format to A1
    // Input: $$cellId, $~cellId, ~$cellId, or cellId
    // Returns empty string if conversion fails
    [[nodiscard]] std::string uuidRefToA1(const std::string& ref) const;

    // Convert an entire formula from UUID refs to A1 notation
    [[nodiscard]] std::string formulaToA1(const std::string& formula) const;

    // ============================================================================
    // A1 to UUID conversion (for import)
    // ============================================================================

    // Convert a single cell reference from A1 to UUID format
    // Returns empty string if conversion fails
    [[nodiscard]] std::string a1RefToUuid(const std::string& ref) const;

    // Convert an entire formula from A1 notation to UUID refs
    [[nodiscard]] std::string formulaToUuid(const std::string& formula) const;

    // ============================================================================
    // Utility functions
    // ============================================================================

    // Convert column index (0-based) to Excel letter (A, B, ..., Z, AA, AB, ...)
    [[nodiscard]] static std::string columnIndexToLetter(size_t index);

    // Convert Excel column letter to index (0-based)
    // Returns -1 if invalid
    [[nodiscard]] static int columnLetterToIndex(const std::string& letter);

    // Parse a cell reference string (e.g., "A1", "$B$2")
    [[nodiscard]] static CellRef parseA1Ref(const std::string& ref);

    // Parse a range reference string (e.g., "A1:C3", "$A$1:$C$3")
    [[nodiscard]] static RangeRef parseRangeRef(const std::string& ref);

    // Format a CellRef back to A1 notation
    [[nodiscard]] static std::string formatA1Ref(const CellRef& ref);

    // Format a CellRef to UUID notation using cell UUID format
    // Output: $$cellId, $~cellId, ~$cellId, or cellId based on ref.type
    [[nodiscard]] std::string formatUuidRef(const CellRef& ref) const;

    // ============================================================================
    // Formula Reference Adjustment (for fill/copy-paste)
    // ============================================================================

    // Adjust cell references in a formula by the given row and column offsets.
    // Only relative references are adjusted; absolute references ($A$1) are preserved.
    // The formula should be in A1 notation (e.g., "=A1+B2", "=$A$1+B2").
    //
    // Parameters:
    //   formula - The formula string in A1 notation (with leading '=')
    //   colOffset - Number of columns to shift relative column references
    //   rowOffset - Number of rows to shift relative row references
    //
    // Returns the adjusted formula string.
    // If a reference would become invalid (negative row/col), returns #REF! for that reference.
    //
    // Examples:
    //   adjustFormulaReferences("=A1+B2", 1, 1)     -> "=B2+C3"
    //   adjustFormulaReferences("=$A$1+B2", 1, 1)   -> "=$A$1+C3"
    //   adjustFormulaReferences("=$A1+B$2", 1, 1)   -> "=$A2+C$2"
    //   adjustFormulaReferences("=A1", -1, 0)       -> "=#REF!" (column would be negative)
    [[nodiscard]] static std::string adjustFormulaReferences(const std::string& formula,
                                                             int colOffset, int rowOffset);

    // ============================================================================
    // AST-Based Reference Adjustment (preferred over string-based)
    // ============================================================================

    // Adjust cell references in an AST by the given row and column offsets.
    // Only relative references are adjusted; absolute references are preserved.
    // This is more efficient than the string-based version as it avoids re-parsing.
    //
    // Parameters:
    //   ast - The AST to adjust (will be cloned, original is not modified)
    //   colOffset - Number of columns to shift relative column references
    //   rowOffset - Number of rows to shift relative row references
    //
    // Returns a new AST with adjusted references.
    // If a reference would become invalid (negative row/col), the node becomes an ErrorNode.
    //
    // Note: Unlike adjustFormulaReferences, this works on the column/row fields of
    // CellRefNode directly, not on the display string.
    [[nodiscard]] static std::unique_ptr<ASTNode> adjustASTReferences(const ASTNode* ast,
                                                                      int colOffset, int rowOffset);

private:
    // Column ID to position mapping
    std::unordered_map<std::string, size_t> colIdToIndex_;
    // Row ID to position mapping
    std::unordered_map<std::string, size_t> rowIdToIndex_;
    // Position to column ID mapping (for A1->UUID conversion)
    std::unordered_map<size_t, std::string> indexToColId_;
    // Position to row ID mapping (for A1->UUID conversion)
    std::unordered_map<size_t, std::string> indexToRowId_;

    // Cell ID to (colId, rowId) mapping for cell UUID format
    struct CellLocation {
        std::string colId;
        std::string rowId;
    };
    std::unordered_map<std::string, CellLocation> cellIdToLocation_;

    // (colId, rowId) to cellId mapping for reverse lookup
    std::unordered_map<std::string, std::string> locationToCellId_;

    // Build composite key for location lookup
    [[nodiscard]] static std::string makeLocationKey(const std::string& colId,
                                                     const std::string& rowId);

    // Check if a character is a valid column letter
    [[nodiscard]] static bool isColumnChar(char c);

    // Check if we're at the start of a cell UUID ref pattern
    // New format: [$$|$~|~$]cellId or bare cellId (8 alphanumeric chars)
    [[nodiscard]] static bool isCellRefStart(const std::string& formula, size_t pos);

    // Check if we're at the start of an A1 ref pattern
    [[nodiscard]] static bool isA1RefStart(const std::string& formula, size_t pos);

    // Extract cell UUID ref at position, returns length consumed (0 if not valid)
    // Sets cellId and refType based on the prefix found
    [[nodiscard]] static size_t extractCellRef(const std::string& formula, size_t pos,
                                               std::string& cellId, ReferenceType& refType);

    // Extract A1 ref at position, returns length consumed (0 if not valid)
    [[nodiscard]] static size_t extractA1Ref(const std::string& formula, size_t pos, CellRef& ref);

    // Adjust a single cell reference by the given offsets
    // Returns the adjusted A1 notation, or "#REF!" if the adjustment would be invalid
    [[nodiscard]] static std::string adjustSingleRef(const CellRef& ref, int colOffset,
                                                     int rowOffset);
};

}  // namespace cells

#endif  // CELLS_REF_CONVERTER_H_
