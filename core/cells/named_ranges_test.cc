#include "core/cells/named_ranges.h"

#include <gtest/gtest.h>

#include "core/cells/id.h"

namespace cells {
namespace {

class NamedRangesTest : public ::testing::Test {
protected:
    void SetUp() override {
        sheet1 = generate_id();
        sheet2 = generate_id();
        cell1 = generate_id();
        cell2 = generate_id();
        col1 = generate_id();
        col2 = generate_id();
        row1 = generate_id();
        row2 = generate_id();
    }

    NamedRangeRegistry registry;
    ID sheet1;
    ID sheet2;
    ID cell1;
    ID cell2;
    ID col1;
    ID col2;
    ID row1;
    ID row2;
};

// ===========================================================================
// Valid name tests
// ===========================================================================

TEST_F(NamedRangesTest, IsValidName_ValidNames) {
    EXPECT_TRUE(NamedRangeRegistry::isValidName("TotalSales"));
    EXPECT_TRUE(NamedRangeRegistry::isValidName("_private"));
    EXPECT_TRUE(NamedRangeRegistry::isValidName("Data2023"));
    EXPECT_TRUE(NamedRangeRegistry::isValidName("My_Data"));
    EXPECT_TRUE(NamedRangeRegistry::isValidName("Sales.Q1"));
    EXPECT_TRUE(NamedRangeRegistry::isValidName("x"));
    EXPECT_TRUE(NamedRangeRegistry::isValidName("_"));
    EXPECT_TRUE(NamedRangeRegistry::isValidName("ABC"));
}

TEST_F(NamedRangesTest, IsValidName_InvalidNames) {
    EXPECT_FALSE(NamedRangeRegistry::isValidName(""));         // Empty
    EXPECT_FALSE(NamedRangeRegistry::isValidName("123"));      // Starts with digit
    EXPECT_FALSE(NamedRangeRegistry::isValidName("1abc"));     // Starts with digit
    EXPECT_FALSE(NamedRangeRegistry::isValidName("A1"));       // Looks like cell ref
    EXPECT_FALSE(NamedRangeRegistry::isValidName("AB123"));    // Looks like cell ref
    EXPECT_FALSE(NamedRangeRegistry::isValidName("Z99"));      // Looks like cell ref
    EXPECT_FALSE(NamedRangeRegistry::isValidName("my-data"));  // Contains hyphen
    EXPECT_FALSE(NamedRangeRegistry::isValidName("my data"));  // Contains space
    EXPECT_FALSE(NamedRangeRegistry::isValidName("data!"));    // Contains special char
}

// ===========================================================================
// Workbook scope tests
// ===========================================================================

TEST_F(NamedRangesTest, DefineWorkbook_Cell) {
    auto target = NamedRangeTarget::cell(cell1, sheet1);
    EXPECT_TRUE(registry.defineWorkbook("TotalSales", target));

    auto* result = registry.resolve("TotalSales", sheet1);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->name, "TotalSales");
    EXPECT_EQ(result->scope, NamedRangeScope::WORKBOOK);
    EXPECT_TRUE(result->scopeSheetId.isNull());
    EXPECT_EQ(result->target.type, NamedRangeTarget::Type::CELL);
    EXPECT_EQ(result->target.id1, cell1);
}

TEST_F(NamedRangesTest, DefineWorkbook_Range) {
    auto target = NamedRangeTarget::range(cell1, cell2, sheet1);
    EXPECT_TRUE(registry.defineWorkbook("DataRange", target));

    auto* result = registry.resolve("DataRange", sheet1);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->target.type, NamedRangeTarget::Type::RANGE);
    EXPECT_EQ(result->target.id1, cell1);
    EXPECT_EQ(result->target.id2, cell2);
}

TEST_F(NamedRangesTest, DefineWorkbook_Column) {
    auto target = NamedRangeTarget::column(col1, sheet1);
    EXPECT_TRUE(registry.defineWorkbook("ColumnA", target));

    auto* result = registry.resolve("ColumnA", sheet1);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->target.type, NamedRangeTarget::Type::COLUMN);
    EXPECT_EQ(result->target.id1, col1);
}

TEST_F(NamedRangesTest, DefineWorkbook_Row) {
    auto target = NamedRangeTarget::row(row1, sheet1);
    EXPECT_TRUE(registry.defineWorkbook("FirstRow", target));

    auto* result = registry.resolve("FirstRow", sheet1);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->target.type, NamedRangeTarget::Type::ROW);
    EXPECT_EQ(result->target.id1, row1);
}

TEST_F(NamedRangesTest, DefineWorkbook_ColumnRange) {
    auto target = NamedRangeTarget::columnRange(col1, col2, sheet1);
    EXPECT_TRUE(registry.defineWorkbook("ColsAtoB", target));

    auto* result = registry.resolve("ColsAtoB", sheet1);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->target.type, NamedRangeTarget::Type::COLUMN_RANGE);
    EXPECT_EQ(result->target.id1, col1);
    EXPECT_EQ(result->target.id2, col2);
}

TEST_F(NamedRangesTest, DefineWorkbook_RowRange) {
    auto target = NamedRangeTarget::rowRange(row1, row2, sheet1);
    EXPECT_TRUE(registry.defineWorkbook("Rows1to5", target));

    auto* result = registry.resolve("Rows1to5", sheet1);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->target.type, NamedRangeTarget::Type::ROW_RANGE);
    EXPECT_EQ(result->target.id1, row1);
    EXPECT_EQ(result->target.id2, row2);
}

TEST_F(NamedRangesTest, DefineWorkbook_DuplicateFails) {
    auto target = NamedRangeTarget::cell(cell1, sheet1);
    EXPECT_TRUE(registry.defineWorkbook("Sales", target));
    EXPECT_FALSE(registry.defineWorkbook("Sales", target));  // Duplicate
}

TEST_F(NamedRangesTest, DefineWorkbook_InvalidNameFails) {
    auto target = NamedRangeTarget::cell(cell1, sheet1);
    EXPECT_FALSE(registry.defineWorkbook("A1", target));   // Cell ref
    EXPECT_FALSE(registry.defineWorkbook("", target));     // Empty
    EXPECT_FALSE(registry.defineWorkbook("123", target));  // Starts with digit
}

TEST_F(NamedRangesTest, DefineWorkbook_AccessibleFromAnySheet) {
    auto target = NamedRangeTarget::cell(cell1, sheet1);
    registry.defineWorkbook("GlobalName", target);

    // Should be accessible from sheet1
    EXPECT_NE(registry.resolve("GlobalName", sheet1), nullptr);

    // Should be accessible from sheet2
    EXPECT_NE(registry.resolve("GlobalName", sheet2), nullptr);

    // Should be accessible with null sheet (workbook context)
    EXPECT_NE(registry.resolve("GlobalName", ID()), nullptr);
}

// ===========================================================================
// Sheet scope tests
// ===========================================================================

TEST_F(NamedRangesTest, DefineSheet_Cell) {
    auto target = NamedRangeTarget::cell(cell1, sheet1);
    EXPECT_TRUE(registry.defineSheet("LocalName", sheet1, target));

    auto* result = registry.resolve("LocalName", sheet1);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->name, "LocalName");
    EXPECT_EQ(result->scope, NamedRangeScope::SHEET);
    EXPECT_EQ(result->scopeSheetId, sheet1);
}

TEST_F(NamedRangesTest, DefineSheet_DuplicateInSameSheetFails) {
    auto target = NamedRangeTarget::cell(cell1, sheet1);
    EXPECT_TRUE(registry.defineSheet("Local", sheet1, target));
    EXPECT_FALSE(registry.defineSheet("Local", sheet1, target));  // Duplicate
}

TEST_F(NamedRangesTest, DefineSheet_SameNameDifferentSheetOk) {
    auto target1 = NamedRangeTarget::cell(cell1, sheet1);
    auto target2 = NamedRangeTarget::cell(cell2, sheet2);

    EXPECT_TRUE(registry.defineSheet("Local", sheet1, target1));
    EXPECT_TRUE(registry.defineSheet("Local", sheet2, target2));  // Different sheet, OK

    // Each sheet sees its own definition
    auto* r1 = registry.resolve("Local", sheet1);
    auto* r2 = registry.resolve("Local", sheet2);

    ASSERT_NE(r1, nullptr);
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(r1->target.id1, cell1);
    EXPECT_EQ(r2->target.id1, cell2);
}

TEST_F(NamedRangesTest, DefineSheet_NullSheetIdFails) {
    auto target = NamedRangeTarget::cell(cell1, sheet1);
    EXPECT_FALSE(registry.defineSheet("Local", ID(), target));
}

TEST_F(NamedRangesTest, DefineSheet_NotAccessibleFromOtherSheet) {
    auto target = NamedRangeTarget::cell(cell1, sheet1);
    registry.defineSheet("LocalOnly", sheet1, target);

    // Accessible from sheet1
    EXPECT_NE(registry.resolve("LocalOnly", sheet1), nullptr);

    // NOT accessible from sheet2
    EXPECT_EQ(registry.resolve("LocalOnly", sheet2), nullptr);
}

// ===========================================================================
// Scope shadowing tests
// ===========================================================================

TEST_F(NamedRangesTest, SheetScopeShadowsWorkbookScope) {
    auto globalTarget = NamedRangeTarget::cell(cell1, sheet1);
    auto localTarget = NamedRangeTarget::cell(cell2, sheet1);

    registry.defineWorkbook("Name", globalTarget);
    registry.defineSheet("Name", sheet1, localTarget);

    // Sheet1 sees local definition (shadows global)
    auto* r1 = registry.resolve("Name", sheet1);
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(r1->scope, NamedRangeScope::SHEET);
    EXPECT_EQ(r1->target.id1, cell2);

    // Sheet2 sees global definition (no local shadow)
    auto* r2 = registry.resolve("Name", sheet2);
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(r2->scope, NamedRangeScope::WORKBOOK);
    EXPECT_EQ(r2->target.id1, cell1);
}

// ===========================================================================
// Remove tests
// ===========================================================================

TEST_F(NamedRangesTest, RemoveWorkbook_Success) {
    auto target = NamedRangeTarget::cell(cell1, sheet1);
    registry.defineWorkbook("ToRemove", target);

    EXPECT_TRUE(registry.removeWorkbook("ToRemove"));
    EXPECT_EQ(registry.resolve("ToRemove", sheet1), nullptr);
}

TEST_F(NamedRangesTest, RemoveWorkbook_NotFoundFails) {
    EXPECT_FALSE(registry.removeWorkbook("NonExistent"));
}

TEST_F(NamedRangesTest, RemoveSheet_Success) {
    auto target = NamedRangeTarget::cell(cell1, sheet1);
    registry.defineSheet("LocalToRemove", sheet1, target);

    EXPECT_TRUE(registry.removeSheet("LocalToRemove", sheet1));
    EXPECT_EQ(registry.resolve("LocalToRemove", sheet1), nullptr);
}

TEST_F(NamedRangesTest, RemoveSheet_NotFoundFails) {
    EXPECT_FALSE(registry.removeSheet("NonExistent", sheet1));
}

TEST_F(NamedRangesTest, RemoveSheet_WrongSheetFails) {
    auto target = NamedRangeTarget::cell(cell1, sheet1);
    registry.defineSheet("Local", sheet1, target);

    // Try to remove from wrong sheet
    EXPECT_FALSE(registry.removeSheet("Local", sheet2));

    // Still exists
    EXPECT_NE(registry.resolve("Local", sheet1), nullptr);
}

TEST_F(NamedRangesTest, RemoveAllForSheet) {
    auto target = NamedRangeTarget::cell(cell1, sheet1);
    registry.defineSheet("Local1", sheet1, target);
    registry.defineSheet("Local2", sheet1, target);
    registry.defineSheet("Local3", sheet2, target);  // Different sheet
    registry.defineWorkbook("Global", target);

    registry.removeAllForSheet(sheet1);

    // Sheet1 local names removed
    EXPECT_EQ(registry.resolve("Local1", sheet1), nullptr);
    EXPECT_EQ(registry.resolve("Local2", sheet1), nullptr);

    // Sheet2 local name still exists
    EXPECT_NE(registry.resolve("Local3", sheet2), nullptr);

    // Global name still exists
    EXPECT_NE(registry.resolve("Global", sheet1), nullptr);
}

// ===========================================================================
// Get all tests
// ===========================================================================

TEST_F(NamedRangesTest, GetAll) {
    auto target = NamedRangeTarget::cell(cell1, sheet1);
    registry.defineWorkbook("Global1", target);
    registry.defineWorkbook("Global2", target);
    registry.defineSheet("Local1", sheet1, target);
    registry.defineSheet("Local2", sheet2, target);

    auto all = registry.getAll();
    EXPECT_EQ(all.size(), 4);
}

TEST_F(NamedRangesTest, GetWorkbookScoped) {
    auto target = NamedRangeTarget::cell(cell1, sheet1);
    registry.defineWorkbook("Global1", target);
    registry.defineWorkbook("Global2", target);
    registry.defineSheet("Local1", sheet1, target);

    auto workbook = registry.getWorkbookScoped();
    EXPECT_EQ(workbook.size(), 2);
}

TEST_F(NamedRangesTest, GetSheetScoped) {
    auto target = NamedRangeTarget::cell(cell1, sheet1);
    registry.defineWorkbook("Global", target);
    registry.defineSheet("Local1", sheet1, target);
    registry.defineSheet("Local2", sheet1, target);
    registry.defineSheet("Local3", sheet2, target);

    auto sheet1Scoped = registry.getSheetScoped(sheet1);
    EXPECT_EQ(sheet1Scoped.size(), 2);

    auto sheet2Scoped = registry.getSheetScoped(sheet2);
    EXPECT_EQ(sheet2Scoped.size(), 1);
}

// ===========================================================================
// Clear test
// ===========================================================================

TEST_F(NamedRangesTest, Clear) {
    auto target = NamedRangeTarget::cell(cell1, sheet1);
    registry.defineWorkbook("Global", target);
    registry.defineSheet("Local", sheet1, target);

    registry.clear();

    EXPECT_EQ(registry.resolve("Global", sheet1), nullptr);
    EXPECT_EQ(registry.resolve("Local", sheet1), nullptr);
    EXPECT_EQ(registry.getAll().size(), 0);
}

// ===========================================================================
// Resolve not found
// ===========================================================================

TEST_F(NamedRangesTest, Resolve_NotFound) {
    EXPECT_EQ(registry.resolve("NonExistent", sheet1), nullptr);
    EXPECT_EQ(registry.resolve("NonExistent", ID()), nullptr);
}

}  // namespace
}  // namespace cells
