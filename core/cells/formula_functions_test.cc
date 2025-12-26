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

}  // namespace
}  // namespace cells
