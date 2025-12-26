#include "core/cells/formula_functions.h"

#include <cmath>

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

}  // namespace
}  // namespace cells
