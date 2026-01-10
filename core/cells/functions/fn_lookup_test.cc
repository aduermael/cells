#include "core/cells/functions/fn_lookup.h"

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

class FnLookupTest : public ::testing::Test {
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

    // Set a cell value
    Cell* setCellValue(uint32_t col, uint32_t row, double value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(value);
        return cell;
    }

    Cell* setCellValue(uint32_t col, uint32_t row, const std::string& value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(value);
        return cell;
    }

    Cell* setCellValue(uint32_t col, uint32_t row, const char* value) {
        return setCellValue(col, row, std::string(value));
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet = nullptr;
    ID colIds[26];
    ID rowIds[100];
};

// =============================================================================
// INDEX Tests
// =============================================================================

TEST_F(FnLookupTest, IndexBasic) {
    // Create a 3x3 grid in A1:C3
    // 1  2  3
    // 4  5  6
    // 7  8  9
    setCellValue(0, 0, 1.0);
    setCellValue(1, 0, 2.0);
    setCellValue(2, 0, 3.0);
    setCellValue(0, 1, 4.0);
    setCellValue(1, 1, 5.0);
    setCellValue(2, 1, 6.0);
    setCellValue(0, 2, 7.0);
    setCellValue(1, 2, 8.0);
    setCellValue(2, 2, 9.0);

    // INDEX(A1:C3, 2, 2) should return 5
    EvalResult result = eval("=INDEX(A1:C3, 2, 2)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

TEST_F(FnLookupTest, IndexFirstCell) {
    setCellValue(0, 0, 10.0);
    setCellValue(1, 0, 20.0);
    setCellValue(0, 1, 30.0);
    setCellValue(1, 1, 40.0);

    EvalResult result = eval("=INDEX(A1:B2, 1, 1)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 10.0);
}

TEST_F(FnLookupTest, IndexLastCell) {
    setCellValue(0, 0, 10.0);
    setCellValue(1, 0, 20.0);
    setCellValue(0, 1, 30.0);
    setCellValue(1, 1, 40.0);

    EvalResult result = eval("=INDEX(A1:B2, 2, 2)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 40.0);
}

TEST_F(FnLookupTest, IndexSingleColumn) {
    // Single column A1:A3
    setCellValue(0, 0, 100.0);
    setCellValue(0, 1, 200.0);
    setCellValue(0, 2, 300.0);

    // Row 2 of a single column
    EvalResult result = eval("=INDEX(A1:A3, 2)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 200.0);
}

TEST_F(FnLookupTest, IndexSingleRow) {
    // Single row A1:C1
    setCellValue(0, 0, 11.0);
    setCellValue(1, 0, 22.0);
    setCellValue(2, 0, 33.0);

    // Column 3 (row 1 implied for single-row range)
    EvalResult result = eval("=INDEX(A1:C1, 1, 3)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 33.0);
}

TEST_F(FnLookupTest, IndexOutOfBounds) {
    setCellValue(0, 0, 1.0);
    setCellValue(1, 0, 2.0);

    EvalResult result = eval("=INDEX(A1:B1, 1, 3)");  // Column 3 doesn't exist
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::REF);
}

TEST_F(FnLookupTest, IndexRowOutOfBounds) {
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);

    EvalResult result = eval("=INDEX(A1:A2, 3, 1)");  // Row 3 doesn't exist
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::REF);
}

TEST_F(FnLookupTest, IndexEmptyCell) {
    setCellValue(0, 0, 1.0);
    // A2 is empty
    setCellValue(0, 2, 3.0);

    EvalResult result = eval("=INDEX(A1:A3, 2, 1)");
    EXPECT_TRUE(result.isEmpty());
}

TEST_F(FnLookupTest, IndexStringValue) {
    setCellValue(0, 0, "hello");
    setCellValue(1, 0, "world");

    EvalResult result = eval("=INDEX(A1:B1, 1, 2)");
    ASSERT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "world");
}

TEST_F(FnLookupTest, IndexWholeColumn) {
    // A1=10, A2=20, A3=30
    setCellValue(0, 0, 10.0);
    setCellValue(0, 1, 20.0);
    setCellValue(0, 2, 30.0);

    // INDEX(A:A, 2) should return A2 = 20
    EvalResult result = eval("=INDEX(A:A, 2)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_EQ(result.getNumber(), 20.0);
}

TEST_F(FnLookupTest, IndexWholeColumnWithColArg) {
    setCellValue(0, 0, 10.0);
    setCellValue(0, 1, 20.0);

    // INDEX(A:A, 1, 1) should return A1 = 10
    EvalResult result = eval("=INDEX(A:A, 1, 1)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_EQ(result.getNumber(), 10.0);
}

TEST_F(FnLookupTest, IndexColumnRange) {
    // A1=1, B1=2, C1=3
    // A2=4, B2=5, C2=6
    setCellValue(0, 0, 1.0);
    setCellValue(1, 0, 2.0);
    setCellValue(2, 0, 3.0);
    setCellValue(0, 1, 4.0);
    setCellValue(1, 1, 5.0);
    setCellValue(2, 1, 6.0);

    // INDEX(A:C, 2, 2) should return B2 = 5
    EvalResult result = eval("=INDEX(A:C, 2, 2)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_EQ(result.getNumber(), 5.0);
}

TEST_F(FnLookupTest, IndexWholeRow) {
    // A1=10, B1=20, C1=30
    setCellValue(0, 0, 10.0);
    setCellValue(1, 0, 20.0);
    setCellValue(2, 0, 30.0);

    // INDEX(1:1, 1, 2) should return B1 = 20
    EvalResult result = eval("=INDEX(1:1, 1, 2)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_EQ(result.getNumber(), 20.0);
}

TEST_F(FnLookupTest, IndexRowRange) {
    // A1=1, B1=2
    // A2=3, B2=4
    // A3=5, B3=6
    setCellValue(0, 0, 1.0);
    setCellValue(1, 0, 2.0);
    setCellValue(0, 1, 3.0);
    setCellValue(1, 1, 4.0);
    setCellValue(0, 2, 5.0);
    setCellValue(1, 2, 6.0);

    // INDEX(1:3, 2, 2) should return B2 = 4
    EvalResult result = eval("=INDEX(1:3, 2, 2)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_EQ(result.getNumber(), 4.0);
}

// =============================================================================
// MATCH Tests
// =============================================================================

TEST_F(FnLookupTest, MatchExactFound) {
    // A1:A5 = 10, 20, 30, 40, 50
    setCellValue(0, 0, 10.0);
    setCellValue(0, 1, 20.0);
    setCellValue(0, 2, 30.0);
    setCellValue(0, 3, 40.0);
    setCellValue(0, 4, 50.0);

    // Find 30 with exact match
    EvalResult result = eval("=MATCH(30, A1:A5, 0)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);  // 1-indexed position
}

TEST_F(FnLookupTest, MatchExactNotFound) {
    setCellValue(0, 0, 10.0);
    setCellValue(0, 1, 20.0);
    setCellValue(0, 2, 30.0);

    EvalResult result = eval("=MATCH(25, A1:A3, 0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NA);
}

TEST_F(FnLookupTest, MatchApproximateAscending) {
    // Sorted ascending: 10, 20, 30, 40, 50
    setCellValue(0, 0, 10.0);
    setCellValue(0, 1, 20.0);
    setCellValue(0, 2, 30.0);
    setCellValue(0, 3, 40.0);
    setCellValue(0, 4, 50.0);

    // Find largest value <= 25
    EvalResult result = eval("=MATCH(25, A1:A5, 1)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);  // 20 is at position 2
}

TEST_F(FnLookupTest, MatchApproximateAscendingExact) {
    setCellValue(0, 0, 10.0);
    setCellValue(0, 1, 20.0);
    setCellValue(0, 2, 30.0);

    // Exact value exists
    EvalResult result = eval("=MATCH(20, A1:A3, 1)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FnLookupTest, MatchApproximateDescending) {
    // Sorted descending: 50, 40, 30, 20, 10
    setCellValue(0, 0, 50.0);
    setCellValue(0, 1, 40.0);
    setCellValue(0, 2, 30.0);
    setCellValue(0, 3, 20.0);
    setCellValue(0, 4, 10.0);

    // Find smallest value >= 35
    EvalResult result = eval("=MATCH(35, A1:A5, -1)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);  // 40 is at position 2
}

TEST_F(FnLookupTest, MatchHorizontalRange) {
    // A1:E1 = 10, 20, 30, 40, 50
    setCellValue(0, 0, 10.0);
    setCellValue(1, 0, 20.0);
    setCellValue(2, 0, 30.0);
    setCellValue(3, 0, 40.0);
    setCellValue(4, 0, 50.0);

    EvalResult result = eval("=MATCH(30, A1:E1, 0)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);
}

TEST_F(FnLookupTest, MatchStringExact) {
    setCellValue(0, 0, "apple");
    setCellValue(0, 1, "banana");
    setCellValue(0, 2, "cherry");

    EvalResult result = eval("=MATCH(\"banana\", A1:A3, 0)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FnLookupTest, MatchStringCaseInsensitive) {
    setCellValue(0, 0, "Apple");
    setCellValue(0, 1, "Banana");

    EvalResult result = eval("=MATCH(\"BANANA\", A1:A2, 0)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FnLookupTest, MatchFirstPosition) {
    setCellValue(0, 0, 5.0);
    setCellValue(0, 1, 10.0);
    setCellValue(0, 2, 15.0);

    EvalResult result = eval("=MATCH(5, A1:A3, 0)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

// =============================================================================
// VLOOKUP Tests
// =============================================================================

TEST_F(FnLookupTest, VlookupExactMatch) {
    // Create a lookup table
    // A    B    C
    // 1    A    100
    // 2    B    200
    // 3    C    300
    setCellValue(0, 0, 1.0);
    setCellValue(1, 0, "A");
    setCellValue(2, 0, 100.0);
    setCellValue(0, 1, 2.0);
    setCellValue(1, 1, "B");
    setCellValue(2, 1, 200.0);
    setCellValue(0, 2, 3.0);
    setCellValue(1, 2, "C");
    setCellValue(2, 2, 300.0);

    // Look up 2, return column 3
    EvalResult result = eval("=VLOOKUP(2, A1:C3, 3, FALSE)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 200.0);
}

TEST_F(FnLookupTest, VlookupExactMatchString) {
    // Create a lookup table with string keys
    setCellValue(0, 0, "apple");
    setCellValue(1, 0, 1.5);
    setCellValue(0, 1, "banana");
    setCellValue(1, 1, 0.99);
    setCellValue(0, 2, "cherry");
    setCellValue(1, 2, 2.5);

    EvalResult result = eval("=VLOOKUP(\"banana\", A1:B3, 2, FALSE)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.99);
}

TEST_F(FnLookupTest, VlookupApproximateMatch) {
    // Sorted data
    setCellValue(0, 0, 10.0);
    setCellValue(1, 0, "ten");
    setCellValue(0, 1, 20.0);
    setCellValue(1, 1, "twenty");
    setCellValue(0, 2, 30.0);
    setCellValue(1, 2, "thirty");

    // Look up 25, should find 20
    EvalResult result = eval("=VLOOKUP(25, A1:B3, 2, TRUE)");
    ASSERT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "twenty");
}

TEST_F(FnLookupTest, VlookupNotFound) {
    setCellValue(0, 0, 10.0);
    setCellValue(1, 0, "A");
    setCellValue(0, 1, 20.0);
    setCellValue(1, 1, "B");

    // Exact match for value not in table
    EvalResult result = eval("=VLOOKUP(15, A1:B2, 2, FALSE)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NA);
}

TEST_F(FnLookupTest, VlookupColumnIndexOutOfBounds) {
    setCellValue(0, 0, 1.0);
    setCellValue(1, 0, 2.0);

    EvalResult result = eval("=VLOOKUP(1, A1:B1, 3, FALSE)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::REF);
}

TEST_F(FnLookupTest, VlookupReturnFirstColumn) {
    setCellValue(0, 0, 1.0);
    setCellValue(1, 0, 100.0);
    setCellValue(0, 1, 2.0);
    setCellValue(1, 1, 200.0);

    // Column index 1 returns the lookup column itself
    EvalResult result = eval("=VLOOKUP(2, A1:B2, 1, FALSE)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FnLookupTest, VlookupCaseInsensitive) {
    setCellValue(0, 0, "Apple");
    setCellValue(1, 0, 1.0);
    setCellValue(0, 1, "Banana");
    setCellValue(1, 1, 2.0);

    EvalResult result = eval("=VLOOKUP(\"APPLE\", A1:B2, 2, FALSE)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

// =============================================================================
// HLOOKUP Tests
// =============================================================================

TEST_F(FnLookupTest, HlookupExactMatch) {
    // Create a horizontal lookup table
    // A      B      C
    // 1      2      3
    // 100    200    300
    setCellValue(0, 0, 1.0);
    setCellValue(1, 0, 2.0);
    setCellValue(2, 0, 3.0);
    setCellValue(0, 1, 100.0);
    setCellValue(1, 1, 200.0);
    setCellValue(2, 1, 300.0);

    // Look up 2 in first row, return row 2
    EvalResult result = eval("=HLOOKUP(2, A1:C2, 2, FALSE)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 200.0);
}

TEST_F(FnLookupTest, HlookupApproximateMatch) {
    // Sorted header row
    setCellValue(0, 0, 10.0);
    setCellValue(1, 0, 20.0);
    setCellValue(2, 0, 30.0);
    setCellValue(0, 1, "A");
    setCellValue(1, 1, "B");
    setCellValue(2, 1, "C");

    // Look up 25, should find 20
    EvalResult result = eval("=HLOOKUP(25, A1:C2, 2, TRUE)");
    ASSERT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "B");
}

TEST_F(FnLookupTest, HlookupNotFound) {
    setCellValue(0, 0, 1.0);
    setCellValue(1, 0, 2.0);
    setCellValue(0, 1, "A");
    setCellValue(1, 1, "B");

    EvalResult result = eval("=HLOOKUP(3, A1:B2, 2, FALSE)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NA);
}

TEST_F(FnLookupTest, HlookupRowIndexOutOfBounds) {
    setCellValue(0, 0, 1.0);
    setCellValue(1, 0, 2.0);

    EvalResult result = eval("=HLOOKUP(1, A1:B1, 2, FALSE)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::REF);
}

TEST_F(FnLookupTest, HlookupStringKey) {
    setCellValue(0, 0, "Name");
    setCellValue(1, 0, "Age");
    setCellValue(2, 0, "City");
    setCellValue(0, 1, "John");
    setCellValue(1, 1, 30.0);
    setCellValue(2, 1, "NYC");

    EvalResult result = eval("=HLOOKUP(\"Age\", A1:C2, 2, FALSE)");
    ASSERT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 30.0);
}

// =============================================================================
// Combined INDEX/MATCH Tests
// =============================================================================

TEST_F(FnLookupTest, IndexMatchCombined) {
    // Common pattern: INDEX/MATCH for flexible lookup
    // A: IDs, B: Names, C: Values
    setCellValue(0, 0, 101.0);
    setCellValue(1, 0, "Alice");
    setCellValue(2, 0, 1000.0);
    setCellValue(0, 1, 102.0);
    setCellValue(1, 1, "Bob");
    setCellValue(2, 1, 2000.0);
    setCellValue(0, 2, 103.0);
    setCellValue(1, 2, "Charlie");
    setCellValue(2, 2, 3000.0);

    // Find Alice's value using INDEX/MATCH
    // First find Alice's row with MATCH
    EvalResult matchResult = eval("=MATCH(\"Alice\", B1:B3, 0)");
    ASSERT_TRUE(matchResult.isNumber());
    EXPECT_DOUBLE_EQ(matchResult.getNumber(), 1.0);

    // Then use INDEX to get the value
    EvalResult indexResult = eval("=INDEX(C1:C3, 1, 1)");
    ASSERT_TRUE(indexResult.isNumber());
    EXPECT_DOUBLE_EQ(indexResult.getNumber(), 1000.0);
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST_F(FnLookupTest, IndexTooFewArgs) {
    setCellValue(0, 0, 1.0);
    EvalResult result = eval("=INDEX(A1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnLookupTest, IndexTooManyArgs) {
    setCellValue(0, 0, 1.0);
    EvalResult result = eval("=INDEX(A1:A1, 1, 1, 1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnLookupTest, MatchTooFewArgs) {
    setCellValue(0, 0, 1.0);
    EvalResult result = eval("=MATCH(1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnLookupTest, VlookupTooFewArgs) {
    setCellValue(0, 0, 1.0);
    setCellValue(1, 0, 2.0);
    EvalResult result = eval("=VLOOKUP(1, A1:B1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FnLookupTest, HlookupTooFewArgs) {
    setCellValue(0, 0, 1.0);
    setCellValue(0, 1, 2.0);
    EvalResult result = eval("=HLOOKUP(1, A1:A2)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

}  // namespace
}  // namespace cells
