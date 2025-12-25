#ifndef CELLS_NAMED_RANGES_H_
#define CELLS_NAMED_RANGES_H_

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "core/cells/types.h"

namespace cells {

// Scope of a named range
enum class NamedRangeScope : uint8_t {
    WORKBOOK,  // Global - accessible from any sheet
    SHEET      // Local - only accessible within the defining sheet, can shadow workbook scope
};

// A named range can refer to either:
// - A single cell (by UUID)
// - A range (two corner cell UUIDs)
// - A column (axis UUID)
// - A row (axis UUID)
// - A column range (two axis UUIDs)
// - A row range (two axis UUIDs)
struct NamedRangeTarget {
    enum class Type : uint8_t { CELL, RANGE, COLUMN, ROW, COLUMN_RANGE, ROW_RANGE };

    Type type;
    ID id1;       // Cell ID, or first corner cell ID, or column/row ID
    ID id2;       // Second corner cell ID for ranges, or end column/row for axis ranges
    ID sheetId;   // Sheet containing the target (null for workbook-scoped absolute refs)

    // Constructors for different target types
    static NamedRangeTarget cell(const ID& cellId, const ID& sheetId = ID()) {
        return {Type::CELL, cellId, ID(), sheetId};
    }
    static NamedRangeTarget range(const ID& topLeft, const ID& bottomRight, const ID& sheetId = ID()) {
        return {Type::RANGE, topLeft, bottomRight, sheetId};
    }
    static NamedRangeTarget column(const ID& colId, const ID& sheetId = ID()) {
        return {Type::COLUMN, colId, ID(), sheetId};
    }
    static NamedRangeTarget row(const ID& rowId, const ID& sheetId = ID()) {
        return {Type::ROW, rowId, ID(), sheetId};
    }
    static NamedRangeTarget columnRange(const ID& startCol, const ID& endCol, const ID& sheetId = ID()) {
        return {Type::COLUMN_RANGE, startCol, endCol, sheetId};
    }
    static NamedRangeTarget rowRange(const ID& startRow, const ID& endRow, const ID& sheetId = ID()) {
        return {Type::ROW_RANGE, startRow, endRow, sheetId};
    }
};

// A named range definition
struct NamedRange {
    std::string name;         // Name of the range (e.g., "TotalSales")
    NamedRangeScope scope;    // Workbook or Sheet scope
    ID scopeSheetId;          // For SHEET scope, which sheet owns this name (null for WORKBOOK)
    NamedRangeTarget target;  // What the name refers to
};

// Registry for named ranges
// Supports both workbook-scoped (global) and sheet-scoped (local) names
// Sheet-scoped names shadow workbook-scoped names when resolving
class NamedRangeRegistry {
public:
    NamedRangeRegistry() = default;

    // Define a workbook-scoped named range (global)
    // Returns false if a workbook-scoped name with this name already exists
    bool defineWorkbook(const std::string& name, const NamedRangeTarget& target);

    // Define a sheet-scoped named range (local to a sheet)
    // Returns false if a sheet-scoped name with this name already exists for this sheet
    bool defineSheet(const std::string& name, const ID& sheetId, const NamedRangeTarget& target);

    // Resolve a name from the perspective of a given sheet
    // Returns nullptr if not found
    // Sheet-scoped names take precedence over workbook-scoped names
    const NamedRange* resolve(const std::string& name, const ID& currentSheetId) const;

    // Remove a workbook-scoped named range
    // Returns false if not found
    bool removeWorkbook(const std::string& name);

    // Remove a sheet-scoped named range
    // Returns false if not found
    bool removeSheet(const std::string& name, const ID& sheetId);

    // Remove all named ranges for a sheet (e.g., when deleting a sheet)
    void removeAllForSheet(const ID& sheetId);

    // Get all named ranges
    [[nodiscard]] std::vector<const NamedRange*> getAll() const;

    // Get all workbook-scoped named ranges
    [[nodiscard]] std::vector<const NamedRange*> getWorkbookScoped() const;

    // Get all sheet-scoped named ranges for a specific sheet
    [[nodiscard]] std::vector<const NamedRange*> getSheetScoped(const ID& sheetId) const;

    // Clear all named ranges
    void clear();

    // Check if a name is valid (alphanumeric, starts with letter or underscore)
    static bool isValidName(const std::string& name);

private:
    // Key for sheet-scoped names: "sheetId:name"
    static std::string makeSheetKey(const ID& sheetId, const std::string& name);

    // Workbook-scoped named ranges (name -> NamedRange)
    std::unordered_map<std::string, NamedRange> _workbookScoped;

    // Sheet-scoped named ranges (sheetId:name -> NamedRange)
    std::unordered_map<std::string, NamedRange> _sheetScoped;
};

}  // namespace cells

#endif  // CELLS_NAMED_RANGES_H_
