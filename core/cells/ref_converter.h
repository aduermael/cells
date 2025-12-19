#ifndef CELLS_REF_CONVERTER_H_
#define CELLS_REF_CONVERTER_H_

#include <cstddef>

#include <string>
#include <unordered_map>
#include <vector>

#include "core/cells/model.h"
#include "core/cells/types.h"

namespace cells {

// Reference types
enum class ReferenceType {
    RELATIVE,  // A1 - both column and row are relative
    ABSOLUTE,  // $A$1 - both column and row are absolute
    COL_ABS,   // $A1 - column absolute, row relative
    ROW_ABS,   // A$1 - column relative, row absolute
};

// Parsed cell reference
struct CellRef {
    std::string colId;       // UUID or empty if not resolved
    std::string rowId;       // UUID or empty if not resolved
    size_t colIndex{0};      // 0-based column index
    size_t rowIndex{0};      // 0-based row index
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
class RefConverter {
public:
    RefConverter() = default;

    // Initialize with sheet context for lookups
    // Must be called before conversion if using UUID refs
    void setContext(const Sheet& sheet);

    // Set context from ordered ID lists (for when you already have them computed)
    void setContext(const std::vector<ID>& columnIds, const std::vector<ID>& rowIds);

    // Clear context
    void clearContext();

    // ============================================================================
    // UUID to A1 conversion (for export)
    // ============================================================================

    // Convert a single cell reference from UUID format ($colId$rowId) to A1
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

    // Format a CellRef to UUID notation ($colId$rowId)
    [[nodiscard]] std::string formatUuidRef(const CellRef& ref) const;

private:
    // Column ID to index mapping
    std::unordered_map<std::string, size_t> colIdToIndex_;
    // Row ID to index mapping
    std::unordered_map<std::string, size_t> rowIdToIndex_;
    // Index to column ID mapping
    std::vector<std::string> indexToColId_;
    // Index to row ID mapping
    std::vector<std::string> indexToRowId_;

    // Check if a character is a valid column letter
    [[nodiscard]] static bool isColumnChar(char c);

    // Check if we're at the start of a UUID ref pattern ($xxxxxxxx$xxxxxxxx)
    [[nodiscard]] static bool isUuidRefStart(const std::string& formula, size_t pos);

    // Check if we're at the start of an A1 ref pattern
    [[nodiscard]] static bool isA1RefStart(const std::string& formula, size_t pos);

    // Extract UUID ref at position, returns length consumed (0 if not valid)
    [[nodiscard]] static size_t extractUuidRef(const std::string& formula, size_t pos,
                                               std::string& colId, std::string& rowId);

    // Extract A1 ref at position, returns length consumed (0 if not valid)
    [[nodiscard]] static size_t extractA1Ref(const std::string& formula, size_t pos, CellRef& ref);
};

}  // namespace cells

#endif  // CELLS_REF_CONVERTER_H_
