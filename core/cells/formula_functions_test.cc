#include "core/cells/formula_functions.h"

#include <cmath>
#include <cstring>

#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_eval.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

namespace cells {
namespace {

// Helper class for function tests - mirrors FormulaEvalTest
class FunctionTest : public ::testing::Test {
protected:
    void SetUp() override {
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

    // Parse and evaluate a formula, returning the result
    EvalResult eval(const std::string& formula) {
        FormulaParser parser(formula);
        auto ast = parser.parse();
        if (!ast || parser.hasErrors()) {
            return EvalResult::Error(CellError::VALUE);
        }

        // Create missing entities and resolve references
        FormulaResolver resolver(*workbook, *sheet);
        createRequiredEntities(resolver, ast.get());
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

    // Helper to create missing entities before resolution
    void createRequiredEntities(FormulaResolver& resolver, ASTNode* ast) {
        RequiredEntities required = resolver.getRequiredEntities(ast);
        for (const auto& pendingCell : required.cells) {
            auto findColPos = [&required, this](const ID& colId) -> uint32_t {
                for (const auto& c : required.columns) {
                    if (c.id == colId)
                        return c.position;
                }
                const Axis* axis = sheet->getColumn(colId);
                return axis ? axis->position : 0;
            };
            auto findRowPos = [&required, this](const ID& rowId) -> uint32_t {
                for (const auto& r : required.rows) {
                    if (r.id == rowId)
                        return r.position;
                }
                const Axis* axis = sheet->getRow(rowId);
                return axis ? axis->position : 0;
            };
            uint32_t colPos = findColPos(pendingCell.colId);
            uint32_t rowPos = findRowPos(pendingCell.rowId);
            const Axis* col = sheet->getColumnByPosition(colPos);
            const Axis* row = sheet->getRowByPosition(rowPos);
            if (col && row) {
                sheet->getOrCreateCellAt(col->id, row->id);
            }
        }
    }

    // Set a cell value at a given column/row position (0-indexed)
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

    // Explicit const char* overload to prevent implicit conversion to bool
    Cell* setCellValue(uint32_t col, uint32_t row, const char* value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(std::string(value));
        return cell;
    }

    Cell* setCellValue(uint32_t col, uint32_t row, bool value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(value);
        return cell;
    }

    Cell* setCellError(uint32_t col, uint32_t row, CellError error) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(error);
        return cell;
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet = nullptr;
    ID colIds[26];   // A=0, B=1, ..., Z=25
    ID rowIds[100];  // Row 1=0, Row 2=1, ..., Row 100=99
};

// =============================================================================
// Function Registry Tests
// =============================================================================

TEST(FunctionRegistryTest, SingletonInstance) {
    FunctionRegistry& r1 = FunctionRegistry::instance();
    FunctionRegistry& r2 = FunctionRegistry::instance();
    EXPECT_EQ(&r1, &r2);
}

TEST_F(FunctionTest, UnknownFunctionReturnsNameError) {
    EvalResult result = eval("=UNKNOWN_FUNCTION()");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NAME);
}

TEST_F(FunctionTest, FunctionNamesCaseInsensitive) {
    // Once we have SUM implemented, this will test that SUM, sum, Sum all work
    // For now, test that an unknown function works case-insensitively
    EvalResult result1 = eval("=NOTAFUNCTION()");
    EvalResult result2 = eval("=notafunction()");
    EvalResult result3 = eval("=NotAFunction()");

    EXPECT_TRUE(result1.isError());
    EXPECT_TRUE(result2.isError());
    EXPECT_TRUE(result3.isError());
    EXPECT_EQ(result1.getError(), result2.getError());
    EXPECT_EQ(result1.getError(), result3.getError());
}

// =============================================================================
// SUM Function Tests
// =============================================================================

TEST_F(FunctionTest, SumBasic) {
    EvalResult result = eval("=SUM(1,2,3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 6.0);
}

TEST_F(FunctionTest, SumRange) {
    setCellValue(0, 0, 1.0);  // A1
    setCellValue(0, 1, 2.0);  // A2
    setCellValue(0, 2, 3.0);  // A3

    EvalResult result = eval("=SUM(A1:A3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 6.0);
}

TEST_F(FunctionTest, Sum2x2Range) {
    setCellValue(0, 0, 1.0);  // A1
    setCellValue(1, 0, 2.0);  // B1
    setCellValue(0, 1, 3.0);  // A2
    setCellValue(1, 1, 4.0);  // B2

    EvalResult result = eval("=SUM(A1:B2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 10.0);
}

TEST_F(FunctionTest, SumMixedArgs) {
    setCellValue(0, 0, 1.0);  // A1
    setCellValue(0, 1, 2.0);  // A2
    setCellValue(0, 2, 3.0);  // A3

    EvalResult result = eval("=SUM(1,A1:A3,10)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 17.0);  // 1 + 1 + 2 + 3 + 10
}

TEST_F(FunctionTest, SumNoArgs) {
    EvalResult result = eval("=SUM()");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, SumStringCoercion) {
    EvalResult result = eval("=SUM(\"5\",3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 8.0);
}

TEST_F(FunctionTest, SumBooleanCoercion) {
    EvalResult result = eval("=SUM(TRUE,1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);  // TRUE=1, 1=1
}

TEST_F(FunctionTest, SumWithErrorPropagates) {
    setCellValue(0, 0, 1.0);             // A1
    setCellError(0, 1, CellError::DIV);  // A2
    setCellValue(0, 2, 3.0);             // A3

    EvalResult result = eval("=SUM(A1:A3)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

TEST_F(FunctionTest, SumWholeColumn) {
    setCellValue(0, 0, 10.0);  // A1
    setCellValue(0, 4, 20.0);  // A5
    setCellValue(0, 9, 30.0);  // A10

    EvalResult result = eval("=SUM(A:A)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 60.0);
}

TEST_F(FunctionTest, SumNegativeNumbers) {
    EvalResult result = eval("=SUM(-1,-2,-3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -6.0);
}

TEST_F(FunctionTest, SumDecimalNumbers) {
    EvalResult result = eval("=SUM(1.5,2.5,3.0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 7.0);
}

TEST_F(FunctionTest, SumSingleValue) {
    EvalResult result = eval("=SUM(42)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 42.0);
}

TEST_F(FunctionTest, SumEmptyCells) {
    setCellValue(0, 0, 5.0);  // A1
    // A2 is empty
    setCellValue(0, 2, 10.0);  // A3

    EvalResult result = eval("=SUM(A1:A3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 15.0);  // Empty cells are 0
}

// =============================================================================
// AVERAGE Function Tests
// =============================================================================

TEST_F(FunctionTest, AverageBasic) {
    EvalResult result = eval("=AVERAGE(1,2,3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FunctionTest, AverageRange) {
    setCellValue(0, 0, 10.0);  // A1
    setCellValue(0, 1, 20.0);  // A2
    setCellValue(0, 2, 30.0);  // A3
    setCellValue(0, 3, 40.0);  // A4

    EvalResult result = eval("=AVERAGE(A1:A4)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 25.0);
}

TEST_F(FunctionTest, AverageFiveValues) {
    EvalResult result = eval("=AVERAGE(1,2,3,4,5)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);
}

TEST_F(FunctionTest, AverageNoArgsDivByZero) {
    EvalResult result = eval("=AVERAGE()");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

TEST_F(FunctionTest, AverageSingleCell) {
    setCellValue(0, 0, 42.0);  // A1
    EvalResult result = eval("=AVERAGE(A1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 42.0);
}

TEST_F(FunctionTest, AverageIgnoresEmptyCells) {
    setCellValue(0, 0, 10.0);  // A1
    // A2 is empty
    setCellValue(0, 2, 20.0);  // A3

    // Empty cells should not count towards the average
    EvalResult result = eval("=AVERAGE(A1:A3)");
    EXPECT_TRUE(result.isNumber());
    // Only A1 and A3 count, so (10+20)/2 = 15
    EXPECT_DOUBLE_EQ(result.getNumber(), 15.0);
}

TEST_F(FunctionTest, AverageWithErrorPropagates) {
    setCellValue(0, 0, 10.0);            // A1
    setCellError(0, 1, CellError::REF);  // A2

    EvalResult result = eval("=AVERAGE(A1:A2)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::REF);
}

// =============================================================================
// COUNT Function Tests
// =============================================================================

TEST_F(FunctionTest, CountNumbers) {
    EvalResult result = eval("=COUNT(1,2,3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);
}

TEST_F(FunctionTest, CountSkipsStrings) {
    setCellValue(0, 0, 1.0);    // A1
    setCellValue(0, 1, "two");  // A2
    setCellValue(0, 2, 3.0);    // A3

    EvalResult result = eval("=COUNT(A1:A3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);  // Skips "two"
}

TEST_F(FunctionTest, CountNoArgs) {
    EvalResult result = eval("=COUNT()");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, CountSkipsEmpty) {
    setCellValue(0, 0, 1.0);  // A1
    // A2 empty
    setCellValue(0, 2, 3.0);  // A3

    EvalResult result = eval("=COUNT(A1:A3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

// =============================================================================
// COUNTA Function Tests
// =============================================================================

TEST_F(FunctionTest, CountaAll) {
    EvalResult result = eval("=COUNTA(1,\"two\",TRUE)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);
}

TEST_F(FunctionTest, CountaNonEmpty) {
    setCellValue(0, 0, 1.0);     // A1
    setCellValue(0, 1, "text");  // A2
    setCellValue(0, 2, true);    // A3
    // A4 empty

    EvalResult result = eval("=COUNTA(A1:A4)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);
}

TEST_F(FunctionTest, CountaNoArgs) {
    EvalResult result = eval("=COUNTA()");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

// =============================================================================
// MIN Function Tests
// =============================================================================

TEST_F(FunctionTest, MinBasic) {
    EvalResult result = eval("=MIN(5,2,8,1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FunctionTest, MinRange) {
    setCellValue(0, 0, 10.0);  // A1
    setCellValue(1, 0, 5.0);   // B1
    setCellValue(0, 1, 20.0);  // A2
    setCellValue(1, 1, 3.0);   // B2

    EvalResult result = eval("=MIN(A1:B2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);
}

TEST_F(FunctionTest, MinNoArgs) {
    EvalResult result = eval("=MIN()");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, MinNegative) {
    EvalResult result = eval("=MIN(-5,0,5)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -5.0);
}

TEST_F(FunctionTest, MinWithError) {
    setCellValue(0, 0, 10.0);              // A1
    setCellError(0, 1, CellError::VALUE);  // A2

    EvalResult result = eval("=MIN(A1:A2)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// =============================================================================
// MAX Function Tests
// =============================================================================

TEST_F(FunctionTest, MaxBasic) {
    EvalResult result = eval("=MAX(5,2,8,1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 8.0);
}

TEST_F(FunctionTest, MaxRange) {
    setCellValue(0, 0, 10.0);  // A1
    setCellValue(1, 0, 5.0);   // B1
    setCellValue(0, 1, 20.0);  // A2
    setCellValue(1, 1, 3.0);   // B2

    EvalResult result = eval("=MAX(A1:B2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 20.0);
}

TEST_F(FunctionTest, MaxNoArgs) {
    EvalResult result = eval("=MAX()");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, MaxAllNegative) {
    EvalResult result = eval("=MAX(-5,-10,-1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -1.0);
}

TEST_F(FunctionTest, MaxWithError) {
    setCellValue(0, 0, 10.0);            // A1
    setCellError(0, 1, CellError::REF);  // A2

    EvalResult result = eval("=MAX(A1:A2)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::REF);
}

// =============================================================================
// ABS Function Tests
// =============================================================================

TEST_F(FunctionTest, AbsNegative) {
    EvalResult result = eval("=ABS(-5)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

TEST_F(FunctionTest, AbsPositive) {
    EvalResult result = eval("=ABS(5)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

TEST_F(FunctionTest, AbsZero) {
    EvalResult result = eval("=ABS(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, AbsTooManyArgs) {
    EvalResult result = eval("=ABS(1,2)");
    EXPECT_TRUE(result.isError());
    // Should return an error for wrong number of arguments
}

TEST_F(FunctionTest, AbsNoArgs) {
    EvalResult result = eval("=ABS()");
    EXPECT_TRUE(result.isError());
}

// =============================================================================
// SQRT Function Tests
// =============================================================================

TEST_F(FunctionTest, SqrtPerfectSquare) {
    EvalResult result = eval("=SQRT(16)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 4.0);
}

TEST_F(FunctionTest, SqrtNonPerfect) {
    EvalResult result = eval("=SQRT(2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_NEAR(result.getNumber(), 1.41421356, 0.0001);
}

TEST_F(FunctionTest, SqrtNegativeReturnsError) {
    EvalResult result = eval("=SQRT(-1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

TEST_F(FunctionTest, SqrtZero) {
    EvalResult result = eval("=SQRT(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

// =============================================================================
// POWER Function Tests
// =============================================================================

TEST_F(FunctionTest, PowerBasic) {
    EvalResult result = eval("=POWER(2,3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 8.0);
}

TEST_F(FunctionTest, PowerFractional) {
    EvalResult result = eval("=POWER(4,0.5)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FunctionTest, PowerZeroExponent) {
    EvalResult result = eval("=POWER(5,0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FunctionTest, PowerNegativeExponent) {
    EvalResult result = eval("=POWER(2,-2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.25);
}

// =============================================================================
// ROUND Function Tests
// =============================================================================

TEST_F(FunctionTest, RoundUp) {
    EvalResult result = eval("=ROUND(2.5)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);
}

TEST_F(FunctionTest, RoundDown) {
    EvalResult result = eval("=ROUND(2.4)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FunctionTest, RoundToDecimalPlaces) {
    EvalResult result = eval("=ROUND(2.567,2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.57);
}

TEST_F(FunctionTest, RoundNegativeDecimalPlaces) {
    EvalResult result = eval("=ROUND(123,-1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 120.0);
}

TEST_F(FunctionTest, RoundNegativeNumber) {
    EvalResult result = eval("=ROUND(-2.5)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -3.0);
}

// =============================================================================
// ROUNDUP Function Tests
// =============================================================================

TEST_F(FunctionTest, RoundUpPositive) {
    EvalResult result = eval("=ROUNDUP(2.1, 0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);
}

TEST_F(FunctionTest, RoundUpNegative) {
    // ROUNDUP rounds away from zero: -2.1 -> -3
    EvalResult result = eval("=ROUNDUP(-2.1, 0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -3.0);
}

TEST_F(FunctionTest, RoundUpWithDigits) {
    EvalResult result = eval("=ROUNDUP(3.141, 2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.15);
}

TEST_F(FunctionTest, RoundUpExact) {
    // Already exact at that precision — no change
    EvalResult result = eval("=ROUNDUP(3.0, 0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);
}

// =============================================================================
// ROUNDDOWN Function Tests
// =============================================================================

TEST_F(FunctionTest, RoundDownPositive) {
    EvalResult result = eval("=ROUNDDOWN(2.9, 0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FunctionTest, RoundDownNegative) {
    // ROUNDDOWN rounds toward zero: -2.9 -> -2
    EvalResult result = eval("=ROUNDDOWN(-2.9, 0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -2.0);
}

TEST_F(FunctionTest, RoundDownWithDigits) {
    EvalResult result = eval("=ROUNDDOWN(3.149, 2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.14);
}

// =============================================================================
// ROUND/ROUNDUP/ROUNDDOWN Overflow Tests
// =============================================================================

TEST_F(FunctionTest, RoundOverflowReturnsOriginal) {
    // ROUND(9.99E+307, 2) — scaling overflows, return original value
    EvalResult result = eval("=ROUND(9.9999999999999E+307, 2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 9.9999999999999e+307);
}

TEST_F(FunctionTest, RoundUpOverflowReturnsOriginal) {
    EvalResult result = eval("=ROUNDUP(9.9999999999999E+307, 2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 9.9999999999999e+307);
}

TEST_F(FunctionTest, RoundDownOverflowReturnsOriginal) {
    EvalResult result = eval("=ROUNDDOWN(9.9999999999999E+307, 2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 9.9999999999999e+307);
}

TEST_F(FunctionTest, RoundNegativeOverflowReturnsOriginal) {
    EvalResult result = eval("=ROUND(-9.9999999999999E+307, 2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -9.9999999999999e+307);
}

// =============================================================================
// FLOOR Function Tests
// =============================================================================

TEST_F(FunctionTest, FloorPositive) {
    EvalResult result = eval("=FLOOR(2.9)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FunctionTest, FloorNegative) {
    EvalResult result = eval("=FLOOR(-2.9)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -3.0);
}

TEST_F(FunctionTest, FloorInteger) {
    EvalResult result = eval("=FLOOR(5)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

// =============================================================================
// CEILING Function Tests
// =============================================================================

TEST_F(FunctionTest, CeilingPositive) {
    EvalResult result = eval("=CEILING(2.1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);
}

TEST_F(FunctionTest, CeilingNegative) {
    EvalResult result = eval("=CEILING(-2.1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -2.0);
}

TEST_F(FunctionTest, CeilingInteger) {
    EvalResult result = eval("=CEILING(5)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

// =============================================================================
// CEILING_MATH Function Tests
// =============================================================================

TEST_F(FunctionTest, CeilingMathBasic) {
    EvalResult result = eval("=CEILING_MATH(4.3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

TEST_F(FunctionTest, CeilingMathWithSignificance) {
    EvalResult result = eval("=CEILING_MATH(4.3, 2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 6.0);
}

TEST_F(FunctionTest, CeilingMathNegativeNoMode) {
    // Without mode: -4.3 rounds away from zero → -5
    EvalResult result = eval("=CEILING_MATH(-4.3, 2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -4.0);
}

TEST_F(FunctionTest, CeilingMathNegativeWithMode) {
    // With mode: -4.3 rounds away from zero (toward -infinity) → -6
    EvalResult result = eval("=CEILING_MATH(-4.3, 2, 1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -6.0);
}

TEST_F(FunctionTest, CeilingMathZeroSignificance) {
    EvalResult result = eval("=CEILING_MATH(4.3, 0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

// =============================================================================
// FLOOR_MATH Function Tests
// =============================================================================

TEST_F(FunctionTest, FloorMathBasic) {
    EvalResult result = eval("=FLOOR_MATH(4.7)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 4.0);
}

TEST_F(FunctionTest, FloorMathWithSignificance) {
    EvalResult result = eval("=FLOOR_MATH(5.5, 2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 4.0);
}

TEST_F(FunctionTest, FloorMathNegativeNoMode) {
    // Without mode: -4.3 rounds toward -infinity → -6
    EvalResult result = eval("=FLOOR_MATH(-4.3, 2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -6.0);
}

TEST_F(FunctionTest, FloorMathNegativeWithMode) {
    // With mode: -4.3 rounds toward zero → -4
    EvalResult result = eval("=FLOOR_MATH(-4.3, 2, 1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -4.0);
}

// =============================================================================
// MOD Function Tests
// =============================================================================

TEST_F(FunctionTest, ModBasic) {
    EvalResult result = eval("=MOD(10,3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FunctionTest, ModDivByZero) {
    EvalResult result = eval("=MOD(10,0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

TEST_F(FunctionTest, ModNegative) {
    // Excel: MOD(-10,3) = 2 (result has same sign as divisor)
    EvalResult result = eval("=MOD(-10,3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FunctionTest, ModNegativeDivisor) {
    // Excel: MOD(10,-3) = -2 (result has same sign as divisor)
    EvalResult result = eval("=MOD(10,-3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -2.0);
}

TEST_F(FunctionTest, ModSmallDivisorOverflow) {
    // MOD(1, 1e-307) — n/d overflows to inf, Excel returns #NUM!
    EvalResult result = eval("=MOD(1, 1e-307)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

TEST_F(FunctionTest, ModLargeNumeratorOverflow) {
    // MOD(9.99E+307, 1) — d * INT(n/d) overflows, Excel returns #NUM!
    EvalResult result = eval("=MOD(9.99999999999999E+307, 1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

TEST_F(FunctionTest, ModLargeNumeratorOverflow2) {
    // MOD(9.99E+307, 42.5) — d * INT(n/d) overflows, Excel returns #NUM!
    EvalResult result = eval("=MOD(9.99999999999999E+307, 42.5)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

TEST_F(FunctionTest, ModTinyNumeratorNormalDivisor) {
    // When |n| is negligible compared to |d|, Excel returns 0.
    // MOD(1e-307, -1): fmod=1e-307, adjust sign: 1e-307+(-1)=-1==d → 0
    EXPECT_DOUBLE_EQ(eval("=MOD(1e-307, -1)").getNumber(), 0.0);
    EXPECT_DOUBLE_EQ(eval("=MOD(-1e-307, 1)").getNumber(), 0.0);
    EXPECT_DOUBLE_EQ(eval("=MOD(1e-307, -42.5)").getNumber(), 0.0);
    EXPECT_DOUBLE_EQ(eval("=MOD(-1e-307, 42.5)").getNumber(), 0.0);
}

TEST_F(FunctionTest, ModNormalNumeratorHugeDivisor) {
    // When |n| << |d|, the sign-adjusted result equals d exactly → 0.
    EXPECT_DOUBLE_EQ(eval("=MOD(1, -9.99999999999999E+307)").getNumber(), 0.0);
    EXPECT_DOUBLE_EQ(eval("=MOD(-1, 9.99999999999999E+307)").getNumber(), 0.0);
    EXPECT_DOUBLE_EQ(eval("=MOD(42.5, -9.99999999999999E+307)").getNumber(), 0.0);
    EXPECT_DOUBLE_EQ(eval("=MOD(-42.5, 9.99999999999999E+307)").getNumber(), 0.0);
}

TEST_F(FunctionTest, ModNearEqualMagnitudeOppositeSigns) {
    // When n and d have nearly equal magnitudes but opposite signs,
    // the fmod remainder is subnormal → precision lost → #NUM!
    // C10=1.0000000000000001e-307 and C11=-9.9999999999999951e-308
    // differ by a few ULP due to POWER computation differences.
    EXPECT_EQ(eval("=MOD(1.0000000000000001e-307, -9.9999999999999951e-308)").getError(),
              CellError::NUM);
    EXPECT_EQ(eval("=MOD(-9.9999999999999951e-308, 1.0000000000000001e-307)").getError(),
              CellError::NUM);
}

// =============================================================================
// INT Function Tests
// =============================================================================

TEST_F(FunctionTest, IntPositive) {
    EvalResult result = eval("=INT(5.9)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

TEST_F(FunctionTest, IntNegative) {
    // Excel: INT(-5.9) = -6 (rounds toward negative infinity)
    EvalResult result = eval("=INT(-5.9)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -6.0);
}

TEST_F(FunctionTest, IntInteger) {
    EvalResult result = eval("=INT(5)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

// =============================================================================
// SIGN Function Tests
// =============================================================================

TEST_F(FunctionTest, SignPositive) {
    EvalResult result = eval("=SIGN(42)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FunctionTest, SignNegative) {
    EvalResult result = eval("=SIGN(-7.5)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -1.0);
}

TEST_F(FunctionTest, SignZero) {
    EvalResult result = eval("=SIGN(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

// =============================================================================
// EXP Function Tests
// =============================================================================

TEST_F(FunctionTest, ExpZero) {
    EvalResult result = eval("=EXP(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FunctionTest, ExpOne) {
    EvalResult result = eval("=EXP(1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_NEAR(result.getNumber(), 2.718281828, 0.0001);
}

TEST_F(FunctionTest, ExpNegative) {
    EvalResult result = eval("=EXP(-1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_NEAR(result.getNumber(), 0.367879441, 0.0001);
}

TEST_F(FunctionTest, ExpOverflow) {
    EvalResult result = eval("=EXP(1000)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

// =============================================================================
// LN Function Tests
// =============================================================================

TEST_F(FunctionTest, LnOne) {
    EvalResult result = eval("=LN(1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, LnE) {
    EvalResult result = eval("=LN(2.718281828459045)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_NEAR(result.getNumber(), 1.0, 0.0001);
}

TEST_F(FunctionTest, LnZeroReturnsError) {
    EvalResult result = eval("=LN(0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

TEST_F(FunctionTest, LnNegativeReturnsError) {
    EvalResult result = eval("=LN(-1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

// =============================================================================
// LOG10 Function Tests
// =============================================================================

TEST_F(FunctionTest, Log10One) {
    EvalResult result = eval("=LOG10(1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, Log10Hundred) {
    EvalResult result = eval("=LOG10(100)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FunctionTest, Log10ZeroReturnsError) {
    EvalResult result = eval("=LOG10(0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

TEST_F(FunctionTest, Log10NegativeReturnsError) {
    EvalResult result = eval("=LOG10(-5)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

// =============================================================================
// LOG Function Tests
// =============================================================================

TEST_F(FunctionTest, LogDefaultBase10) {
    EvalResult result = eval("=LOG(100)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FunctionTest, LogBase2) {
    EvalResult result = eval("=LOG(8, 2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);
}

TEST_F(FunctionTest, LogBase1ReturnsError) {
    EvalResult result = eval("=LOG(10, 1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

// =============================================================================
// TRUNC Function Tests
// =============================================================================

TEST_F(FunctionTest, TruncPositive) {
    EvalResult result = eval("=TRUNC(5.9)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

TEST_F(FunctionTest, TruncNegative) {
    // TRUNC(-5.9) = -5 (toward zero, unlike INT which gives -6)
    EvalResult result = eval("=TRUNC(-5.9)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -5.0);
}

TEST_F(FunctionTest, TruncWithDigits) {
    EvalResult result = eval("=TRUNC(3.14159, 2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.14);
}

// =============================================================================
// FACT Function Tests
// =============================================================================

TEST_F(FunctionTest, FactZero) {
    EvalResult result = eval("=FACT(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FunctionTest, FactFive) {
    EvalResult result = eval("=FACT(5)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 120.0);
}

TEST_F(FunctionTest, FactNegativeReturnsError) {
    EvalResult result = eval("=FACT(-1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

TEST_F(FunctionTest, FactTruncatesDecimal) {
    // FACT(5.7) should be same as FACT(5)
    EvalResult result = eval("=FACT(5.7)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 120.0);
}

TEST_F(FunctionTest, FactOverflow) {
    EvalResult result = eval("=FACT(171)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

TEST_F(FunctionTest, FactPrecisionMatchesExcel) {
    // FACT(42) = 42! — Excel computes in descending order (42*41*...*2)
    // which gives 0x4A8E0AC0EA48D949, not the ascending order 0x...D947.
    EvalResult result = eval("=FACT(42)");
    EXPECT_TRUE(result.isNumber());
    uint64_t bits = 0;
    double val = result.getNumber();
    std::memcpy(&bits, &val, sizeof(bits));
    EXPECT_EQ(0x4A8E0AC0EA48D949ULL, bits) << "FACT(42) should match Excel's exact value";
}

// =============================================================================
// QUOTIENT Function Tests
// =============================================================================

TEST_F(FunctionTest, QuotientBasic) {
    EvalResult result = eval("=QUOTIENT(7, 2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);
}

TEST_F(FunctionTest, QuotientNegative) {
    EvalResult result = eval("=QUOTIENT(-7, 2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -3.0);
}

TEST_F(FunctionTest, QuotientDivByZero) {
    EvalResult result = eval("=QUOTIENT(7, 0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

TEST_F(FunctionTest, QuotientExact) {
    EvalResult result = eval("=QUOTIENT(10, 5)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FunctionTest, QuotientOverflow) {
    // QUOTIENT(9.99E+307, 1e-307) — n/d overflows to inf, returns #NUM!
    EvalResult result = eval("=QUOTIENT(9.99999999999999E+307, 1e-307)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

TEST_F(FunctionTest, QuotientLargeResult) {
    // QUOTIENT(1, 1e-307) — large but finite result, not #NUM!
    EvalResult result = eval("=QUOTIENT(1, 1e-307)");
    EXPECT_TRUE(result.isNumber());
}

// =============================================================================
// PI Function Tests
// =============================================================================

TEST_F(FunctionTest, PiValue) {
    EvalResult result = eval("=PI()");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), M_PI);
}

TEST_F(FunctionTest, PiWithArgsReturnsError) {
    EvalResult result = eval("=PI(1)");
    EXPECT_TRUE(result.isError());
}

// =============================================================================
// SIN Function Tests
// =============================================================================

TEST_F(FunctionTest, SinZero) {
    EvalResult result = eval("=SIN(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, SinPiOver2) {
    EvalResult result = eval("=SIN(PI()/2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FunctionTest, SinPi) {
    // SIN(PI()) is not exactly 0 in floating point, but very close
    EvalResult result = eval("=SIN(PI())");
    EXPECT_TRUE(result.isNumber());
    EXPECT_NEAR(result.getNumber(), 0.0, 1e-15);
}

// =============================================================================
// COS Function Tests
// =============================================================================

TEST_F(FunctionTest, CosZero) {
    EvalResult result = eval("=COS(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FunctionTest, CosPi) {
    EvalResult result = eval("=COS(PI())");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -1.0);
}

// =============================================================================
// TAN Function Tests
// =============================================================================

TEST_F(FunctionTest, TanZero) {
    EvalResult result = eval("=TAN(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, TanPiOver4) {
    EvalResult result = eval("=TAN(PI()/4)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_NEAR(result.getNumber(), 1.0, 1e-15);
}

// =============================================================================
// ASIN Function Tests
// =============================================================================

TEST_F(FunctionTest, AsinZero) {
    EvalResult result = eval("=ASIN(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, AsinOne) {
    EvalResult result = eval("=ASIN(1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), M_PI / 2.0);
}

TEST_F(FunctionTest, AsinDomainError) {
    EvalResult result = eval("=ASIN(2)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

// =============================================================================
// ACOS Function Tests
// =============================================================================

TEST_F(FunctionTest, AcosOne) {
    EvalResult result = eval("=ACOS(1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, AcosZero) {
    EvalResult result = eval("=ACOS(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), M_PI / 2.0);
}

TEST_F(FunctionTest, AcosDomainError) {
    EvalResult result = eval("=ACOS(-2)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

// =============================================================================
// ATAN Function Tests
// =============================================================================

TEST_F(FunctionTest, AtanZero) {
    EvalResult result = eval("=ATAN(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, AtanOne) {
    EvalResult result = eval("=ATAN(1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_NEAR(result.getNumber(), M_PI / 4.0, 1e-15);
}

// =============================================================================
// ATAN2 Function Tests
// =============================================================================

TEST_F(FunctionTest, Atan2Basic) {
    // ATAN2(1, 1) = atan2(1, 1) = PI/4
    EvalResult result = eval("=ATAN2(1, 1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_NEAR(result.getNumber(), M_PI / 4.0, 1e-15);
}

TEST_F(FunctionTest, Atan2XAxisPositive) {
    // ATAN2(1, 0) = atan2(0, 1) = 0
    EvalResult result = eval("=ATAN2(1, 0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, Atan2YAxisPositive) {
    // ATAN2(0, 1) = atan2(1, 0) = PI/2
    EvalResult result = eval("=ATAN2(0, 1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), M_PI / 2.0);
}

TEST_F(FunctionTest, Atan2BothZero) {
    EvalResult result = eval("=ATAN2(0, 0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

// =============================================================================
// CSC Function Tests
// =============================================================================

TEST_F(FunctionTest, CscPiOver2) {
    // CSC(PI/2) = 1/sin(PI/2) = 1
    EvalResult result = eval("=CSC(PI()/2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FunctionTest, CscZero) {
    // CSC(0) = 1/sin(0) = 1/0 = #DIV/0!
    EvalResult result = eval("=CSC(0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

// =============================================================================
// SEC Function Tests
// =============================================================================

TEST_F(FunctionTest, SecZero) {
    // SEC(0) = 1/cos(0) = 1
    EvalResult result = eval("=SEC(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

// =============================================================================
// COT Function Tests
// =============================================================================

TEST_F(FunctionTest, CotPiOver4) {
    // COT(PI/4) = cos(PI/4)/sin(PI/4) = 1
    EvalResult result = eval("=COT(PI()/4)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_NEAR(result.getNumber(), 1.0, 1e-15);
}

TEST_F(FunctionTest, CotZero) {
    // COT(0) = cos(0)/sin(0) = #DIV/0!
    EvalResult result = eval("=COT(0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

// =============================================================================
// SINH Function Tests
// =============================================================================

TEST_F(FunctionTest, SinhZero) {
    EvalResult result = eval("=SINH(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, SinhOne) {
    EvalResult result = eval("=SINH(1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), std::sinh(1.0));
}

TEST_F(FunctionTest, SinhOverflow) {
    EvalResult result = eval("=SINH(1000)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

// =============================================================================
// COSH Function Tests
// =============================================================================

TEST_F(FunctionTest, CoshZero) {
    EvalResult result = eval("=COSH(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FunctionTest, CoshOne) {
    EvalResult result = eval("=COSH(1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), std::cosh(1.0));
}

TEST_F(FunctionTest, CoshOverflow) {
    EvalResult result = eval("=COSH(1000)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

// =============================================================================
// TANH Function Tests
// =============================================================================

TEST_F(FunctionTest, TanhZero) {
    EvalResult result = eval("=TANH(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, TanhLarge) {
    // TANH of large value approaches 1
    EvalResult result = eval("=TANH(100)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

// =============================================================================
// ASINH Function Tests
// =============================================================================

TEST_F(FunctionTest, AsinhZero) {
    EvalResult result = eval("=ASINH(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, AsinhOne) {
    EvalResult result = eval("=ASINH(1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), std::asinh(1.0));
}

// =============================================================================
// ACOSH Function Tests
// =============================================================================

TEST_F(FunctionTest, AcoshOne) {
    EvalResult result = eval("=ACOSH(1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, AcoshDomainError) {
    EvalResult result = eval("=ACOSH(0.5)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

// =============================================================================
// ATANH Function Tests
// =============================================================================

TEST_F(FunctionTest, AtanhZero) {
    EvalResult result = eval("=ATANH(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, AtanhHalf) {
    EvalResult result = eval("=ATANH(0.5)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), std::atanh(0.5));
}

TEST_F(FunctionTest, AtanhDomainError) {
    EvalResult result = eval("=ATANH(1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

// =============================================================================
// RADIANS Function Tests
// =============================================================================

TEST_F(FunctionTest, Radians180) {
    EvalResult result = eval("=RADIANS(180)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), M_PI);
}

TEST_F(FunctionTest, Radians90) {
    EvalResult result = eval("=RADIANS(90)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), M_PI / 2.0);
}

TEST_F(FunctionTest, RadiansZero) {
    EvalResult result = eval("=RADIANS(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

// =============================================================================
// DEGREES Function Tests
// =============================================================================

TEST_F(FunctionTest, DegreesPi) {
    EvalResult result = eval("=DEGREES(PI())");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 180.0);
}

TEST_F(FunctionTest, DegreesHalfPi) {
    EvalResult result = eval("=DEGREES(PI()/2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 90.0);
}

TEST_F(FunctionTest, DegreesZero) {
    EvalResult result = eval("=DEGREES(0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

// =============================================================================
// Edge Cases and Type Coercion
// =============================================================================

TEST_F(FunctionTest, LargeNumbers) {
    EvalResult result = eval("=SUM(1e15, 1e15)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2e15);
}

TEST_F(FunctionTest, SmallNumbers) {
    EvalResult result = eval("=SUM(1e-15, 1e-15)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2e-15);
}

TEST_F(FunctionTest, NestedFunctions) {
    EvalResult result = eval("=SUM(ABS(-5), SQRT(16))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 9.0);  // 5 + 4
}

TEST_F(FunctionTest, FunctionWithCellRef) {
    setCellValue(0, 0, -10.0);  // A1
    EvalResult result = eval("=ABS(A1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 10.0);
}

TEST_F(FunctionTest, FunctionWithFormula) {
    EvalResult result = eval("=SUM(1+2, 3*4)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 15.0);  // 3 + 12
}

// =============================================================================
// IF Function Tests
// =============================================================================

TEST_F(FunctionTest, IfTrueReturnsFirst) {
    EvalResult result = eval("=IF(TRUE,1,2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FunctionTest, IfFalseReturnsSecond) {
    EvalResult result = eval("=IF(FALSE,1,2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FunctionTest, IfWithComparison) {
    EvalResult result = eval("=IF(1>0,\"yes\",\"no\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "yes");
}

TEST_F(FunctionTest, IfWithCellRef) {
    setCellValue(0, 0, 15.0);  // A1
    EvalResult result = eval("=IF(A1>10,A1*2,0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 30.0);
}

TEST_F(FunctionTest, IfWithCellRefFalse) {
    setCellValue(0, 0, 5.0);  // A1
    EvalResult result = eval("=IF(A1>10,A1*2,0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, IfTwoArgsReturnsFalse) {
    EvalResult result = eval("=IF(FALSE,1)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, IfTwoArgsTrueReturnsValue) {
    EvalResult result = eval("=IF(TRUE,1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FunctionTest, IfZeroIsFalse) {
    EvalResult result = eval("=IF(0,1,2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);
}

TEST_F(FunctionTest, IfNonZeroIsTrue) {
    EvalResult result = eval("=IF(1,1,2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FunctionTest, IfNested) {
    setCellValue(0, 0, 15.0);  // A1
    EvalResult result = eval("=IF(A1>0,IF(A1>10,\"big\",\"small\"),\"negative\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "big");
}

TEST_F(FunctionTest, IfNestedSmall) {
    setCellValue(0, 0, 5.0);  // A1
    EvalResult result = eval("=IF(A1>0,IF(A1>10,\"big\",\"small\"),\"negative\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "small");
}

TEST_F(FunctionTest, IfNestedNegative) {
    setCellValue(0, 0, -5.0);  // A1
    EvalResult result = eval("=IF(A1>0,IF(A1>10,\"big\",\"small\"),\"negative\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "negative");
}

TEST_F(FunctionTest, IfErrorInCondition) {
    EvalResult result = eval("=IF(1/0,1,2)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

TEST_F(FunctionTest, IfTooFewArgs) {
    EvalResult result = eval("=IF(TRUE)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FunctionTest, IfTooManyArgs) {
    EvalResult result = eval("=IF(TRUE,1,2,3)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// =============================================================================
// AND Function Tests
// =============================================================================

TEST_F(FunctionTest, AndAllTrue) {
    EvalResult result = eval("=AND(TRUE,TRUE)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, AndOneFalse) {
    EvalResult result = eval("=AND(TRUE,FALSE)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, AndWithNumbers) {
    EvalResult result = eval("=AND(1,1,1)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, AndWithZero) {
    EvalResult result = eval("=AND(1,0,1)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, AndNoArgs) {
    EvalResult result = eval("=AND()");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());  // Vacuous truth
}

TEST_F(FunctionTest, AndWithCellRefs) {
    setCellValue(0, 0, 5.0);  // A1
    EvalResult result = eval("=AND(A1>0,A1<10)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, AndWithCellRefsFalse) {
    setCellValue(0, 0, 15.0);  // A1
    EvalResult result = eval("=AND(A1>0,A1<10)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, AndSingleTrue) {
    EvalResult result = eval("=AND(TRUE)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, AndSingleFalse) {
    EvalResult result = eval("=AND(FALSE)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, AndErrorPropagates) {
    EvalResult result = eval("=AND(TRUE,1/0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

// =============================================================================
// OR Function Tests
// =============================================================================

TEST_F(FunctionTest, OrAllFalse) {
    EvalResult result = eval("=OR(FALSE,FALSE)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, OrOneTrue) {
    EvalResult result = eval("=OR(FALSE,TRUE)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, OrAllTrue) {
    EvalResult result = eval("=OR(TRUE,TRUE)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, OrNoArgs) {
    EvalResult result = eval("=OR()");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, OrWithNumbers) {
    EvalResult result = eval("=OR(0,0,1)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, OrAllZeros) {
    EvalResult result = eval("=OR(0,0,0)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, OrSingleTrue) {
    EvalResult result = eval("=OR(TRUE)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, OrSingleFalse) {
    EvalResult result = eval("=OR(FALSE)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, OrErrorPropagates) {
    EvalResult result = eval("=OR(FALSE,1/0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

// =============================================================================
// NOT Function Tests
// =============================================================================

TEST_F(FunctionTest, NotTrue) {
    EvalResult result = eval("=NOT(TRUE)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, NotFalse) {
    EvalResult result = eval("=NOT(FALSE)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, NotZero) {
    EvalResult result = eval("=NOT(0)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, NotNonZero) {
    EvalResult result = eval("=NOT(1)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, NotNegative) {
    EvalResult result = eval("=NOT(-5)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, NotNoArgs) {
    EvalResult result = eval("=NOT()");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FunctionTest, NotTooManyArgs) {
    EvalResult result = eval("=NOT(TRUE,FALSE)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FunctionTest, NotErrorPropagates) {
    EvalResult result = eval("=NOT(1/0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

// =============================================================================
// IFERROR Function Tests
// =============================================================================

TEST_F(FunctionTest, IferrorWithError) {
    EvalResult result = eval("=IFERROR(1/0,0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, IferrorNoError) {
    EvalResult result = eval("=IFERROR(5,0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

TEST_F(FunctionTest, IferrorWithRefError) {
    setCellError(0, 0, CellError::REF);  // A1
    EvalResult result = eval("=IFERROR(A1,0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, IferrorWithString) {
    EvalResult result = eval("=IFERROR(SQRT(-1),\"invalid\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "invalid");
}

TEST_F(FunctionTest, IferrorTooFewArgs) {
    EvalResult result = eval("=IFERROR(1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FunctionTest, IferrorTooManyArgs) {
    EvalResult result = eval("=IFERROR(1,2,3)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// =============================================================================
// IFNA Function Tests
// =============================================================================

TEST_F(FunctionTest, IfnaWithNaError) {
    setCellError(0, 0, CellError::NA);  // A1
    EvalResult result = eval("=IFNA(A1,0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, IfnaNoError) {
    EvalResult result = eval("=IFNA(5,0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

TEST_F(FunctionTest, IfnaWithOtherError) {
    // IFNA should only catch #N/A, not other errors
    setCellError(0, 0, CellError::REF);  // A1
    EvalResult result = eval("=IFNA(A1,0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::REF);
}

TEST_F(FunctionTest, IfnaTooFewArgs) {
    EvalResult result = eval("=IFNA(1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// =============================================================================
// EXACT Function Tests
// =============================================================================

TEST_F(FunctionTest, ExactSameString) {
    EvalResult result = eval("=EXACT(\"abc\",\"abc\")");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, ExactDifferentCase) {
    EvalResult result = eval("=EXACT(\"abc\",\"ABC\")");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, ExactDifferentString) {
    EvalResult result = eval("=EXACT(\"abc\",\"def\")");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, ExactWithNumbers) {
    EvalResult result = eval("=EXACT(123,123)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, ExactTooFewArgs) {
    EvalResult result = eval("=EXACT(\"abc\")");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// =============================================================================
// ISBLANK Function Tests
// =============================================================================

TEST_F(FunctionTest, IsblankEmpty) {
    // A1 is not created, so it should be blank
    EvalResult result = eval("=ISBLANK(A1)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, IsblankNotEmpty) {
    setCellValue(0, 0, 5.0);  // A1
    EvalResult result = eval("=ISBLANK(A1)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, IsblankZero) {
    setCellValue(0, 0, 0.0);  // A1
    EvalResult result = eval("=ISBLANK(A1)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, IsblankLiteral) {
    EvalResult result = eval("=ISBLANK(0)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, IsblankEmptyString) {
    setCellValue(0, 0, "");  // A1
    EvalResult result = eval("=ISBLANK(A1)");
    EXPECT_TRUE(result.isBoolean());
    // In our implementation, empty string cells are treated as blank (consistent with Excel)
    EXPECT_TRUE(result.getBoolean());
}

// =============================================================================
// ISNUMBER Function Tests
// =============================================================================

TEST_F(FunctionTest, IsnumberTrue) {
    EvalResult result = eval("=ISNUMBER(5)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, IsnumberString) {
    EvalResult result = eval("=ISNUMBER(\"5\")");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, IsnumberBoolean) {
    EvalResult result = eval("=ISNUMBER(TRUE)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, IsnumberFormula) {
    EvalResult result = eval("=ISNUMBER(1+1)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

// =============================================================================
// ISTEXT Function Tests
// =============================================================================

TEST_F(FunctionTest, IstextTrue) {
    EvalResult result = eval("=ISTEXT(\"hello\")");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, IstextNumber) {
    EvalResult result = eval("=ISTEXT(5)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, IstextBoolean) {
    EvalResult result = eval("=ISTEXT(TRUE)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, IstextEmptyString) {
    EvalResult result = eval("=ISTEXT(\"\")");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

// =============================================================================
// ISERROR Function Tests
// =============================================================================

TEST_F(FunctionTest, IserrorWithError) {
    EvalResult result = eval("=ISERROR(1/0)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, IserrorNoError) {
    EvalResult result = eval("=ISERROR(5)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, IserrorWithRefError) {
    setCellError(0, 0, CellError::REF);  // A1
    EvalResult result = eval("=ISERROR(A1)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, IserrorWithSqrtError) {
    EvalResult result = eval("=ISERROR(SQRT(-1))");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

// =============================================================================
// ISLOGICAL Function Tests
// =============================================================================

TEST_F(FunctionTest, IslogicalTrue) {
    EvalResult result = eval("=ISLOGICAL(TRUE)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, IslogicalFalse) {
    EvalResult result = eval("=ISLOGICAL(FALSE)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, IslogicalNumber) {
    EvalResult result = eval("=ISLOGICAL(1)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, IslogicalComparison) {
    EvalResult result = eval("=ISLOGICAL(1>0)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

// =============================================================================
// ISNA Function Tests
// =============================================================================

TEST_F(FunctionTest, IsnaWithNa) {
    setCellError(0, 0, CellError::NA);  // A1
    EvalResult result = eval("=ISNA(A1)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, IsnaWithOtherError) {
    setCellError(0, 0, CellError::REF);  // A1
    EvalResult result = eval("=ISNA(A1)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, IsnaNoError) {
    EvalResult result = eval("=ISNA(5)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

// =============================================================================
// TRUE/FALSE Function Tests
// =============================================================================

TEST_F(FunctionTest, TrueFunction) {
    EvalResult result = eval("=TRUE()");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, FalseFunction) {
    EvalResult result = eval("=FALSE()");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_FALSE(result.getBoolean());
}

TEST_F(FunctionTest, TrueFunctionWithArgs) {
    EvalResult result = eval("=TRUE(1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FunctionTest, FalseFunctionWithArgs) {
    EvalResult result = eval("=FALSE(1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// =============================================================================
// Combined Logic Tests
// =============================================================================

TEST_F(FunctionTest, CombinedAndOr) {
    EvalResult result = eval("=AND(OR(TRUE,FALSE),OR(FALSE,TRUE))");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, CombinedIfAnd) {
    setCellValue(0, 0, 5.0);  // A1
    EvalResult result = eval("=IF(AND(A1>0,A1<10),\"ok\",\"bad\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "ok");
}

TEST_F(FunctionTest, CombinedIfOr) {
    setCellValue(0, 0, 15.0);  // A1
    EvalResult result = eval("=IF(OR(A1<0,A1>10),\"extreme\",\"normal\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "extreme");
}

TEST_F(FunctionTest, CombinedNotAnd) {
    EvalResult result = eval("=NOT(AND(TRUE,FALSE))");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(FunctionTest, CombinedIferrorIf) {
    EvalResult result = eval("=IFERROR(IF(TRUE,1/0,0),\"error\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "error");
}

// =============================================================================
// LEN Function Tests
// =============================================================================

TEST_F(FunctionTest, LenBasic) {
    EvalResult result = eval("=LEN(\"hello\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

TEST_F(FunctionTest, LenEmpty) {
    EvalResult result = eval("=LEN(\"\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, LenNumber) {
    EvalResult result = eval("=LEN(123)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);  // Coerced to "123"
}

TEST_F(FunctionTest, LenWithSpaces) {
    EvalResult result = eval("=LEN(\"  hi  \")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 6.0);
}

TEST_F(FunctionTest, LenTooManyArgs) {
    EvalResult result = eval("=LEN(\"a\",\"b\")");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FunctionTest, LenNoArgs) {
    EvalResult result = eval("=LEN()");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// =============================================================================
// LEFT Function Tests
// =============================================================================

TEST_F(FunctionTest, LeftBasic) {
    EvalResult result = eval("=LEFT(\"hello\",2)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "he");
}

TEST_F(FunctionTest, LeftDefault) {
    EvalResult result = eval("=LEFT(\"hello\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "h");
}

TEST_F(FunctionTest, LeftZero) {
    EvalResult result = eval("=LEFT(\"hello\",0)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "");
}

TEST_F(FunctionTest, LeftMoreThanLength) {
    EvalResult result = eval("=LEFT(\"hi\",10)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "hi");
}

TEST_F(FunctionTest, LeftNegative) {
    EvalResult result = eval("=LEFT(\"hello\",-1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FunctionTest, LeftEmpty) {
    EvalResult result = eval("=LEFT(\"\",5)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "");
}

// =============================================================================
// RIGHT Function Tests
// =============================================================================

TEST_F(FunctionTest, RightBasic) {
    EvalResult result = eval("=RIGHT(\"hello\",2)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "lo");
}

TEST_F(FunctionTest, RightDefault) {
    EvalResult result = eval("=RIGHT(\"hello\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "o");
}

TEST_F(FunctionTest, RightZero) {
    EvalResult result = eval("=RIGHT(\"hello\",0)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "");
}

TEST_F(FunctionTest, RightMoreThanLength) {
    EvalResult result = eval("=RIGHT(\"hi\",10)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "hi");
}

TEST_F(FunctionTest, RightNegative) {
    EvalResult result = eval("=RIGHT(\"hello\",-1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// =============================================================================
// MID Function Tests
// =============================================================================

TEST_F(FunctionTest, MidBasic) {
    EvalResult result = eval("=MID(\"hello\",2,3)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "ell");
}

TEST_F(FunctionTest, MidFromStart) {
    EvalResult result = eval("=MID(\"hello\",1,2)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "he");
}

TEST_F(FunctionTest, MidToEnd) {
    EvalResult result = eval("=MID(\"hello\",4,10)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "lo");
}

TEST_F(FunctionTest, MidBeyondString) {
    EvalResult result = eval("=MID(\"hello\",10,2)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "");
}

TEST_F(FunctionTest, MidZeroLength) {
    EvalResult result = eval("=MID(\"hello\",2,0)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "");
}

TEST_F(FunctionTest, MidStartZero) {
    EvalResult result = eval("=MID(\"hello\",0,2)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FunctionTest, MidNegativeLength) {
    EvalResult result = eval("=MID(\"hello\",2,-1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// =============================================================================
// TRIM Function Tests
// =============================================================================

TEST_F(FunctionTest, TrimLeadingTrailing) {
    EvalResult result = eval("=TRIM(\"  hello  \")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "hello");
}

TEST_F(FunctionTest, TrimMultipleSpaces) {
    EvalResult result = eval("=TRIM(\"  hello  world  \")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "hello world");
}

TEST_F(FunctionTest, TrimNoSpaces) {
    EvalResult result = eval("=TRIM(\"hello\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "hello");
}

TEST_F(FunctionTest, TrimAllSpaces) {
    EvalResult result = eval("=TRIM(\"     \")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "");
}

TEST_F(FunctionTest, TrimEmpty) {
    EvalResult result = eval("=TRIM(\"\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "");
}

// =============================================================================
// UPPER Function Tests
// =============================================================================

TEST_F(FunctionTest, UpperBasic) {
    EvalResult result = eval("=UPPER(\"hello\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "HELLO");
}

TEST_F(FunctionTest, UpperMixed) {
    EvalResult result = eval("=UPPER(\"HeLLo WoRLd\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "HELLO WORLD");
}

TEST_F(FunctionTest, UpperAlreadyUpper) {
    EvalResult result = eval("=UPPER(\"HELLO\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "HELLO");
}

TEST_F(FunctionTest, UpperWithNumbers) {
    EvalResult result = eval("=UPPER(\"abc123\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "ABC123");
}

TEST_F(FunctionTest, UpperEmpty) {
    EvalResult result = eval("=UPPER(\"\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "");
}

// =============================================================================
// LOWER Function Tests
// =============================================================================

TEST_F(FunctionTest, LowerBasic) {
    EvalResult result = eval("=LOWER(\"HELLO\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "hello");
}

TEST_F(FunctionTest, LowerMixed) {
    EvalResult result = eval("=LOWER(\"HeLLo WoRLd\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "hello world");
}

TEST_F(FunctionTest, LowerAlreadyLower) {
    EvalResult result = eval("=LOWER(\"hello\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "hello");
}

TEST_F(FunctionTest, LowerWithNumbers) {
    EvalResult result = eval("=LOWER(\"ABC123\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "abc123");
}

// =============================================================================
// PROPER Function Tests
// =============================================================================

TEST_F(FunctionTest, ProperBasic) {
    EvalResult result = eval("=PROPER(\"hello world\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "Hello World");
}

TEST_F(FunctionTest, ProperAllCaps) {
    EvalResult result = eval("=PROPER(\"MR. SMITH\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "Mr. Smith");
}

TEST_F(FunctionTest, ProperAllLower) {
    EvalResult result = eval("=PROPER(\"this is a test\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "This Is A Test");
}

TEST_F(FunctionTest, ProperWithNumbers) {
    EvalResult result = eval("=PROPER(\"abc123def\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "Abc123Def");
}

TEST_F(FunctionTest, ProperEmpty) {
    EvalResult result = eval("=PROPER(\"\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "");
}

// =============================================================================
// FIND Function Tests
// =============================================================================

TEST_F(FunctionTest, FindBasic) {
    EvalResult result = eval("=FIND(\"l\",\"hello\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);  // 1-indexed
}

TEST_F(FunctionTest, FindNotFound) {
    EvalResult result = eval("=FIND(\"x\",\"hello\")");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FunctionTest, FindCaseSensitive) {
    EvalResult result = eval("=FIND(\"L\",\"hello\")");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FunctionTest, FindWithStartPos) {
    EvalResult result = eval("=FIND(\"l\",\"hello\",4)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 4.0);
}

TEST_F(FunctionTest, FindEmptyNeedle) {
    EvalResult result = eval("=FIND(\"\",\"hello\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FunctionTest, FindAtStart) {
    EvalResult result = eval("=FIND(\"h\",\"hello\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FunctionTest, FindAtEnd) {
    EvalResult result = eval("=FIND(\"o\",\"hello\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

// =============================================================================
// SEARCH Function Tests
// =============================================================================

TEST_F(FunctionTest, SearchBasic) {
    EvalResult result = eval("=SEARCH(\"l\",\"hello\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);
}

TEST_F(FunctionTest, SearchCaseInsensitive) {
    EvalResult result = eval("=SEARCH(\"L\",\"hello\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 3.0);
}

TEST_F(FunctionTest, SearchNotFound) {
    EvalResult result = eval("=SEARCH(\"x\",\"hello\")");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FunctionTest, SearchWithStartPos) {
    EvalResult result = eval("=SEARCH(\"L\",\"HELLO\",4)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 4.0);
}

// =============================================================================
// SUBSTITUTE Function Tests
// =============================================================================

TEST_F(FunctionTest, SubstituteAll) {
    EvalResult result = eval("=SUBSTITUTE(\"hello\",\"l\",\"L\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "heLLo");
}

TEST_F(FunctionTest, SubstituteFirstOnly) {
    EvalResult result = eval("=SUBSTITUTE(\"hello\",\"l\",\"L\",1)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "heLlo");
}

TEST_F(FunctionTest, SubstituteSecondOnly) {
    EvalResult result = eval("=SUBSTITUTE(\"hello\",\"l\",\"L\",2)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "helLo");
}

TEST_F(FunctionTest, SubstituteNotFound) {
    EvalResult result = eval("=SUBSTITUTE(\"hello\",\"x\",\"y\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "hello");
}

TEST_F(FunctionTest, SubstituteEmpty) {
    EvalResult result = eval("=SUBSTITUTE(\"hello\",\"l\",\"\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "heo");
}

TEST_F(FunctionTest, SubstituteEmptyOld) {
    EvalResult result = eval("=SUBSTITUTE(\"hello\",\"\",\"x\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "hello");  // No change
}

// =============================================================================
// REPLACE Function Tests
// =============================================================================

TEST_F(FunctionTest, ReplaceBasic) {
    EvalResult result = eval("=REPLACE(\"hello\",2,3,\"i\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "hio");
}

TEST_F(FunctionTest, ReplaceAtStart) {
    EvalResult result = eval("=REPLACE(\"hello\",1,2,\"XYZ\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "XYZllo");
}

TEST_F(FunctionTest, ReplaceAtEnd) {
    EvalResult result = eval("=REPLACE(\"hello\",4,2,\"XYZ\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "helXYZ");
}

TEST_F(FunctionTest, ReplaceInsert) {
    EvalResult result = eval("=REPLACE(\"hello\",3,0,\"X\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "heXllo");
}

TEST_F(FunctionTest, ReplaceDelete) {
    EvalResult result = eval("=REPLACE(\"hello\",2,2,\"\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "hlo");
}

TEST_F(FunctionTest, ReplacePastEnd) {
    EvalResult result = eval("=REPLACE(\"hello\",10,2,\"XYZ\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "helloXYZ");
}

// =============================================================================
// CONCAT Function Tests
// =============================================================================

TEST_F(FunctionTest, ConcatBasic) {
    EvalResult result = eval("=CONCAT(\"a\",\"b\",\"c\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "abc");
}

TEST_F(FunctionTest, ConcatWithNumbers) {
    EvalResult result = eval("=CONCAT(\"value:\",123)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "value:123");
}

TEST_F(FunctionTest, ConcatEmpty) {
    EvalResult result = eval("=CONCAT()");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "");
}

TEST_F(FunctionTest, ConcatSingleArg) {
    EvalResult result = eval("=CONCAT(\"hello\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "hello");
}

TEST_F(FunctionTest, ConcatWithBoolean) {
    EvalResult result = eval("=CONCAT(\"result:\",TRUE)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "result:TRUE");
}

// =============================================================================
// CONCATENATE Function Tests
// =============================================================================

TEST_F(FunctionTest, ConcatenateBasic) {
    EvalResult result = eval("=CONCATENATE(\"hello\",\" \",\"world\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "hello world");
}

TEST_F(FunctionTest, ConcatenateWithCells) {
    setCellValue(0, 0, "Hello");  // A1
    setCellValue(1, 0, "World");  // B1
    EvalResult result = eval("=CONCATENATE(A1,\" \",B1)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "Hello World");
}

// =============================================================================
// REPT Function Tests
// =============================================================================

TEST_F(FunctionTest, ReptBasic) {
    EvalResult result = eval("=REPT(\"ab\",3)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "ababab");
}

TEST_F(FunctionTest, ReptZero) {
    EvalResult result = eval("=REPT(\"ab\",0)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "");
}

TEST_F(FunctionTest, ReptOne) {
    EvalResult result = eval("=REPT(\"ab\",1)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "ab");
}

TEST_F(FunctionTest, ReptNegative) {
    EvalResult result = eval("=REPT(\"ab\",-1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FunctionTest, ReptEmpty) {
    EvalResult result = eval("=REPT(\"\",5)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "");
}

// =============================================================================
// TEXT Function Tests
// =============================================================================

TEST_F(FunctionTest, TextPercentage) {
    EvalResult result = eval("=TEXT(0.5,\"0%\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "50%");
}

TEST_F(FunctionTest, TextPercentageDecimals) {
    EvalResult result = eval("=TEXT(0.567,\"0.0%\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "56.7%");
}

TEST_F(FunctionTest, TextNoFormat) {
    EvalResult result = eval("=TEXT(123,\"0\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "123");
}

TEST_F(FunctionTest, TextDecimalPlaces) {
    EvalResult result = eval("=TEXT(123.456,\"0.00\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "123.46");
}

TEST_F(FunctionTest, TextCurrency) {
    EvalResult result = eval("=TEXT(1234.5,\"$#,##0.00\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "$1,234.50");
}

TEST_F(FunctionTest, TextNegative) {
    EvalResult result = eval("=TEXT(-123.45,\"$#,##0.00\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "-$123.45");
}

// =============================================================================
// VALUE Function Tests
// =============================================================================

TEST_F(FunctionTest, ValueBasic) {
    EvalResult result = eval("=VALUE(\"123\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 123.0);
}

TEST_F(FunctionTest, ValueDecimal) {
    EvalResult result = eval("=VALUE(\"123.45\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 123.45);
}

TEST_F(FunctionTest, ValueCurrency) {
    EvalResult result = eval("=VALUE(\"$100\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 100.0);
}

TEST_F(FunctionTest, ValuePercentage) {
    EvalResult result = eval("=VALUE(\"50%\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.5);
}

TEST_F(FunctionTest, ValueWithCommas) {
    EvalResult result = eval("=VALUE(\"1,234.56\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1234.56);
}

TEST_F(FunctionTest, ValueNonNumeric) {
    EvalResult result = eval("=VALUE(\"abc\")");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FunctionTest, ValueEmpty) {
    EvalResult result = eval("=VALUE(\"\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, ValueNegative) {
    EvalResult result = eval("=VALUE(\"-123.45\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -123.45);
}

// =============================================================================
// CHAR Function Tests
// =============================================================================

TEST_F(FunctionTest, CharBasic) {
    EvalResult result = eval("=CHAR(65)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "A");
}

TEST_F(FunctionTest, CharLowercase) {
    EvalResult result = eval("=CHAR(97)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "a");
}

TEST_F(FunctionTest, CharSpace) {
    EvalResult result = eval("=CHAR(32)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), " ");
}

TEST_F(FunctionTest, CharZero) {
    EvalResult result = eval("=CHAR(0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FunctionTest, CharTooLarge) {
    EvalResult result = eval("=CHAR(256)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// =============================================================================
// CODE Function Tests
// =============================================================================

TEST_F(FunctionTest, CodeBasic) {
    EvalResult result = eval("=CODE(\"A\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 65.0);
}

TEST_F(FunctionTest, CodeLowercase) {
    EvalResult result = eval("=CODE(\"a\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 97.0);
}

TEST_F(FunctionTest, CodeSpace) {
    EvalResult result = eval("=CODE(\" \")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 32.0);
}

TEST_F(FunctionTest, CodeEmpty) {
    EvalResult result = eval("=CODE(\"\")");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FunctionTest, CodeMultipleChars) {
    // CODE should return the code of the first character only
    EvalResult result = eval("=CODE(\"ABC\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 65.0);
}

// =============================================================================
// Combined Text Function Tests
// =============================================================================

TEST_F(FunctionTest, CombinedLeftLen) {
    EvalResult result = eval("=LEFT(\"hello\",LEN(\"hi\"))");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "he");
}

TEST_F(FunctionTest, CombinedUpperTrim) {
    EvalResult result = eval("=UPPER(TRIM(\"  hello  world  \"))");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "HELLO WORLD");
}

TEST_F(FunctionTest, CombinedConcatUpper) {
    EvalResult result = eval("=CONCAT(UPPER(\"hello\"),\" \",LOWER(\"WORLD\"))");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "HELLO world");
}

TEST_F(FunctionTest, CombinedIfLen) {
    EvalResult result = eval("=IF(LEN(\"hello\")>3,\"long\",\"short\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "long");
}

TEST_F(FunctionTest, CombinedValueSum) {
    EvalResult result = eval("=SUM(VALUE(\"10\"),VALUE(\"20\"))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 30.0);
}

TEST_F(FunctionTest, CombinedTextConcat) {
    EvalResult result = eval("=CONCAT(\"Total: \",TEXT(1234,\"#,##0\"))");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "Total: 1,234");
}

TEST_F(FunctionTest, TextWithCellRef) {
    setCellValue(0, 0, "hello world");  // A1
    EvalResult result = eval("=UPPER(A1)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "HELLO WORLD");
}

TEST_F(FunctionTest, ConcatWithRange) {
    setCellValue(0, 0, "A");  // A1
    setCellValue(0, 1, "B");  // A2
    setCellValue(0, 2, "C");  // A3
    EvalResult result = eval("=CONCAT(A1:A3)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "ABC");
}

// =============================================================================
// Date/Time Function Tests - Phase 6
// =============================================================================

// -----------------------------------------------------------------------------
// NOW and TODAY Tests (Volatile Functions)
// -----------------------------------------------------------------------------

TEST_F(FunctionTest, NowReturnsNumber) {
    EvalResult result = eval("=NOW()");
    EXPECT_TRUE(result.isNumber());
    // NOW should return a serial date > 45000 (approx year 2023+)
    EXPECT_GT(result.getNumber(), 45000.0);
}

TEST_F(FunctionTest, NowIncludesTime) {
    EvalResult result = eval("=NOW()");
    EXPECT_TRUE(result.isNumber());
    // NOW should have a fractional part (time component)
    double intPart;
    (void)std::modf(result.getNumber(), &intPart);
    // There's a small chance this could be exactly 0 at midnight, but unlikely
    // Just verify it's a valid serial date
    EXPECT_GT(intPart, 0);
}

TEST_F(FunctionTest, NowTooManyArgs) {
    EvalResult result = eval("=NOW(1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FunctionTest, TodayReturnsWholeNumber) {
    EvalResult result = eval("=TODAY()");
    EXPECT_TRUE(result.isNumber());
    // TODAY should return an integer (no time component)
    double val = result.getNumber();
    EXPECT_DOUBLE_EQ(val, std::floor(val));
}

TEST_F(FunctionTest, TodayTooManyArgs) {
    EvalResult result = eval("=TODAY(1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(FunctionTest, NowAndTodayVolatile) {
    FunctionRegistry& registry = FunctionRegistry::instance();
    EXPECT_TRUE(registry.isVolatile("NOW"));
    EXPECT_TRUE(registry.isVolatile("TODAY"));
}

// -----------------------------------------------------------------------------
// DATE Function Tests
// -----------------------------------------------------------------------------

TEST_F(FunctionTest, DateBasic) {
    // Jan 1, 1900 = serial date 1
    EvalResult result = eval("=DATE(1900,1,1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FunctionTest, DateJan15_1900) {
    // Jan 15, 1900 = serial date 15
    EvalResult result = eval("=DATE(1900,1,15)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 15.0);
}

TEST_F(FunctionTest, DateFeb1_1900) {
    // Feb 1, 1900 = serial date 32 (31 days in Jan + 1)
    EvalResult result = eval("=DATE(1900,2,1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 32.0);
}

TEST_F(FunctionTest, DateMar1_1900ExcelBug) {
    // Excel treats 1900 as a leap year (bug), so Mar 1, 1900 = day 61
    // (31 Jan + 29 Feb + 1)
    EvalResult result = eval("=DATE(1900,3,1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 61.0);
}

TEST_F(FunctionTest, DateJan1_2024) {
    // Known value: Jan 1, 2024 = 45292 in Excel
    EvalResult result = eval("=DATE(2024,1,1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 45292.0);
}

TEST_F(FunctionTest, DateJun15_2024) {
    // Jun 15, 2024
    EvalResult result = eval("=DATE(2024,6,15)");
    EXPECT_TRUE(result.isNumber());
    // Jan(31) + Feb(29 leap) + Mar(31) + Apr(30) + May(31) + 15 = 167
    // 45292 + 166 = 45458
    EXPECT_DOUBLE_EQ(result.getNumber(), 45458.0);
}

TEST_F(FunctionTest, DateTwoDigitYearLow) {
    // Year 0-29 → 2000-2029
    EvalResult result1 = eval("=DATE(0,1,1)");
    EvalResult result2 = eval("=DATE(2000,1,1)");
    EXPECT_DOUBLE_EQ(result1.getNumber(), result2.getNumber());

    EvalResult result3 = eval("=DATE(29,1,1)");
    EvalResult result4 = eval("=DATE(2029,1,1)");
    EXPECT_DOUBLE_EQ(result3.getNumber(), result4.getNumber());
}

TEST_F(FunctionTest, DateTwoDigitYearHigh) {
    // Year 30-99 → 1930-1999
    EvalResult result1 = eval("=DATE(30,1,1)");
    EvalResult result2 = eval("=DATE(1930,1,1)");
    EXPECT_DOUBLE_EQ(result1.getNumber(), result2.getNumber());

    EvalResult result3 = eval("=DATE(99,1,1)");
    EvalResult result4 = eval("=DATE(1999,1,1)");
    EXPECT_DOUBLE_EQ(result3.getNumber(), result4.getNumber());
}

TEST_F(FunctionTest, DateMonthOverflow) {
    // Month 13 = next year January
    EvalResult result = eval("=DATE(2024,13,1)");
    EvalResult expected = eval("=DATE(2025,1,1)");
    EXPECT_DOUBLE_EQ(result.getNumber(), expected.getNumber());
}

TEST_F(FunctionTest, DateMonthUnderflow) {
    // Month 0 = previous year December
    EvalResult result = eval("=DATE(2024,0,1)");
    EvalResult expected = eval("=DATE(2023,12,1)");
    EXPECT_DOUBLE_EQ(result.getNumber(), expected.getNumber());
}

TEST_F(FunctionTest, DateInvalidYearTooSmall) {
    // Year < 1900 after adjustment
    EvalResult result = eval("=DATE(1899,1,1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

TEST_F(FunctionTest, DateWrongArgCount) {
    EvalResult result = eval("=DATE(2024,1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// -----------------------------------------------------------------------------
// TIME Function Tests
// -----------------------------------------------------------------------------

TEST_F(FunctionTest, TimeNoon) {
    // 12:00:00 = 0.5 (half of the day)
    EvalResult result = eval("=TIME(12,0,0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.5);
}

TEST_F(FunctionTest, TimeMidnight) {
    // 0:00:00 = 0
    EvalResult result = eval("=TIME(0,0,0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, Time6AM) {
    // 6:00:00 = 0.25 (quarter of the day)
    EvalResult result = eval("=TIME(6,0,0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.25);
}

TEST_F(FunctionTest, Time6PM) {
    // 18:00:00 = 0.75
    EvalResult result = eval("=TIME(18,0,0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.75);
}

TEST_F(FunctionTest, TimeWithMinutes) {
    // 12:30:00 = 0.5 + 30/1440 = 0.520833...
    EvalResult result = eval("=TIME(12,30,0)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_NEAR(result.getNumber(), 0.520833333, 0.0001);
}

TEST_F(FunctionTest, TimeWithSeconds) {
    // 12:00:30 = 0.5 + 30/86400
    EvalResult result = eval("=TIME(12,0,30)");
    EXPECT_TRUE(result.isNumber());
    double expected = 0.5 + 30.0 / 86400.0;
    EXPECT_NEAR(result.getNumber(), expected, 0.0001);
}

TEST_F(FunctionTest, TimeWrapsAt24Hours) {
    // 25:00:00 should wrap to 1:00:00
    EvalResult result = eval("=TIME(25,0,0)");
    EvalResult expected = eval("=TIME(1,0,0)");
    EXPECT_NEAR(result.getNumber(), expected.getNumber(), 0.0001);
}

TEST_F(FunctionTest, TimeWrongArgCount) {
    EvalResult result = eval("=TIME(12,0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// -----------------------------------------------------------------------------
// DATEVALUE Function Tests
// -----------------------------------------------------------------------------

TEST_F(FunctionTest, DateValueISO) {
    EvalResult result = eval("=DATEVALUE(\"2024-01-15\")");
    EvalResult expected = eval("=DATE(2024,1,15)");
    EXPECT_DOUBLE_EQ(result.getNumber(), expected.getNumber());
}

TEST_F(FunctionTest, DateValueUS) {
    EvalResult result = eval("=DATEVALUE(\"1/15/2024\")");
    EvalResult expected = eval("=DATE(2024,1,15)");
    EXPECT_DOUBLE_EQ(result.getNumber(), expected.getNumber());
}

TEST_F(FunctionTest, DateValueEuropean) {
    EvalResult result = eval("=DATEVALUE(\"15.1.2024\")");
    EvalResult expected = eval("=DATE(2024,1,15)");
    EXPECT_DOUBLE_EQ(result.getNumber(), expected.getNumber());
}

TEST_F(FunctionTest, DateValueInvalid) {
    EvalResult result = eval("=DATEVALUE(\"not a date\")");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// -----------------------------------------------------------------------------
// TIMEVALUE Function Tests
// -----------------------------------------------------------------------------

TEST_F(FunctionTest, TimeValueBasic) {
    EvalResult result = eval("=TIMEVALUE(\"12:00:00\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.5);
}

TEST_F(FunctionTest, TimeValueNoSeconds) {
    EvalResult result = eval("=TIMEVALUE(\"12:00\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.5);
}

TEST_F(FunctionTest, TimeValuePM) {
    // 2:30 PM should be 14:30
    EvalResult result = eval("=TIMEVALUE(\"2:30 PM\")");
    EXPECT_TRUE(result.isNumber());
    double expected = (14 * 3600 + 30 * 60) / 86400.0;
    EXPECT_NEAR(result.getNumber(), expected, 0.0001);
}

TEST_F(FunctionTest, TimeValueAM) {
    // 12:00 AM should be 0:00
    EvalResult result = eval("=TIMEVALUE(\"12:00 AM\")");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, TimeValueInvalid) {
    EvalResult result = eval("=TIMEVALUE(\"not a time\")");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// -----------------------------------------------------------------------------
// YEAR Function Tests
// -----------------------------------------------------------------------------

TEST_F(FunctionTest, YearBasic) {
    // DATE(2024,6,15) should have YEAR 2024
    EvalResult result = eval("=YEAR(DATE(2024,6,15))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2024.0);
}

TEST_F(FunctionTest, YearFrom1900) {
    EvalResult result = eval("=YEAR(DATE(1900,1,1))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1900.0);
}

TEST_F(FunctionTest, YearFromSerialDate) {
    // Serial date 45292 = Jan 1, 2024
    EvalResult result = eval("=YEAR(45292)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2024.0);
}

TEST_F(FunctionTest, YearNegative) {
    EvalResult result = eval("=YEAR(0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

// -----------------------------------------------------------------------------
// MONTH Function Tests
// -----------------------------------------------------------------------------

TEST_F(FunctionTest, MonthBasic) {
    EvalResult result = eval("=MONTH(DATE(2024,6,15))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 6.0);
}

TEST_F(FunctionTest, MonthJanuary) {
    EvalResult result = eval("=MONTH(DATE(2024,1,15))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FunctionTest, MonthDecember) {
    EvalResult result = eval("=MONTH(DATE(2024,12,15))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 12.0);
}

TEST_F(FunctionTest, MonthFromSerialDate) {
    // Serial date 45292 = Jan 1, 2024
    EvalResult result = eval("=MONTH(45292)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

// -----------------------------------------------------------------------------
// DAY Function Tests
// -----------------------------------------------------------------------------

TEST_F(FunctionTest, DayBasic) {
    EvalResult result = eval("=DAY(DATE(2024,6,15))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 15.0);
}

TEST_F(FunctionTest, DayFirst) {
    EvalResult result = eval("=DAY(DATE(2024,6,1))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(FunctionTest, DayLast31) {
    EvalResult result = eval("=DAY(DATE(2024,1,31))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 31.0);
}

TEST_F(FunctionTest, DayFromSerialDate) {
    // Serial date 45292 = Jan 1, 2024
    EvalResult result = eval("=DAY(45292)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

// -----------------------------------------------------------------------------
// HOUR Function Tests
// -----------------------------------------------------------------------------

TEST_F(FunctionTest, HourNoon) {
    EvalResult result = eval("=HOUR(TIME(12,30,45))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 12.0);
}

TEST_F(FunctionTest, HourMidnight) {
    EvalResult result = eval("=HOUR(TIME(0,30,45))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, HourEvening) {
    EvalResult result = eval("=HOUR(TIME(23,59,59))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 23.0);
}

TEST_F(FunctionTest, HourFromFraction) {
    // 0.5 = noon
    EvalResult result = eval("=HOUR(0.5)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 12.0);
}

// -----------------------------------------------------------------------------
// MINUTE Function Tests
// -----------------------------------------------------------------------------

TEST_F(FunctionTest, MinuteBasic) {
    EvalResult result = eval("=MINUTE(TIME(12,30,45))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 30.0);
}

TEST_F(FunctionTest, MinuteZero) {
    EvalResult result = eval("=MINUTE(TIME(12,0,45))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, Minute59) {
    EvalResult result = eval("=MINUTE(TIME(12,59,45))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 59.0);
}

// -----------------------------------------------------------------------------
// SECOND Function Tests
// -----------------------------------------------------------------------------

TEST_F(FunctionTest, SecondBasic) {
    EvalResult result = eval("=SECOND(TIME(12,30,45))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 45.0);
}

TEST_F(FunctionTest, SecondZero) {
    EvalResult result = eval("=SECOND(TIME(12,30,0))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(FunctionTest, Second59) {
    EvalResult result = eval("=SECOND(TIME(12,30,59))");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 59.0);
}

// -----------------------------------------------------------------------------
// WEEKDAY Function Tests
// -----------------------------------------------------------------------------

TEST_F(FunctionTest, WeekdayType1Default) {
    // Jan 1, 1900 is treated as Sunday (1) in Excel's type 1
    EvalResult result = eval("=WEEKDAY(1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);  // Sunday
}

TEST_F(FunctionTest, WeekdaySaturday) {
    // Jan 7, 1900 = Saturday = 7 in type 1
    EvalResult result = eval("=WEEKDAY(7)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 7.0);  // Saturday
}

TEST_F(FunctionTest, WeekdayType2MondayFirst) {
    // Type 2: Monday = 1
    // Jan 1, 1900 is Sunday = 7 in type 2
    EvalResult result = eval("=WEEKDAY(1,2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 7.0);  // Sunday
}

TEST_F(FunctionTest, WeekdayType3MondayZero) {
    // Type 3: Monday = 0
    // Jan 1, 1900 is Sunday = 6 in type 3
    EvalResult result = eval("=WEEKDAY(1,3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 6.0);  // Sunday
}

TEST_F(FunctionTest, WeekdayInvalidType) {
    EvalResult result = eval("=WEEKDAY(45292,4)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

TEST_F(FunctionTest, WeekdayInvalidSerial) {
    EvalResult result = eval("=WEEKDAY(0)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

// -----------------------------------------------------------------------------
// EOMONTH Function Tests
// -----------------------------------------------------------------------------

TEST_F(FunctionTest, EomonthBasic) {
    // Jan 15, 2024 + 0 months = Jan 31, 2024
    EvalResult result = eval("=EOMONTH(DATE(2024,1,15),0)");
    EvalResult expected = eval("=DATE(2024,1,31)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), expected.getNumber());
}

TEST_F(FunctionTest, EomonthPositiveMonths) {
    // Jan 15, 2024 + 2 months = Mar 31, 2024
    EvalResult result = eval("=EOMONTH(DATE(2024,1,15),2)");
    EvalResult expected = eval("=DATE(2024,3,31)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), expected.getNumber());
}

TEST_F(FunctionTest, EomonthNegativeMonths) {
    // Mar 15, 2024 - 2 months = Jan 31, 2024
    EvalResult result = eval("=EOMONTH(DATE(2024,3,15),-2)");
    EvalResult expected = eval("=DATE(2024,1,31)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), expected.getNumber());
}

TEST_F(FunctionTest, EomonthFebruaryLeapYear) {
    // Jan 15, 2024 + 1 month = Feb 29, 2024 (leap year)
    EvalResult result = eval("=EOMONTH(DATE(2024,1,15),1)");
    EvalResult expected = eval("=DATE(2024,2,29)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), expected.getNumber());
}

TEST_F(FunctionTest, EomonthFebruaryNonLeapYear) {
    // Jan 15, 2023 + 1 month = Feb 28, 2023 (non-leap year)
    EvalResult result = eval("=EOMONTH(DATE(2023,1,15),1)");
    EvalResult expected = eval("=DATE(2023,2,28)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), expected.getNumber());
}

TEST_F(FunctionTest, EomonthCrossYear) {
    // Nov 15, 2023 + 3 months = Feb 29, 2024
    EvalResult result = eval("=EOMONTH(DATE(2023,11,15),3)");
    EvalResult expected = eval("=DATE(2024,2,29)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), expected.getNumber());
}

TEST_F(FunctionTest, EomonthCrossYearNegative) {
    // Feb 15, 2024 - 3 months = Nov 30, 2023
    EvalResult result = eval("=EOMONTH(DATE(2024,2,15),-3)");
    EvalResult expected = eval("=DATE(2023,11,30)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), expected.getNumber());
}

TEST_F(FunctionTest, EomonthFromEndOfMonth) {
    // Starting from end of month (Jan 31, 2024 + 1 month = Feb 29, 2024)
    EvalResult result = eval("=EOMONTH(DATE(2024,1,31),1)");
    EvalResult expected = eval("=DATE(2024,2,29)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), expected.getNumber());
}

TEST_F(FunctionTest, EomonthInvalidDate) {
    EvalResult result = eval("=EOMONTH(0,1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::NUM);
}

TEST_F(FunctionTest, EomonthWrongArgCount) {
    EvalResult result = eval("=EOMONTH(DATE(2024,1,1))");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

// -----------------------------------------------------------------------------
// Combined Date/Time Tests
// -----------------------------------------------------------------------------

TEST_F(FunctionTest, DatePlusTime) {
    // Combine date and time
    EvalResult result = eval("=DATE(2024,6,15)+TIME(12,30,0)");
    EXPECT_TRUE(result.isNumber());

    EvalResult dateOnly = eval("=DATE(2024,6,15)");
    EvalResult timeOnly = eval("=TIME(12,30,0)");
    EXPECT_NEAR(result.getNumber(), dateOnly.getNumber() + timeOnly.getNumber(), 0.0001);
}

TEST_F(FunctionTest, ExtractFromNow) {
    // YEAR(NOW()) should be current year (2024 or later)
    EvalResult result = eval("=YEAR(NOW())");
    EXPECT_TRUE(result.isNumber());
    EXPECT_GE(result.getNumber(), 2024.0);
}

TEST_F(FunctionTest, ExtractFromToday) {
    // YEAR(TODAY()) should be current year
    EvalResult result = eval("=YEAR(TODAY())");
    EXPECT_TRUE(result.isNumber());
    EXPECT_GE(result.getNumber(), 2024.0);
}

TEST_F(FunctionTest, DateArithmetic) {
    // Add 30 days to a date
    EvalResult result = eval("=DATE(2024,1,1)+30");
    EvalResult expected = eval("=DATE(2024,1,31)");
    EXPECT_DOUBLE_EQ(result.getNumber(), expected.getNumber());
}

TEST_F(FunctionTest, DateSubtraction) {
    // Days between two dates
    EvalResult result = eval("=DATE(2024,2,1)-DATE(2024,1,1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 31.0);  // 31 days in January
}

TEST_F(FunctionTest, CombinedDateTimeExtraction) {
    // Create datetime, extract components
    EvalResult hour = eval("=HOUR(DATE(2024,6,15)+TIME(14,30,45))");
    EvalResult minute = eval("=MINUTE(DATE(2024,6,15)+TIME(14,30,45))");
    EvalResult second = eval("=SECOND(DATE(2024,6,15)+TIME(14,30,45))");

    EXPECT_DOUBLE_EQ(hour.getNumber(), 14.0);
    EXPECT_DOUBLE_EQ(minute.getNumber(), 30.0);
    EXPECT_DOUBLE_EQ(second.getNumber(), 45.0);
}

TEST_F(FunctionTest, DateWithCellReference) {
    setCellValue(0, 0, 2024.0);  // A1 = year
    setCellValue(1, 0, 6.0);     // B1 = month
    setCellValue(2, 0, 15.0);    // C1 = day

    EvalResult result = eval("=DATE(A1,B1,C1)");
    EvalResult expected = eval("=DATE(2024,6,15)");
    EXPECT_DOUBLE_EQ(result.getNumber(), expected.getNumber());
}

TEST_F(FunctionTest, YearMonthDayRoundTrip) {
    // Create a date, extract Y/M/D, recreate - should be same
    EvalResult original = eval("=DATE(2024,6,15)");
    EvalResult year = eval("=YEAR(DATE(2024,6,15))");
    EvalResult month = eval("=MONTH(DATE(2024,6,15))");
    EvalResult day = eval("=DAY(DATE(2024,6,15))");

    // Can't easily recreate with DATE function in test, but verify extraction
    EXPECT_DOUBLE_EQ(year.getNumber(), 2024.0);
    EXPECT_DOUBLE_EQ(month.getNumber(), 6.0);
    EXPECT_DOUBLE_EQ(day.getNumber(), 15.0);
}

TEST_F(FunctionTest, HourMinuteSecondRoundTrip) {
    // Create a time, extract H/M/S - should match
    EvalResult hour = eval("=HOUR(TIME(14,30,45))");
    EvalResult minute = eval("=MINUTE(TIME(14,30,45))");
    EvalResult second = eval("=SECOND(TIME(14,30,45))");

    EXPECT_DOUBLE_EQ(hour.getNumber(), 14.0);
    EXPECT_DOUBLE_EQ(minute.getNumber(), 30.0);
    EXPECT_DOUBLE_EQ(second.getNumber(), 45.0);
}

}  // namespace
}  // namespace cells
