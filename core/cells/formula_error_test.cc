#include <cmath>

#include <memory>
#include <string>
#include <unordered_set>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_eval.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// =============================================================================
// Test Fixture
// =============================================================================

class FormulaErrorTest : public ::testing::Test {
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

    // Set a formula at a given column/row position
    Cell* setCellFormula(uint32_t col, uint32_t row, const std::string& formula) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);

        FormulaParser parser(formula);
        auto ast = parser.parse();
        if (ast && !parser.hasErrors()) {
            FormulaResolver resolver(*workbook, *sheet);
            createRequiredEntities(resolver, ast.get());
            resolver.resolve(ast.get());
            sheet->setCellFormula(cell->id, formula, ast.release());
        }

        return cell;
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet = nullptr;
    ID colIds[26];   // A=0, B=1, ..., Z=25
    ID rowIds[100];  // Row 1=0, Row 2=1, ..., Row 100=99
};

// =============================================================================
// 7a: #VALUE! Error from Incompatible Types
// =============================================================================
// #VALUE! occurs when an operation receives the wrong type of argument that
// cannot be coerced to the expected type.

TEST_F(FormulaErrorTest, ValueError_TextInAddition) {
    // Text that can't be converted to number in arithmetic
    setCellValue(0, 0, "hello");  // A1 = "hello"
    EvalResult r = eval("=A1+5");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::VALUE, r.getError());
}

TEST_F(FormulaErrorTest, ValueError_TextInSubtraction) {
    // Text in subtraction operation
    setCellValue(0, 0, "world");  // A1 = "world"
    EvalResult r = eval("=10-A1");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::VALUE, r.getError());
}

TEST_F(FormulaErrorTest, ValueError_TextInMultiplication) {
    // Text in multiplication
    setCellValue(0, 0, "abc");  // A1 = "abc"
    EvalResult r = eval("=A1*3");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::VALUE, r.getError());
}

TEST_F(FormulaErrorTest, ValueError_TextInDivision) {
    // Text in division
    setCellValue(0, 0, "xyz");  // A1 = "xyz"
    EvalResult r = eval("=100/A1");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::VALUE, r.getError());
}

TEST_F(FormulaErrorTest, ValueError_TextInPower) {
    // Text in exponentiation
    setCellValue(0, 0, "text");  // A1 = "text"
    EvalResult r = eval("=2^A1");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::VALUE, r.getError());
}

TEST_F(FormulaErrorTest, ValueError_DirectTextArithmetic) {
    // Direct text literal in arithmetic (that's not a numeric string)
    EvalResult r = eval("=\"abc\"+3");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::VALUE, r.getError());
}

TEST_F(FormulaErrorTest, ValueError_NumericStringConverts) {
    // Numeric strings CAN be converted - this should NOT be an error
    setCellValue(0, 0, "42");  // A1 = "42" (numeric string)
    EvalResult r = eval("=A1+8");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(50.0, r.getNumber());
}

TEST_F(FormulaErrorTest, ValueError_FunctionWrongArgCount) {
    // Function with wrong number of arguments should return VALUE
    // ABS requires exactly 1 argument
    EvalResult r = eval("=ABS(1, 2)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::VALUE, r.getError());
}

TEST_F(FormulaErrorTest, ValueError_TextToBoolean) {
    // Excel's AND/OR skip text values in direct args (same as ranges)
    setCellValue(0, 0, "hello");  // A1 = "hello"
    // AND(text, TRUE) skips text, returns TRUE
    EvalResult r = eval("=AND(A1, TRUE)");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

TEST_F(FormulaErrorTest, ValueError_RangeToScalar) {
    // Range cannot be converted to scalar directly
    setCellValue(0, 0, 1.0);  // A1 = 1
    setCellValue(0, 1, 2.0);  // A2 = 2
    setCellValue(0, 2, 3.0);  // A3 = 3

    // Trying to add a range directly (not via SUM)
    EvalResult r = eval("=A1:A3+5");
    // This should either return VALUE or handle via implicit intersection
    // Depending on implementation, check for appropriate behavior
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::VALUE, r.getError());
}

// =============================================================================
// 7b: #REF! Error from Deleted/Invalid Reference
// =============================================================================
// #REF! occurs when a formula references a cell that doesn't exist or
// has been deleted.

TEST_F(FormulaErrorTest, RefError_CellWithRefError) {
    // Cell containing #REF! propagates the error
    setCellError(0, 0, CellError::REF);  // A1 = #REF!
    EvalResult r = eval("=A1+1");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::REF, r.getError());
}

TEST_F(FormulaErrorTest, RefError_IndexOutOfBounds) {
    // INDEX with row/column out of range
    setCellValue(0, 0, 1.0);  // A1 = 1
    setCellValue(0, 1, 2.0);  // A2 = 2

    // INDEX(A1:A2, 3) - row 3 doesn't exist in a 2-row range
    EvalResult r = eval("=INDEX(A1:A2, 3)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::REF, r.getError());
}

TEST_F(FormulaErrorTest, RefError_IndexColumnOutOfBounds) {
    // INDEX with column out of bounds
    setCellValue(0, 0, 1.0);  // A1 = 1
    setCellValue(1, 0, 2.0);  // B1 = 2

    // INDEX(A1:B1, 1, 5) - column 5 doesn't exist
    EvalResult r = eval("=INDEX(A1:B1, 1, 5)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::REF, r.getError());
}

TEST_F(FormulaErrorTest, RefError_PropagatedFromCell) {
    // #REF! in a referenced cell propagates through formula
    setCellError(0, 0, CellError::REF);  // A1 = #REF!
    setCellValue(1, 0, 10.0);            // B1 = 10

    EvalResult r = eval("=SUM(A1, B1)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::REF, r.getError());
}

TEST_F(FormulaErrorTest, RefError_InMultiplication) {
    // #REF! propagates through multiplication
    setCellError(0, 0, CellError::REF);  // A1 = #REF!
    EvalResult r = eval("=A1*5");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::REF, r.getError());
}

TEST_F(FormulaErrorTest, RefError_CrossSheetInvalidSheet) {
    // Reference to non-existent sheet
    // Note: This test assumes the parser/resolver returns REF for invalid sheet
    EvalResult r = eval("=NonExistentSheet!A1");
    ASSERT_TRUE(r.isError());
    // Could be REF or NAME depending on implementation
    // Most implementations return #REF! for missing sheets
    EXPECT_EQ(CellError::REF, r.getError());
}

// =============================================================================
// 7c: #DIV/0! Error from Division by Zero
// =============================================================================

TEST_F(FormulaErrorTest, DivError_DirectDivisionByZero) {
    // Direct division by zero literal
    EvalResult r = eval("=10/0");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::DIV, r.getError());
}

TEST_F(FormulaErrorTest, DivError_CellContainingZero) {
    // Division by cell containing zero
    setCellValue(0, 0, 0.0);  // A1 = 0
    EvalResult r = eval("=100/A1");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::DIV, r.getError());
}

TEST_F(FormulaErrorTest, DivError_EmptyCellAsZero) {
    // Empty cell treated as zero in division
    // A1 is empty (not created, should be 0)
    EvalResult r = eval("=50/Z99");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::DIV, r.getError());
}

TEST_F(FormulaErrorTest, DivError_MODByZero) {
    // MOD function with zero divisor
    EvalResult r = eval("=MOD(10, 0)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::DIV, r.getError());
}

TEST_F(FormulaErrorTest, DivError_AverageOfEmptyRange) {
    // AVERAGE of empty range (divides by 0)
    EvalResult r = eval("=AVERAGE()");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::DIV, r.getError());
}

TEST_F(FormulaErrorTest, DivError_PropagatedFromCell) {
    // #DIV/0! error propagates from cell
    setCellError(0, 0, CellError::DIV);  // A1 = #DIV/0!
    EvalResult r = eval("=A1+5");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::DIV, r.getError());
}

TEST_F(FormulaErrorTest, DivError_FormulaResult) {
    // Division where divisor formula evaluates to zero
    setCellValue(0, 0, 5.0);   // A1 = 5
    setCellValue(1, 0, -5.0);  // B1 = -5

    EvalResult r = eval("=100/(A1+B1)");  // 100/(5 + -5) = 100/0
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::DIV, r.getError());
}

// =============================================================================
// 7d: #NAME? Error from Unknown Function/Named Range
// =============================================================================

TEST_F(FormulaErrorTest, NameError_UnknownFunction) {
    // Calling a function that doesn't exist
    EvalResult r = eval("=NOTAFUNCTION(1, 2)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NAME, r.getError());
}

TEST_F(FormulaErrorTest, NameError_MisspelledFunction) {
    // Misspelled function name
    EvalResult r = eval("=SUMM(1, 2, 3)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NAME, r.getError());
}

TEST_F(FormulaErrorTest, NameError_UndefinedNamedRange) {
    // Reference to undefined named range
    EvalResult r = eval("=UndefinedRange");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NAME, r.getError());
}

TEST_F(FormulaErrorTest, NameError_CaseInsensitiveFunction) {
    // Functions should be case-insensitive
    EvalResult r1 = eval("=SUM(1, 2)");
    EvalResult r2 = eval("=sum(1, 2)");
    EvalResult r3 = eval("=Sum(1, 2)");

    // All should succeed (not NAME error)
    ASSERT_TRUE(r1.isNumber());
    ASSERT_TRUE(r2.isNumber());
    ASSERT_TRUE(r3.isNumber());
    EXPECT_DOUBLE_EQ(3.0, r1.getNumber());
    EXPECT_DOUBLE_EQ(3.0, r2.getNumber());
    EXPECT_DOUBLE_EQ(3.0, r3.getNumber());
}

TEST_F(FormulaErrorTest, NameError_InFormula) {
    // #NAME? combined with other operations
    EvalResult r = eval("=100 + UnknownName");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NAME, r.getError());
}

// =============================================================================
// 7e: #NUM! Error from Invalid Numeric Operations
// =============================================================================

TEST_F(FormulaErrorTest, NumError_SqrtOfNegative) {
    // Square root of negative number
    EvalResult r = eval("=SQRT(-1)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NUM, r.getError());
}

TEST_F(FormulaErrorTest, NumError_SqrtOfNegativeCell) {
    // SQRT of cell with negative value
    setCellValue(0, 0, -16.0);  // A1 = -16
    EvalResult r = eval("=SQRT(A1)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NUM, r.getError());
}

TEST_F(FormulaErrorTest, NumError_PowerOverflow) {
    // Power resulting in overflow/infinity
    EvalResult r = eval("=POWER(10, 1000)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NUM, r.getError());
}

TEST_F(FormulaErrorTest, NumError_AddOverflow) {
    // Addition overflow to infinity → #NUM!
    setCellValue(0, 0, 1.7e308);  // A1
    setCellValue(0, 1, 1.7e308);  // A2
    EvalResult r = eval("=A1+A2");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NUM, r.getError());
}

TEST_F(FormulaErrorTest, NumError_SubtractOverflow) {
    // Subtraction overflow to -infinity → #NUM!
    setCellValue(0, 0, -1.7e308);  // A1
    setCellValue(0, 1, 1.7e308);   // A2
    EvalResult r = eval("=A1-A2");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NUM, r.getError());
}

TEST_F(FormulaErrorTest, NumError_MultiplyOverflow) {
    // Multiplication overflow to infinity → #NUM!
    setCellValue(0, 0, 1e200);  // A1
    setCellValue(0, 1, 1e200);  // A2
    EvalResult r = eval("=A1*A2");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NUM, r.getError());
}

TEST_F(FormulaErrorTest, NumError_DivideOverflow) {
    // Division overflow (large / tiny) → #NUM!
    setCellValue(0, 0, 1.7e308);  // A1
    setCellValue(0, 1, 0.5);      // A2
    EvalResult r = eval("=A1/A2");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NUM, r.getError());
}

TEST_F(FormulaErrorTest, NumError_NegativeBaseNonIntegerExponent) {
    // Negative base with non-integer exponent (complex result)
    EvalResult r = eval("=POWER(-4, 0.5)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NUM, r.getError());
}

TEST_F(FormulaErrorTest, DivError_ZeroToNegativePower) {
    // 0 raised to negative power → #DIV/0! (Excel behavior)
    EvalResult r = eval("=POWER(0, -1)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::DIV, r.getError());
}

TEST_F(FormulaErrorTest, NumError_ZeroToZeroPower) {
    // 0^0 → #NUM! (Excel behavior, not IEEE754's 1)
    EvalResult r = eval("=POWER(0, 0)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NUM, r.getError());
}

TEST_F(FormulaErrorTest, NumError_ZeroToZeroPowerOperator) {
    // 0^0 via ^ operator → #NUM!
    EvalResult r = eval("=0^0");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NUM, r.getError());
}

TEST_F(FormulaErrorTest, DivError_ZeroToNegativePowerOperator) {
    // 0^(-1) via ^ operator → #DIV/0!
    EvalResult r = eval("=0^-1");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::DIV, r.getError());
}

TEST_F(FormulaErrorTest, NumError_ExtremeExponent) {
    // |exponent| >= 2^53 → #NUM!
    EvalResult r = eval("=POWER(-1, 9.99E+307)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NUM, r.getError());
}

TEST_F(FormulaErrorTest, NumError_ExtremeNegativeExponent) {
    // |exponent| >= 2^53 with negative base → #NUM!
    EvalResult r = eval("=POWER(-1, -9.99E+307)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NUM, r.getError());
}

TEST_F(FormulaErrorTest, ExtremeNegativeExponent_PositiveBaseUnderflows) {
    // 0 < base < 1 with extreme negative exponent → 0 (Excel behavior)
    EvalResult r = eval("=POWER(1E-307, -9.99E+307)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_EQ(0.0, r.getNumber());

    // base > 1 with extreme negative exponent → 0 (underflow)
    EvalResult r2 = eval("=POWER(42.5, -9.99E+307)");
    ASSERT_TRUE(r2.isNumber());
    EXPECT_EQ(0.0, r2.getNumber());
}

TEST_F(FormulaErrorTest, NumError_PropagatedFromCell) {
    // #NUM! propagates from cell
    setCellError(0, 0, CellError::NUM);  // A1 = #NUM!
    EvalResult r = eval("=A1*2");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NUM, r.getError());
}

// =============================================================================
// 7f: #N/A Error from Lookup Failures
// =============================================================================

TEST_F(FormulaErrorTest, NAError_MatchNotFound) {
    // MATCH when value not found (exact match mode)
    setCellValue(0, 0, 1.0);  // A1 = 1
    setCellValue(0, 1, 2.0);  // A2 = 2
    setCellValue(0, 2, 3.0);  // A3 = 3

    // Looking for 5 which doesn't exist
    EvalResult r = eval("=MATCH(5, A1:A3, 0)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NA, r.getError());
}

TEST_F(FormulaErrorTest, NAError_VlookupNotFound) {
    // VLOOKUP when lookup value not found
    setCellValue(0, 0, 1.0);    // A1 = 1
    setCellValue(1, 0, 100.0);  // B1 = 100
    setCellValue(0, 1, 2.0);    // A2 = 2
    setCellValue(1, 1, 200.0);  // B2 = 200
    setCellValue(0, 2, 3.0);    // A3 = 3
    setCellValue(1, 2, 300.0);  // B3 = 300

    // Looking for 99 which doesn't exist (exact match)
    EvalResult r = eval("=VLOOKUP(99, A1:B3, 2, FALSE)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NA, r.getError());
}

TEST_F(FormulaErrorTest, NAError_HlookupNotFound) {
    // HLOOKUP when lookup value not found
    setCellValue(0, 0, 1.0);   // A1 = 1
    setCellValue(1, 0, 2.0);   // B1 = 2
    setCellValue(2, 0, 3.0);   // C1 = 3
    setCellValue(0, 1, 10.0);  // A2 = 10
    setCellValue(1, 1, 20.0);  // B2 = 20
    setCellValue(2, 1, 30.0);  // C2 = 30

    // Looking for 99 which doesn't exist (exact match)
    EvalResult r = eval("=HLOOKUP(99, A1:C2, 2, FALSE)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NA, r.getError());
}

TEST_F(FormulaErrorTest, NAError_ISNADetectsNA) {
    // ISNA() detects #N/A errors
    setCellError(0, 0, CellError::NA);  // A1 = #N/A
    EvalResult r = eval("=ISNA(A1)");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

TEST_F(FormulaErrorTest, NAError_ISNAFalseForOtherErrors) {
    // ISNA() returns FALSE for non-N/A errors
    setCellError(0, 0, CellError::DIV);  // A1 = #DIV/0!
    EvalResult r = eval("=ISNA(A1)");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_FALSE(r.getBoolean());
}

TEST_F(FormulaErrorTest, NAError_PropagatedFromCell) {
    // #N/A propagates from cell
    setCellError(0, 0, CellError::NA);  // A1 = #N/A
    EvalResult r = eval("=A1+10");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NA, r.getError());
}

TEST_F(FormulaErrorTest, NAError_MatchOnEmptyRange) {
    // MATCH on range with no matching values
    setCellValue(0, 0, "apple");   // A1 = "apple"
    setCellValue(0, 1, "banana");  // A2 = "banana"
    setCellValue(0, 2, "cherry");  // A3 = "cherry"

    // Looking for "orange" which doesn't exist
    EvalResult r = eval("=MATCH(\"orange\", A1:A3, 0)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NA, r.getError());
}

// =============================================================================
// 7g: Error Propagation Through Formula Chains
// =============================================================================
// Errors should propagate through formula chains - when one cell has an error,
// formulas depending on it should also evaluate to that error.

TEST_F(FormulaErrorTest, ErrorChain_SingleLevel) {
    // A1 has error, B1 references A1
    setCellError(0, 0, CellError::DIV);  // A1 = #DIV/0!
    EvalResult r = eval("=A1+1");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::DIV, r.getError());
}

TEST_F(FormulaErrorTest, ErrorChain_ThroughFunction) {
    // Error propagates through SUM
    setCellValue(0, 0, 10.0);              // A1 = 10
    setCellError(0, 1, CellError::VALUE);  // A2 = #VALUE!
    setCellValue(0, 2, 30.0);              // A3 = 30

    EvalResult r = eval("=SUM(A1:A3)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::VALUE, r.getError());
}

TEST_F(FormulaErrorTest, ErrorChain_MultipleOperations) {
    // Error propagates through multiple operations
    setCellError(0, 0, CellError::REF);  // A1 = #REF!
    EvalResult r = eval("=(A1+5)*2/3");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::REF, r.getError());
}

TEST_F(FormulaErrorTest, ErrorChain_FirstErrorWins) {
    // When multiple errors exist, the first one encountered wins
    setCellError(0, 0, CellError::REF);    // A1 = #REF!
    setCellError(1, 0, CellError::VALUE);  // B1 = #VALUE!

    EvalResult r = eval("=A1+B1");
    ASSERT_TRUE(r.isError());
    // First operand's error should be returned
    EXPECT_EQ(CellError::REF, r.getError());
}

TEST_F(FormulaErrorTest, ErrorChain_InNestedFunction) {
    // Error in nested function call
    setCellError(0, 0, CellError::NUM);  // A1 = #NUM!
    EvalResult r = eval("=ABS(A1)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NUM, r.getError());
}

TEST_F(FormulaErrorTest, ErrorChain_InConditional) {
    // Error in IF condition
    setCellError(0, 0, CellError::DIV);  // A1 = #DIV/0!
    EvalResult r = eval("=IF(A1>0, \"yes\", \"no\")");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::DIV, r.getError());
}

TEST_F(FormulaErrorTest, ErrorChain_IFERRORCatches) {
    // IFERROR should catch the error and return alternative
    setCellError(0, 0, CellError::DIV);  // A1 = #DIV/0!
    EvalResult r = eval("=IFERROR(A1, \"Error caught\")");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("Error caught", r.getString());
}

TEST_F(FormulaErrorTest, ErrorChain_IFERRORDoesNotCatchValid) {
    // IFERROR should not catch when there's no error
    setCellValue(0, 0, 42.0);  // A1 = 42
    EvalResult r = eval("=IFERROR(A1, \"Error caught\")");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(42.0, r.getNumber());
}

TEST_F(FormulaErrorTest, ErrorChain_ISERRORDetects) {
    // ISERROR should detect errors
    setCellError(0, 0, CellError::VALUE);  // A1 = #VALUE!
    EvalResult r = eval("=ISERROR(A1)");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

TEST_F(FormulaErrorTest, ErrorChain_ISERRORFalseForValid) {
    // ISERROR should return FALSE for valid values
    setCellValue(0, 0, 100.0);  // A1 = 100
    EvalResult r = eval("=ISERROR(A1)");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_FALSE(r.getBoolean());
}

TEST_F(FormulaErrorTest, ErrorChain_InConcatenation) {
    // Error in string concatenation
    setCellError(0, 0, CellError::NAME);  // A1 = #NAME?
    EvalResult r = eval("=\"Value: \"&A1");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NAME, r.getError());
}

TEST_F(FormulaErrorTest, ErrorChain_ThroughComparison) {
    // Error in comparison
    setCellError(0, 0, CellError::NA);  // A1 = #N/A
    EvalResult r = eval("=A1>5");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NA, r.getError());
}

// =============================================================================
// 7h: Formula Recovery After Fixing Source Cell (Auto-Recalculate)
// =============================================================================
// Note: These tests verify the behavior at the evaluation level.
// Full recalculation testing is in formula_recalc_test.cc

TEST_F(FormulaErrorTest, Recovery_ValidAfterFix) {
    // Initially set a valid value, evaluate, then verify
    setCellValue(0, 0, 10.0);  // A1 = 10
    EvalResult r1 = eval("=A1+5");
    ASSERT_TRUE(r1.isNumber());
    EXPECT_DOUBLE_EQ(15.0, r1.getNumber());

    // Now if we change A1 to error then back, new eval should work
    setCellValue(0, 0, 20.0);  // A1 = 20
    EvalResult r2 = eval("=A1+5");
    ASSERT_TRUE(r2.isNumber());
    EXPECT_DOUBLE_EQ(25.0, r2.getNumber());
}

TEST_F(FormulaErrorTest, Recovery_ErrorToValid) {
    // Start with error, then valid value
    setCellError(0, 0, CellError::DIV);  // A1 = #DIV/0!
    EvalResult r1 = eval("=A1*2");
    ASSERT_TRUE(r1.isError());
    EXPECT_EQ(CellError::DIV, r1.getError());

    // Fix the error
    setCellValue(0, 0, 5.0);  // A1 = 5
    EvalResult r2 = eval("=A1*2");
    ASSERT_TRUE(r2.isNumber());
    EXPECT_DOUBLE_EQ(10.0, r2.getNumber());
}

TEST_F(FormulaErrorTest, Recovery_DivisionBecomesValid) {
    // Division error when divisor is zero
    setCellValue(0, 0, 0.0);  // A1 = 0
    EvalResult r1 = eval("=100/A1");
    ASSERT_TRUE(r1.isError());
    EXPECT_EQ(CellError::DIV, r1.getError());

    // Fix divisor
    setCellValue(0, 0, 5.0);  // A1 = 5
    EvalResult r2 = eval("=100/A1");
    ASSERT_TRUE(r2.isNumber());
    EXPECT_DOUBLE_EQ(20.0, r2.getNumber());
}

TEST_F(FormulaErrorTest, Recovery_SqrtBecomesValid) {
    // SQRT of negative becomes valid when value changes
    setCellValue(0, 0, -4.0);  // A1 = -4
    EvalResult r1 = eval("=SQRT(A1)");
    ASSERT_TRUE(r1.isError());
    EXPECT_EQ(CellError::NUM, r1.getError());

    // Fix to positive value
    setCellValue(0, 0, 16.0);  // A1 = 16
    EvalResult r2 = eval("=SQRT(A1)");
    ASSERT_TRUE(r2.isNumber());
    EXPECT_DOUBLE_EQ(4.0, r2.getNumber());
}

TEST_F(FormulaErrorTest, Recovery_TypeCoercionFixed) {
    // Text value that can't be coerced to number
    setCellValue(0, 0, "abc");  // A1 = "abc"
    EvalResult r1 = eval("=A1+10");
    ASSERT_TRUE(r1.isError());
    EXPECT_EQ(CellError::VALUE, r1.getError());

    // Fix with numeric value
    setCellValue(0, 0, 5.0);  // A1 = 5
    EvalResult r2 = eval("=A1+10");
    ASSERT_TRUE(r2.isNumber());
    EXPECT_DOUBLE_EQ(15.0, r2.getNumber());
}

TEST_F(FormulaErrorTest, Recovery_ChainedFormulas) {
    // Chain: B1 = A1 + 5, C1 = B1 * 2
    // Start with error in A1
    setCellError(0, 0, CellError::REF);  // A1 = #REF!

    // B1 formula: =A1+5
    EvalResult r1 = eval("=A1+5");
    ASSERT_TRUE(r1.isError());
    EXPECT_EQ(CellError::REF, r1.getError());

    // Fix A1
    setCellValue(0, 0, 10.0);  // A1 = 10

    // Now the formula should work
    EvalResult r2 = eval("=A1+5");
    ASSERT_TRUE(r2.isNumber());
    EXPECT_DOUBLE_EQ(15.0, r2.getNumber());

    // Chain continues: =((A1+5)*2)
    EvalResult r3 = eval("=(A1+5)*2");
    ASSERT_TRUE(r3.isNumber());
    EXPECT_DOUBLE_EQ(30.0, r3.getNumber());
}

// =============================================================================
// Additional Error Edge Cases
// =============================================================================

TEST_F(FormulaErrorTest, MultipleErrorTypes_MixedInFormula) {
    // Multiple different errors - first one wins
    setCellError(0, 0, CellError::VALUE);  // A1 = #VALUE!
    setCellError(1, 0, CellError::DIV);    // B1 = #DIV/0!
    setCellError(2, 0, CellError::REF);    // C1 = #REF!

    // SUM iterates A1 first, should return VALUE
    EvalResult r = eval("=SUM(A1, B1, C1)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::VALUE, r.getError());
}

TEST_F(FormulaErrorTest, ErrorInRange_FirstCellHasError) {
    // Range where first cell has error
    setCellError(0, 0, CellError::NA);  // A1 = #N/A
    setCellValue(0, 1, 20.0);           // A2 = 20
    setCellValue(0, 2, 30.0);           // A3 = 30

    EvalResult r = eval("=SUM(A1:A3)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NA, r.getError());
}

TEST_F(FormulaErrorTest, ErrorInRange_MiddleCellHasError) {
    // Range where middle cell has error
    setCellValue(0, 0, 10.0);            // A1 = 10
    setCellError(0, 1, CellError::NUM);  // A2 = #NUM!
    setCellValue(0, 2, 30.0);            // A3 = 30

    EvalResult r = eval("=SUM(A1:A3)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NUM, r.getError());
}

TEST_F(FormulaErrorTest, ErrorInRange_LastCellHasError) {
    // Range where last cell has error
    setCellValue(0, 0, 10.0);              // A1 = 10
    setCellValue(0, 1, 20.0);              // A2 = 20
    setCellError(0, 2, CellError::SPILL);  // A3 = #SPILL!

    EvalResult r = eval("=SUM(A1:A3)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::SPILL, r.getError());
}

TEST_F(FormulaErrorTest, ErrorString_Conversion) {
    // Test that error values can be detected and handled
    setCellError(0, 0, CellError::CALC);  // A1 = #CALC!
    EvalResult r = eval("=ISERROR(A1)");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

// =============================================================================
// NA() Function
// =============================================================================

TEST_F(FormulaErrorTest, NA_ReturnsNAError) {
    EvalResult r = eval("=NA()");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NA, r.getError());
}

TEST_F(FormulaErrorTest, NA_WithArgsReturnsError) {
    EvalResult r = eval("=NA(1)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::VALUE, r.getError());
}

TEST_F(FormulaErrorTest, NA_PropagatesInFormula) {
    EvalResult r = eval("=NA()+1");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NA, r.getError());
}

TEST_F(FormulaErrorTest, NA_DetectedByISNA) {
    EvalResult r = eval("=ISNA(NA())");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

TEST_F(FormulaErrorTest, NA_CaughtByIFERROR) {
    EvalResult r = eval("=IFERROR(NA(), 42)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(42.0, r.getNumber());
}

// =============================================================================
// XOR() Function
// =============================================================================

TEST_F(FormulaErrorTest, XOR_SingleTrue) {
    EvalResult r = eval("=XOR(TRUE)");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

TEST_F(FormulaErrorTest, XOR_SingleFalse) {
    EvalResult r = eval("=XOR(FALSE)");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_FALSE(r.getBoolean());
}

TEST_F(FormulaErrorTest, XOR_TwoTrue) {
    EvalResult r = eval("=XOR(TRUE, TRUE)");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_FALSE(r.getBoolean());
}

TEST_F(FormulaErrorTest, XOR_TrueFalse) {
    EvalResult r = eval("=XOR(TRUE, FALSE)");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

TEST_F(FormulaErrorTest, XOR_ThreeTrue) {
    // Odd count of TRUE → TRUE
    EvalResult r = eval("=XOR(TRUE, TRUE, TRUE)");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

TEST_F(FormulaErrorTest, XOR_NumericCoercion) {
    // 1 = TRUE, 0 = FALSE
    EvalResult r = eval("=XOR(1, 0, 1)");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_FALSE(r.getBoolean());  // 2 TRUE values → even → FALSE
}

TEST_F(FormulaErrorTest, XOR_ErrorPropagation) {
    setCellError(0, 0, CellError::NA);  // A1 = #N/A
    EvalResult r = eval("=XOR(TRUE, A1)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NA, r.getError());
}

TEST_F(FormulaErrorTest, XOR_SkipsTextInRange) {
    setCellValue(0, 0, true);     // A1 = TRUE
    setCellValue(0, 1, "hello");  // A2 = "hello"
    setCellValue(0, 2, false);    // A3 = FALSE
    EvalResult r = eval("=XOR(A1:A3)");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());  // 1 TRUE → odd → TRUE
}

TEST_F(FormulaErrorTest, XOR_SkipsEmptyInRange) {
    setCellValue(0, 0, true);  // A1 = TRUE
    // A2 is empty
    setCellValue(0, 2, true);  // A3 = TRUE
    EvalResult r = eval("=XOR(A1:A3)");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_FALSE(r.getBoolean());  // 2 TRUE → even → FALSE
}

TEST_F(FormulaErrorTest, XOR_NoArgs) {
    EvalResult r = eval("=XOR()");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::VALUE, r.getError());
}

TEST_F(FormulaErrorTest, XOR_AllTextNoLogical) {
    setCellValue(0, 0, "hello");  // A1 = "hello"
    setCellValue(0, 1, "world");  // A2 = "world"
    EvalResult r = eval("=XOR(A1:A2)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::VALUE, r.getError());
}

// =============================================================================
// SWITCH Tests
// =============================================================================

TEST_F(FormulaErrorTest, SWITCH_MatchFirst) {
    EvalResult r = eval("=SWITCH(1, 1, \"one\", 2, \"two\", 3, \"three\")");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("one", r.getString());
}

TEST_F(FormulaErrorTest, SWITCH_MatchMiddle) {
    EvalResult r = eval("=SWITCH(2, 1, \"one\", 2, \"two\", 3, \"three\")");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("two", r.getString());
}

TEST_F(FormulaErrorTest, SWITCH_MatchLast) {
    EvalResult r = eval("=SWITCH(3, 1, \"one\", 2, \"two\", 3, \"three\")");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("three", r.getString());
}

TEST_F(FormulaErrorTest, SWITCH_NoMatchWithDefault) {
    EvalResult r = eval("=SWITCH(99, 1, \"one\", 2, \"two\", \"none\")");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("none", r.getString());
}

TEST_F(FormulaErrorTest, SWITCH_NoMatchNoDefault) {
    EvalResult r = eval("=SWITCH(99, 1, \"one\", 2, \"two\")");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NA, r.getError());
}

TEST_F(FormulaErrorTest, SWITCH_StringMatch) {
    EvalResult r = eval("=SWITCH(\"B\", \"a\", 1, \"b\", 2, \"c\", 3)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(2.0, r.getNumber());
}

TEST_F(FormulaErrorTest, SWITCH_BooleanMatch) {
    EvalResult r = eval("=SWITCH(TRUE, FALSE, \"no\", TRUE, \"yes\")");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("yes", r.getString());
}

TEST_F(FormulaErrorTest, SWITCH_ErrorInExpression) {
    EvalResult r = eval("=SWITCH(1/0, 1, \"one\")");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::DIV, r.getError());
}

TEST_F(FormulaErrorTest, SWITCH_ErrorInCaseValue) {
    EvalResult r = eval("=SWITCH(1, 1/0, \"one\", 2, \"two\")");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::DIV, r.getError());
}

TEST_F(FormulaErrorTest, SWITCH_TooFewArgs) {
    EvalResult r = eval("=SWITCH(1, 2)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::VALUE, r.getError());
}

// =============================================================================
// IFS Tests
// =============================================================================

TEST_F(FormulaErrorTest, IFS_FirstTrue) {
    EvalResult r = eval("=IFS(TRUE, \"first\", TRUE, \"second\")");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("first", r.getString());
}

TEST_F(FormulaErrorTest, IFS_SecondTrue) {
    EvalResult r = eval("=IFS(FALSE, \"first\", TRUE, \"second\")");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("second", r.getString());
}

TEST_F(FormulaErrorTest, IFS_NoneTrue) {
    EvalResult r = eval("=IFS(FALSE, \"first\", FALSE, \"second\")");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NA, r.getError());
}

TEST_F(FormulaErrorTest, IFS_NumericCondition) {
    EvalResult r = eval("=IFS(0, \"zero\", 1, \"one\")");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("one", r.getString());
}

TEST_F(FormulaErrorTest, IFS_ErrorInCondition) {
    EvalResult r = eval("=IFS(1/0, \"first\", TRUE, \"second\")");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::DIV, r.getError());
}

TEST_F(FormulaErrorTest, IFS_WithExpressions) {
    setCellValue(0, 0, 85.0);  // A1 = 85
    EvalResult r = eval("=IFS(A1>=90, \"A\", A1>=80, \"B\", A1>=70, \"C\", TRUE, \"F\")");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("B", r.getString());
}

TEST_F(FormulaErrorTest, IFS_OddArgCount) {
    EvalResult r = eval("=IFS(TRUE, \"first\", FALSE)");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::VALUE, r.getError());
}

}  // namespace
}  // namespace cells
