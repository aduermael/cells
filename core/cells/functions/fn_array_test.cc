#include "core/cells/functions/fn_array.h"

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unordered_set>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_eval.h"
#include "core/cells/formula_functions.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

namespace cells {
namespace {

class FnArrayTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a workbook with one sheet
        workbook = std::make_unique<Workbook>(generate_id(), "Test");
        workbook->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet1"));
        sheet = workbook->getSheetByIndex(0);

        // Create columns A-Z (positions 0-25)
        for (uint32_t i = 0; i < 26; i++) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = i;
            col->name = Sheet::positionToColumnName(i);
            colIds[i] = col->id;
            sheet->addColumn(std::move(col));
        }

        // Create rows 1-100 (positions 0-99)
        for (uint32_t i = 0; i < 100; i++) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = i;
            rowIds[i] = row->id;
            sheet->addRow(std::move(row));
        }
    }

    // Parse and evaluate a formula
    EvalResult eval(const std::string& formula) {
        FormulaParser parser(formula);
        auto ast = parser.parse();
        if (!ast || parser.hasErrors()) {
            return EvalResult::Error(CellError::VALUE);
        }

        // Resolve references
        FormulaResolver resolver(*workbook, *sheet);
        resolver.resolve(ast.get());

        // Evaluate
        std::unordered_set<ID> evaluating;
        EvalContext ctx;
        ctx.sheet = sheet;
        ctx.workbook = workbook.get();
        ctx.evaluatingCells = &evaluating;
        ctx.recursionDepth = 0;

        return evaluate(ast.get(), ctx);
    }

    // Set a cell value (number)
    Cell* setCellValue(uint32_t col, uint32_t row, double value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(value);
        return cell;
    }

    // Set a cell value (string)
    Cell* setCellString(uint32_t col, uint32_t row, const std::string& value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(value);
        return cell;
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet = nullptr;
    ID colIds[26];
    ID rowIds[100];
};

// =============================================================================
// UNIQUE Tests - Basic functionality
// =============================================================================

TEST_F(FnArrayTest, UniqueBasicSingleColumn) {
    // A1:A5 = {1, 2, 2, 3, 1}
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 2.0);
    setCellValue(0, 3, 3.0);
    setCellValue(0, 4, 1.0);

    EvalResult result = eval("=UNIQUE(A1:A5)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 3u);
    EXPECT_EQ(result.getArrayCols(), 1u);

    // Should return {1, 2, 3}
    EXPECT_TRUE(result.getArrayAt(0, 0).isNumber());
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_TRUE(result.getArrayAt(1, 0).isNumber());
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 2.0);
    EXPECT_TRUE(result.getArrayAt(2, 0).isNumber());
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 0).getNumber(), 3.0);
}

TEST_F(FnArrayTest, UniqueAllDuplicates) {
    // A1:A3 = {5, 5, 5}
    setCellValue(0, 0, 5.0);
    setCellValue(0, 1, 5.0);
    setCellValue(0, 2, 5.0);

    EvalResult result = eval("=UNIQUE(A1:A3)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 1u);
    EXPECT_EQ(result.getArrayCols(), 1u);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 5.0);
}

TEST_F(FnArrayTest, UniqueNoDuplicates) {
    // A1:A3 = {1, 2, 3}
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);

    EvalResult result = eval("=UNIQUE(A1:A3)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 3u);
    EXPECT_EQ(result.getArrayCols(), 1u);
}

TEST_F(FnArrayTest, UniqueWithStrings) {
    // A1:A5 = {"Apple", "Banana", "Apple", "Cherry", "Banana"}
    setCellString(0, 0, "Apple");
    setCellString(0, 1, "Banana");
    setCellString(0, 2, "Apple");
    setCellString(0, 3, "Cherry");
    setCellString(0, 4, "Banana");

    EvalResult result = eval("=UNIQUE(A1:A5)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 3u);
    EXPECT_EQ(result.getArrayCols(), 1u);

    EXPECT_TRUE(result.getArrayAt(0, 0).isString());
    EXPECT_EQ(result.getArrayAt(0, 0).getString(), "Apple");
    EXPECT_TRUE(result.getArrayAt(1, 0).isString());
    EXPECT_EQ(result.getArrayAt(1, 0).getString(), "Banana");
    EXPECT_TRUE(result.getArrayAt(2, 0).isString());
    EXPECT_EQ(result.getArrayAt(2, 0).getString(), "Cherry");
}

// =============================================================================
// UNIQUE Tests - exactly_once parameter
// =============================================================================

TEST_F(FnArrayTest, UniqueExactlyOnce) {
    // A1:A5 = {1, 2, 2, 3, 1}
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 2.0);
    setCellValue(0, 3, 3.0);
    setCellValue(0, 4, 1.0);

    // Only 3 appears exactly once
    // Use FALSE explicitly for by_col since parser doesn't support empty args
    EvalResult result = eval("=UNIQUE(A1:A5,FALSE,TRUE)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 1u);
    EXPECT_EQ(result.getArrayCols(), 1u);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 3.0);
}

TEST_F(FnArrayTest, UniqueExactlyOnceAllDuplicates) {
    // A1:A3 = {1, 1, 1}
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 1.0);
    setCellValue(0, 2, 1.0);

    // No values appear exactly once
    EvalResult result = eval("=UNIQUE(A1:A3,FALSE,TRUE)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 0u);  // Empty array
}

TEST_F(FnArrayTest, UniqueExactlyOnceMultiple) {
    // A1:A6 = {1, 2, 3, 3, 4, 4}
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);
    setCellValue(0, 3, 3.0);
    setCellValue(0, 4, 4.0);
    setCellValue(0, 5, 4.0);

    // 1 and 2 appear exactly once
    EvalResult result = eval("=UNIQUE(A1:A6,FALSE,TRUE)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 2u);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 2.0);
}

// =============================================================================
// UNIQUE Tests - Multi-column (rows comparison)
// =============================================================================

TEST_F(FnArrayTest, UniqueMultiColumnRows) {
    // A1:B4 = {{1, "A"}, {2, "B"}, {1, "A"}, {3, "C"}}
    setCellValue(0, 0, 1.0);
    setCellString(1, 0, "A");
    setCellValue(0, 1, 2.0);
    setCellString(1, 1, "B");
    setCellValue(0, 2, 1.0);
    setCellString(1, 2, "A");
    setCellValue(0, 3, 3.0);
    setCellString(1, 3, "C");

    EvalResult result = eval("=UNIQUE(A1:B4)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 3u);  // Unique rows: {1,A}, {2,B}, {3,C}
    EXPECT_EQ(result.getArrayCols(), 2u);

    // First row: {1, "A"}
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_EQ(result.getArrayAt(0, 1).getString(), "A");

    // Second row: {2, "B"}
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 2.0);
    EXPECT_EQ(result.getArrayAt(1, 1).getString(), "B");

    // Third row: {3, "C"}
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 0).getNumber(), 3.0);
    EXPECT_EQ(result.getArrayAt(2, 1).getString(), "C");
}

// =============================================================================
// UNIQUE Tests - by_col parameter (compare columns instead of rows)
// =============================================================================

TEST_F(FnArrayTest, UniqueByColumn) {
    // A1:C1 = {1, 2, 1} (one row, three columns)
    // by_col=TRUE should compare columns and return unique columns
    setCellValue(0, 0, 1.0);  // A1
    setCellValue(1, 0, 2.0);  // B1
    setCellValue(2, 0, 1.0);  // C1

    EvalResult result = eval("=UNIQUE(A1:C1,TRUE)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 1u);
    EXPECT_EQ(result.getArrayCols(), 2u);  // Only 2 unique columns: {1} and {2}

    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 1).getNumber(), 2.0);
}

TEST_F(FnArrayTest, UniqueByColumnMultiRow) {
    // A1:D2 = {{1, 2, 1, 3}, {A, B, A, C}}
    // Columns are: {1,A}, {2,B}, {1,A}, {3,C}
    // Unique columns: {1,A}, {2,B}, {3,C}
    setCellValue(0, 0, 1.0);
    setCellValue(1, 0, 2.0);
    setCellValue(2, 0, 1.0);
    setCellValue(3, 0, 3.0);
    setCellString(0, 1, "A");
    setCellString(1, 1, "B");
    setCellString(2, 1, "A");
    setCellString(3, 1, "C");

    EvalResult result = eval("=UNIQUE(A1:D2,TRUE)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 2u);
    EXPECT_EQ(result.getArrayCols(), 3u);  // 3 unique columns
}

// =============================================================================
// UNIQUE Tests - Error cases
// =============================================================================

TEST_F(FnArrayTest, UniqueNoArgs) {
    EvalResult result = eval("=UNIQUE()");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnArrayTest, UniqueTooManyArgs) {
    setCellValue(0, 0, 1.0);
    EvalResult result = eval("=UNIQUE(A1:A3,FALSE,FALSE,FALSE)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnArrayTest, UniqueErrorPropagation) {
    // Cell with an error should propagate
    Cell* cell = sheet->getOrCreateCellAt(colIds[0], rowIds[0]);
    cell->value = CellValue(CellError::DIV);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);

    EvalResult result = eval("=UNIQUE(A1:A3)");
    // The result should include the error as one of the unique values
    // (errors are treated as values, not propagated in array functions)
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 3u);
    EXPECT_TRUE(result.getArrayAt(0, 0).isError());
    EXPECT_EQ(result.getArrayAt(0, 0).getError(), CellError::DIV);
}

// =============================================================================
// UNIQUE Tests - Single value
// =============================================================================

TEST_F(FnArrayTest, UniqueSingleCell) {
    setCellValue(0, 0, 42.0);

    EvalResult result = eval("=UNIQUE(A1)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 1u);
    EXPECT_EQ(result.getArrayCols(), 1u);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 42.0);
}

// =============================================================================
// UNIQUE Tests - Empty cells
// =============================================================================

TEST_F(FnArrayTest, UniqueWithEmptyCells) {
    // A1=1, A2=empty, A3=1, A4=empty
    setCellValue(0, 0, 1.0);
    // A2 is empty (not set)
    setCellValue(0, 2, 1.0);
    // A4 is empty (not set)

    EvalResult result = eval("=UNIQUE(A1:A4)");
    ASSERT_TRUE(result.isArray());
    // Should have 2 unique values: 1 and empty
    EXPECT_EQ(result.getArrayRows(), 2u);
}

// =============================================================================
// UNIQUE Tests - Mixed types
// =============================================================================

TEST_F(FnArrayTest, UniqueMixedTypes) {
    // Numbers and strings that look like numbers are different
    setCellValue(0, 0, 1.0);   // Number 1
    setCellString(0, 1, "1");  // String "1"
    setCellValue(0, 2, 1.0);   // Number 1 (duplicate)
    setCellString(0, 3, "1");  // String "1" (duplicate)
    setCellValue(0, 4, 2.0);   // Number 2

    EvalResult result = eval("=UNIQUE(A1:A5)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 3u);  // {1, "1", 2}

    EXPECT_TRUE(result.getArrayAt(0, 0).isNumber());
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_TRUE(result.getArrayAt(1, 0).isString());
    EXPECT_EQ(result.getArrayAt(1, 0).getString(), "1");
    EXPECT_TRUE(result.getArrayAt(2, 0).isNumber());
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 0).getNumber(), 2.0);
}

// =============================================================================
// SORT Tests - Basic functionality
// =============================================================================

TEST_F(FnArrayTest, SortBasicAscending) {
    // A1:A5 = {3, 1, 4, 1, 5}
    setCellValue(0, 0, 3.0);
    setCellValue(0, 1, 1.0);
    setCellValue(0, 2, 4.0);
    setCellValue(0, 3, 1.0);
    setCellValue(0, 4, 5.0);

    EvalResult result = eval("=SORT(A1:A5)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 5u);
    EXPECT_EQ(result.getArrayCols(), 1u);

    // Should return {1, 1, 3, 4, 5}
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 1.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 0).getNumber(), 3.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(3, 0).getNumber(), 4.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(4, 0).getNumber(), 5.0);
}

TEST_F(FnArrayTest, SortBasicDescending) {
    // A1:A5 = {3, 1, 4, 1, 5}
    setCellValue(0, 0, 3.0);
    setCellValue(0, 1, 1.0);
    setCellValue(0, 2, 4.0);
    setCellValue(0, 3, 1.0);
    setCellValue(0, 4, 5.0);

    EvalResult result = eval("=SORT(A1:A5,1,-1)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 5u);

    // Should return {5, 4, 3, 1, 1}
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 5.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 4.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 0).getNumber(), 3.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(3, 0).getNumber(), 1.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(4, 0).getNumber(), 1.0);
}

TEST_F(FnArrayTest, SortWithStrings) {
    // A1:A4 = {"Banana", "Apple", "Cherry", "Apple"}
    setCellString(0, 0, "Banana");
    setCellString(0, 1, "Apple");
    setCellString(0, 2, "Cherry");
    setCellString(0, 3, "Apple");

    EvalResult result = eval("=SORT(A1:A4)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 4u);

    // Should return {"Apple", "Apple", "Banana", "Cherry"}
    EXPECT_EQ(result.getArrayAt(0, 0).getString(), "Apple");
    EXPECT_EQ(result.getArrayAt(1, 0).getString(), "Apple");
    EXPECT_EQ(result.getArrayAt(2, 0).getString(), "Banana");
    EXPECT_EQ(result.getArrayAt(3, 0).getString(), "Cherry");
}

// =============================================================================
// SORT Tests - Multi-column with sort_index
// =============================================================================

TEST_F(FnArrayTest, SortMultiColumnByFirstColumn) {
    // A1:B3 = {{3, "C"}, {1, "A"}, {2, "B"}}
    setCellValue(0, 0, 3.0);
    setCellString(1, 0, "C");
    setCellValue(0, 1, 1.0);
    setCellString(1, 1, "A");
    setCellValue(0, 2, 2.0);
    setCellString(1, 2, "B");

    EvalResult result = eval("=SORT(A1:B3)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 3u);
    EXPECT_EQ(result.getArrayCols(), 2u);

    // Should be sorted by first column: {1,A}, {2,B}, {3,C}
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_EQ(result.getArrayAt(0, 1).getString(), "A");
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 2.0);
    EXPECT_EQ(result.getArrayAt(1, 1).getString(), "B");
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 0).getNumber(), 3.0);
    EXPECT_EQ(result.getArrayAt(2, 1).getString(), "C");
}

TEST_F(FnArrayTest, SortMultiColumnBySecondColumn) {
    // A1:B3 = {{1, "C"}, {2, "A"}, {3, "B"}}
    setCellValue(0, 0, 1.0);
    setCellString(1, 0, "C");
    setCellValue(0, 1, 2.0);
    setCellString(1, 1, "A");
    setCellValue(0, 2, 3.0);
    setCellString(1, 2, "B");

    EvalResult result = eval("=SORT(A1:B3,2)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 3u);
    EXPECT_EQ(result.getArrayCols(), 2u);

    // Should be sorted by second column (string): {2,A}, {3,B}, {1,C}
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 2.0);
    EXPECT_EQ(result.getArrayAt(0, 1).getString(), "A");
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 3.0);
    EXPECT_EQ(result.getArrayAt(1, 1).getString(), "B");
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 0).getNumber(), 1.0);
    EXPECT_EQ(result.getArrayAt(2, 1).getString(), "C");
}

// =============================================================================
// SORT Tests - by_col parameter (sort columns by row values)
// =============================================================================

TEST_F(FnArrayTest, SortByColumnSingleRow) {
    // A1:C1 = {3, 1, 2} (one row, three columns)
    setCellValue(0, 0, 3.0);  // A1
    setCellValue(1, 0, 1.0);  // B1
    setCellValue(2, 0, 2.0);  // C1

    EvalResult result = eval("=SORT(A1:C1,1,1,TRUE)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 1u);
    EXPECT_EQ(result.getArrayCols(), 3u);

    // Should return {1, 2, 3}
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 1).getNumber(), 2.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 2).getNumber(), 3.0);
}

TEST_F(FnArrayTest, SortByColumnMultiRow) {
    // A1:D2 = {{3, 1, 2, 4}, {"C", "A", "B", "D"}}
    // Sorting by row 1 (numbers): cols should be reordered as {1,A}, {2,B}, {3,C}, {4,D}
    setCellValue(0, 0, 3.0);
    setCellValue(1, 0, 1.0);
    setCellValue(2, 0, 2.0);
    setCellValue(3, 0, 4.0);
    setCellString(0, 1, "C");
    setCellString(1, 1, "A");
    setCellString(2, 1, "B");
    setCellString(3, 1, "D");

    EvalResult result = eval("=SORT(A1:D2,1,1,TRUE)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 2u);
    EXPECT_EQ(result.getArrayCols(), 4u);

    // Row 1: {1, 2, 3, 4}
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 1).getNumber(), 2.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 2).getNumber(), 3.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 3).getNumber(), 4.0);
    // Row 2: {"A", "B", "C", "D"}
    EXPECT_EQ(result.getArrayAt(1, 0).getString(), "A");
    EXPECT_EQ(result.getArrayAt(1, 1).getString(), "B");
    EXPECT_EQ(result.getArrayAt(1, 2).getString(), "C");
    EXPECT_EQ(result.getArrayAt(1, 3).getString(), "D");
}

// =============================================================================
// SORT Tests - Error cases
// =============================================================================

TEST_F(FnArrayTest, SortNoArgs) {
    EvalResult result = eval("=SORT()");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnArrayTest, SortTooManyArgs) {
    setCellValue(0, 0, 1.0);
    EvalResult result = eval("=SORT(A1:A3,1,1,FALSE,FALSE)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnArrayTest, SortInvalidSortIndex) {
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);

    // Sort index 0 is invalid
    EvalResult result = eval("=SORT(A1:A2,0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);

    // Sort index larger than columns is invalid
    result = eval("=SORT(A1:A2,5)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnArrayTest, SortInvalidSortOrder) {
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);

    // Sort order must be 1 or -1
    EvalResult result = eval("=SORT(A1:A2,1,2)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// =============================================================================
// SORT Tests - Mixed types
// =============================================================================

TEST_F(FnArrayTest, SortMixedTypes) {
    // Numbers sort before strings in Excel
    setCellValue(0, 0, 5.0);      // Number
    setCellString(0, 1, "Apple"); // String
    setCellValue(0, 2, 1.0);      // Number
    setCellString(0, 3, "Banana"); // String

    EvalResult result = eval("=SORT(A1:A4)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 4u);

    // Excel sort order: numbers first, then strings
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 5.0);
    EXPECT_EQ(result.getArrayAt(2, 0).getString(), "Apple");
    EXPECT_EQ(result.getArrayAt(3, 0).getString(), "Banana");
}

TEST_F(FnArrayTest, SortStableSort) {
    // Test that sort is stable - equal values maintain original order
    // A1:B3 = {{1, "First"}, {2, "Second"}, {1, "Third"}}
    setCellValue(0, 0, 1.0);
    setCellString(1, 0, "First");
    setCellValue(0, 1, 2.0);
    setCellString(1, 1, "Second");
    setCellValue(0, 2, 1.0);
    setCellString(1, 2, "Third");

    EvalResult result = eval("=SORT(A1:B3)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 3u);

    // The two rows with value 1 should maintain their original order
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_EQ(result.getArrayAt(0, 1).getString(), "First");
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 1.0);
    EXPECT_EQ(result.getArrayAt(1, 1).getString(), "Third");
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 0).getNumber(), 2.0);
    EXPECT_EQ(result.getArrayAt(2, 1).getString(), "Second");
}

// =============================================================================
// SORT Tests - Single cell
// =============================================================================

TEST_F(FnArrayTest, SortSingleCell) {
    setCellValue(0, 0, 42.0);

    EvalResult result = eval("=SORT(A1)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 1u);
    EXPECT_EQ(result.getArrayCols(), 1u);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 42.0);
}

// =============================================================================
// FILTER Tests - Basic functionality
// =============================================================================

TEST_F(FnArrayTest, FilterBasicRows) {
    // A1:A5 = {1, 2, 3, 4, 5}
    // B1:B5 = {TRUE, FALSE, TRUE, FALSE, TRUE}
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);
    setCellValue(0, 3, 4.0);
    setCellValue(0, 4, 5.0);
    // Set B column as boolean criteria
    Cell* b1 = sheet->getOrCreateCellAt(colIds[1], rowIds[0]);
    b1->value = CellValue(true);
    Cell* b2 = sheet->getOrCreateCellAt(colIds[1], rowIds[1]);
    b2->value = CellValue(false);
    Cell* b3 = sheet->getOrCreateCellAt(colIds[1], rowIds[2]);
    b3->value = CellValue(true);
    Cell* b4 = sheet->getOrCreateCellAt(colIds[1], rowIds[3]);
    b4->value = CellValue(false);
    Cell* b5 = sheet->getOrCreateCellAt(colIds[1], rowIds[4]);
    b5->value = CellValue(true);

    EvalResult result = eval("=FILTER(A1:A5,B1:B5)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 3u);
    EXPECT_EQ(result.getArrayCols(), 1u);

    // Should return {1, 3, 5}
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 3.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 0).getNumber(), 5.0);
}

TEST_F(FnArrayTest, FilterMultiColumn) {
    // A1:B3 = {{1, "A"}, {2, "B"}, {3, "C"}}
    // C1:C3 = {TRUE, FALSE, TRUE}
    setCellValue(0, 0, 1.0);
    setCellString(1, 0, "A");
    setCellValue(0, 1, 2.0);
    setCellString(1, 1, "B");
    setCellValue(0, 2, 3.0);
    setCellString(1, 2, "C");

    Cell* c1 = sheet->getOrCreateCellAt(colIds[2], rowIds[0]);
    c1->value = CellValue(true);
    Cell* c2 = sheet->getOrCreateCellAt(colIds[2], rowIds[1]);
    c2->value = CellValue(false);
    Cell* c3 = sheet->getOrCreateCellAt(colIds[2], rowIds[2]);
    c3->value = CellValue(true);

    EvalResult result = eval("=FILTER(A1:B3,C1:C3)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 2u);
    EXPECT_EQ(result.getArrayCols(), 2u);

    // Should return {{1, "A"}, {3, "C"}}
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_EQ(result.getArrayAt(0, 1).getString(), "A");
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 3.0);
    EXPECT_EQ(result.getArrayAt(1, 1).getString(), "C");
}

TEST_F(FnArrayTest, FilterWithNumbers) {
    // Numbers can be used as criteria (0 = FALSE, non-zero = TRUE)
    setCellValue(0, 0, 10.0);
    setCellValue(0, 1, 20.0);
    setCellValue(0, 2, 30.0);

    setCellValue(1, 0, 1.0);  // TRUE
    setCellValue(1, 1, 0.0);  // FALSE
    setCellValue(1, 2, 2.0);  // TRUE

    EvalResult result = eval("=FILTER(A1:A3,B1:B3)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 2u);

    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 10.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 30.0);
}

// =============================================================================
// FILTER Tests - if_empty parameter
// =============================================================================

TEST_F(FnArrayTest, FilterIfEmptyUsed) {
    // All criteria are FALSE
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);

    Cell* b1 = sheet->getOrCreateCellAt(colIds[1], rowIds[0]);
    b1->value = CellValue(false);
    Cell* b2 = sheet->getOrCreateCellAt(colIds[1], rowIds[1]);
    b2->value = CellValue(false);

    // With if_empty, should return that value
    EvalResult result = eval("=FILTER(A1:A2,B1:B2,\"No matches\")");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 1u);
    EXPECT_EQ(result.getArrayCols(), 1u);
    EXPECT_EQ(result.getArrayAt(0, 0).getString(), "No matches");
}

TEST_F(FnArrayTest, FilterIfEmptyNotUsed) {
    // Some criteria are TRUE, so if_empty should not be used
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);

    Cell* b1 = sheet->getOrCreateCellAt(colIds[1], rowIds[0]);
    b1->value = CellValue(true);
    Cell* b2 = sheet->getOrCreateCellAt(colIds[1], rowIds[1]);
    b2->value = CellValue(false);

    EvalResult result = eval("=FILTER(A1:A2,B1:B2,\"No matches\")");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 1u);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
}

TEST_F(FnArrayTest, FilterNoMatchesNoIfEmpty) {
    // All criteria are FALSE and no if_empty - should return #CALC! error
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);

    Cell* b1 = sheet->getOrCreateCellAt(colIds[1], rowIds[0]);
    b1->value = CellValue(false);
    Cell* b2 = sheet->getOrCreateCellAt(colIds[1], rowIds[1]);
    b2->value = CellValue(false);

    EvalResult result = eval("=FILTER(A1:A2,B1:B2)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::CALC);
}

// =============================================================================
// FILTER Tests - Error cases
// =============================================================================

TEST_F(FnArrayTest, FilterNoArgs) {
    EvalResult result = eval("=FILTER()");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnArrayTest, FilterOneArg) {
    setCellValue(0, 0, 1.0);
    EvalResult result = eval("=FILTER(A1:A3)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnArrayTest, FilterTooManyArgs) {
    setCellValue(0, 0, 1.0);
    Cell* b1 = sheet->getOrCreateCellAt(colIds[1], rowIds[0]);
    b1->value = CellValue(true);
    EvalResult result = eval("=FILTER(A1:A3,B1:B3,\"empty\",\"extra\")");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnArrayTest, FilterDimensionMismatch) {
    // 5 rows of data but only 3 rows of criteria
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);
    setCellValue(0, 3, 4.0);
    setCellValue(0, 4, 5.0);

    Cell* b1 = sheet->getOrCreateCellAt(colIds[1], rowIds[0]);
    b1->value = CellValue(true);
    Cell* b2 = sheet->getOrCreateCellAt(colIds[1], rowIds[1]);
    b2->value = CellValue(false);
    Cell* b3 = sheet->getOrCreateCellAt(colIds[1], rowIds[2]);
    b3->value = CellValue(true);

    EvalResult result = eval("=FILTER(A1:A5,B1:B3)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// =============================================================================
// FILTER Tests - Single cell
// =============================================================================

TEST_F(FnArrayTest, FilterSingleCellTrue) {
    setCellValue(0, 0, 42.0);
    Cell* b1 = sheet->getOrCreateCellAt(colIds[1], rowIds[0]);
    b1->value = CellValue(true);

    EvalResult result = eval("=FILTER(A1,B1)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 1u);
    EXPECT_EQ(result.getArrayCols(), 1u);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 42.0);
}

TEST_F(FnArrayTest, FilterSingleCellFalse) {
    setCellValue(0, 0, 42.0);
    Cell* b1 = sheet->getOrCreateCellAt(colIds[1], rowIds[0]);
    b1->value = CellValue(false);

    EvalResult result = eval("=FILTER(A1,B1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::CALC);
}

// =============================================================================
// SEQUENCE Tests - Basic functionality
// =============================================================================

TEST_F(FnArrayTest, SequenceBasicSingleColumn) {
    EvalResult result = eval("=SEQUENCE(5)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 5u);
    EXPECT_EQ(result.getArrayCols(), 1u);

    // Should return {1, 2, 3, 4, 5}
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 2.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 0).getNumber(), 3.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(3, 0).getNumber(), 4.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(4, 0).getNumber(), 5.0);
}

TEST_F(FnArrayTest, SequenceRowsAndCols) {
    EvalResult result = eval("=SEQUENCE(3,4)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 3u);
    EXPECT_EQ(result.getArrayCols(), 4u);

    // Should return {{1,2,3,4}, {5,6,7,8}, {9,10,11,12}}
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 1).getNumber(), 2.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 2).getNumber(), 3.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 3).getNumber(), 4.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 5.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 1).getNumber(), 6.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 2).getNumber(), 7.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 3).getNumber(), 8.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 0).getNumber(), 9.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 1).getNumber(), 10.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 2).getNumber(), 11.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 3).getNumber(), 12.0);
}

TEST_F(FnArrayTest, SequenceCustomStart) {
    EvalResult result = eval("=SEQUENCE(3,1,10)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 3u);
    EXPECT_EQ(result.getArrayCols(), 1u);

    // Should return {10, 11, 12}
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 10.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 11.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 0).getNumber(), 12.0);
}

TEST_F(FnArrayTest, SequenceCustomStep) {
    EvalResult result = eval("=SEQUENCE(4,1,0,5)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 4u);
    EXPECT_EQ(result.getArrayCols(), 1u);

    // Should return {0, 5, 10, 15}
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 0.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 5.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 0).getNumber(), 10.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(3, 0).getNumber(), 15.0);
}

TEST_F(FnArrayTest, SequenceNegativeStep) {
    EvalResult result = eval("=SEQUENCE(5,1,10,-2)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 5u);

    // Should return {10, 8, 6, 4, 2}
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 10.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 8.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 0).getNumber(), 6.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(3, 0).getNumber(), 4.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(4, 0).getNumber(), 2.0);
}

TEST_F(FnArrayTest, SequenceFractionalStep) {
    EvalResult result = eval("=SEQUENCE(4,1,0,0.5)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 4u);

    // Should return {0, 0.5, 1, 1.5}
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 0.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 0.5);
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 0).getNumber(), 1.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(3, 0).getNumber(), 1.5);
}

// =============================================================================
// SEQUENCE Tests - Error cases
// =============================================================================

TEST_F(FnArrayTest, SequenceNoArgs) {
    EvalResult result = eval("=SEQUENCE()");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnArrayTest, SequenceTooManyArgs) {
    EvalResult result = eval("=SEQUENCE(1,1,1,1,1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnArrayTest, SequenceZeroRows) {
    EvalResult result = eval("=SEQUENCE(0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnArrayTest, SequenceNegativeRows) {
    EvalResult result = eval("=SEQUENCE(-5)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnArrayTest, SequenceZeroCols) {
    EvalResult result = eval("=SEQUENCE(5,0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// =============================================================================
// SEQUENCE Tests - Single cell
// =============================================================================

TEST_F(FnArrayTest, SequenceSingleCell) {
    EvalResult result = eval("=SEQUENCE(1,1,42)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 1u);
    EXPECT_EQ(result.getArrayCols(), 1u);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 42.0);
}

// =============================================================================
// TRANSPOSE Tests - Basic functionality
// =============================================================================

TEST_F(FnArrayTest, TransposeColumnToRow) {
    // A1:A3 = {1, 2, 3} (column vector)
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    setCellValue(0, 2, 3.0);

    EvalResult result = eval("=TRANSPOSE(A1:A3)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 1u);
    EXPECT_EQ(result.getArrayCols(), 3u);

    // Should return {1, 2, 3} as a row
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 1).getNumber(), 2.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 2).getNumber(), 3.0);
}

TEST_F(FnArrayTest, TransposeRowToColumn) {
    // A1:C1 = {1, 2, 3} (row vector)
    setCellValue(0, 0, 1.0);
    setCellValue(1, 0, 2.0);
    setCellValue(2, 0, 3.0);

    EvalResult result = eval("=TRANSPOSE(A1:C1)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 3u);
    EXPECT_EQ(result.getArrayCols(), 1u);

    // Should return {1, 2, 3} as a column
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 2.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(2, 0).getNumber(), 3.0);
}

TEST_F(FnArrayTest, TransposeMatrix) {
    // A1:B3 = {{1, 2}, {3, 4}, {5, 6}} (3x2 matrix)
    setCellValue(0, 0, 1.0);
    setCellValue(1, 0, 2.0);
    setCellValue(0, 1, 3.0);
    setCellValue(1, 1, 4.0);
    setCellValue(0, 2, 5.0);
    setCellValue(1, 2, 6.0);

    EvalResult result = eval("=TRANSPOSE(A1:B3)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 2u);  // Now 2x3
    EXPECT_EQ(result.getArrayCols(), 3u);

    // Row 0: {1, 3, 5}
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 1).getNumber(), 3.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 2).getNumber(), 5.0);
    // Row 1: {2, 4, 6}
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 2.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 1).getNumber(), 4.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 2).getNumber(), 6.0);
}

TEST_F(FnArrayTest, TransposeMixedTypes) {
    // A1:B2 = {{1, "A"}, {2, "B"}}
    setCellValue(0, 0, 1.0);
    setCellString(1, 0, "A");
    setCellValue(0, 1, 2.0);
    setCellString(1, 1, "B");

    EvalResult result = eval("=TRANSPOSE(A1:B2)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 2u);
    EXPECT_EQ(result.getArrayCols(), 2u);

    // Row 0: {1, 2}
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 1).getNumber(), 2.0);
    // Row 1: {"A", "B"}
    EXPECT_EQ(result.getArrayAt(1, 0).getString(), "A");
    EXPECT_EQ(result.getArrayAt(1, 1).getString(), "B");
}

// =============================================================================
// TRANSPOSE Tests - Error cases
// =============================================================================

TEST_F(FnArrayTest, TransposeNoArgs) {
    EvalResult result = eval("=TRANSPOSE()");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnArrayTest, TransposeTooManyArgs) {
    setCellValue(0, 0, 1.0);
    EvalResult result = eval("=TRANSPOSE(A1,B1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// =============================================================================
// TRANSPOSE Tests - Single cell
// =============================================================================

TEST_F(FnArrayTest, TransposeSingleCell) {
    setCellValue(0, 0, 42.0);

    EvalResult result = eval("=TRANSPOSE(A1)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 1u);
    EXPECT_EQ(result.getArrayCols(), 1u);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 42.0);
}

// =============================================================================
// TRANSPOSE Tests - Square matrix
// =============================================================================

TEST_F(FnArrayTest, TransposeSquareMatrix) {
    // A1:B2 = {{1, 2}, {3, 4}}
    setCellValue(0, 0, 1.0);
    setCellValue(1, 0, 2.0);
    setCellValue(0, 1, 3.0);
    setCellValue(1, 1, 4.0);

    EvalResult result = eval("=TRANSPOSE(A1:B2)");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.getArrayRows(), 2u);
    EXPECT_EQ(result.getArrayCols(), 2u);

    // Row 0: {1, 3}
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 0).getNumber(), 1.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(0, 1).getNumber(), 3.0);
    // Row 1: {2, 4}
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 0).getNumber(), 2.0);
    EXPECT_DOUBLE_EQ(result.getArrayAt(1, 1).getNumber(), 4.0);
}

}  // namespace
}  // namespace cells
