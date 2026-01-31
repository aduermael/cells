#include <memory>
#include <string>

#include "core/cells/crdt.h"
#include "core/cells/formula_display.h"
#include "core/cells/formula_eval.h"
#include "core/cells/formula_functions.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// =============================================================================
// Test Fixture for Cell Type Coercion Tests
// =============================================================================
// Tests type coercion behavior for cells with different value types and formats.
// Covers number/string/boolean storage, format effects, and coercion functions.
// =============================================================================

class CellTypeCoercionTest : public ::testing::Test {
protected:
    void SetUp() override {
        workbook = std::make_unique<Workbook>(generate_id(), "Test");
        workbook->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet1"));
        sheet = workbook->getSheetByIndex(0);

        // Create columns A-J (positions 0-9)
        for (uint32_t i = 0; i < 10; i++) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = i;
            col->name = Sheet::positionToColumnName(i);
            colIds[i] = col->id;
            sheet->addColumn(std::move(col));
        }

        // Create rows 1-20 (positions 0-19)
        for (uint32_t i = 0; i < 20; i++) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = i;
            rowIds[i] = row->id;
            sheet->addRow(std::move(row));
        }
    }

    // Set a cell value at a given column/row position (0-indexed)
    Cell* setCellValue(uint32_t col, uint32_t row, double value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(value);
        return cell;
    }

    Cell* setCellString(uint32_t col, uint32_t row, const std::string& value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(value);
        return cell;
    }

    Cell* setCellBoolean(uint32_t col, uint32_t row, bool value) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(value);
        return cell;
    }

    Cell* setCellError(uint32_t col, uint32_t row, CellError error) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
        cell->value = CellValue(error);
        return cell;
    }

    // Set a formula on a cell
    Cell* setCellFormula(uint32_t col, uint32_t row, const std::string& formula) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);

        FormulaParser parser(formula);
        auto ast = parser.parse();
        if (!ast || parser.hasErrors()) {
            return nullptr;
        }

        FormulaResolver resolver(*workbook, *sheet, workbook->getNamedRanges());
        createRequiredEntities(resolver, ast.get());
        ResolveResult resolveResult = resolver.resolve(ast.get());
        if (!resolveResult.success) {
            return nullptr;
        }

        auto result = sheet->setCellFormula(cell->id, formula, ast.release());
        if (!result.success) {
            return nullptr;
        }

        return cell;
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
            const Axis* c = sheet->getColumnByPosition(colPos);
            const Axis* r = sheet->getRowByPosition(rowPos);
            if (c && r) {
                sheet->getOrCreateCellAt(c->id, r->id);
            }
        }
    }

    // Get cell at position
    Cell* getCell(uint32_t col, uint32_t row) { return sheet->getCellAt(colIds[col], rowIds[row]); }

    // Evaluate a formula and return the result
    EvalResult evaluateFormula(Cell* cell) {
        if (!cell || !cell->getFormula() || !cell->getFormula()->ast) {
            return EvalResult::Error(CellError::VALUE);
        }
        EvalContext ctx;
        ctx.sheet = sheet;
        ctx.workbook = workbook.get();
        ctx.namedRanges = workbook->getNamedRanges();
        return evaluate(cell->getFormula()->ast, ctx);
    }

    // Evaluate a formula string directly
    EvalResult evaluateFormulaString(const std::string& formula) {
        FormulaParser parser(formula);
        auto ast = parser.parse();
        if (!ast || parser.hasErrors()) {
            return EvalResult::Error(CellError::VALUE);
        }

        FormulaResolver resolver(*workbook, *sheet, workbook->getNamedRanges());
        createRequiredEntities(resolver, ast.get());
        ResolveResult resolveResult = resolver.resolve(ast.get());
        if (!resolveResult.success) {
            return EvalResult::Error(CellError::VALUE);
        }

        EvalContext ctx;
        ctx.sheet = sheet;
        ctx.workbook = workbook.get();
        ctx.namedRanges = workbook->getNamedRanges();
        return evaluate(ast.get(), ctx);
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet = nullptr;
    ID colIds[10];  // A=0, B=1, ..., J=9
    ID rowIds[20];  // Row 1=0, ..., Row 20=19
};

// =============================================================================
// 11a: Test Number Stored in Text-Formatted Cell
// =============================================================================
// Tests how numeric values behave when stored in text-formatted cells.
// The cell format doesn't affect the internal value type in this engine.

TEST_F(CellTypeCoercionTest, NumberStoredAsNumber_UsedInMath) {
    // Store a number as a number type
    Cell* a1 = setCellValue(0, 0, 42.0);
    EXPECT_EQ(a1->value.type, CellValueType::NUMBER);

    // Reference it in a formula
    Cell* b1 = setCellFormula(1, 0, "=A1*2");
    ASSERT_NE(b1, nullptr);

    EvalResult result = evaluateFormula(b1);
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 84.0);
}

TEST_F(CellTypeCoercionTest, NumericString_CanBeConvertedToNumber) {
    // Store a numeric string
    Cell* a1 = setCellString(0, 0, "123.45");
    EXPECT_EQ(a1->value.type, CellValueType::STRING);

    // Use VALUE() to convert
    EvalResult result = evaluateFormulaString("=VALUE(A1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 123.45);
}

TEST_F(CellTypeCoercionTest, NumericString_ImplicitCoercionInArithmetic) {
    // Store a numeric string
    setCellString(0, 0, "10");

    // Arithmetic operators coerce strings to numbers
    EvalResult result = evaluateFormulaString("=A1+5");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 15.0);
}

TEST_F(CellTypeCoercionTest, NumericString_ImplicitCoercionMultiplication) {
    // Store two numeric strings
    setCellString(0, 0, "7");
    setCellString(0, 1, "6");

    EvalResult result = evaluateFormulaString("=A1*A2");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 42.0);
}

TEST_F(CellTypeCoercionTest, NumericString_WithLeadingSpaces) {
    setCellString(0, 0, "  42  ");

    EvalResult result = evaluateFormulaString("=VALUE(A1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 42.0);
}

TEST_F(CellTypeCoercionTest, NumericString_NegativeNumber) {
    setCellString(0, 0, "-99.5");

    EvalResult result = evaluateFormulaString("=VALUE(A1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -99.5);
}

TEST_F(CellTypeCoercionTest, NumericString_ScientificNotation) {
    setCellString(0, 0, "1.5e3");

    EvalResult result = evaluateFormulaString("=VALUE(A1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1500.0);
}

// =============================================================================
// 11b: Test Text Stored in Number-Formatted Cell
// =============================================================================
// Tests how text values behave when stored regardless of cell format.

TEST_F(CellTypeCoercionTest, TextInNumberContext_ReturnsValueError) {
    // Store pure text (non-numeric)
    setCellString(0, 0, "hello");

    // Arithmetic with text that can't be parsed should error
    EvalResult result = evaluateFormulaString("=A1+1");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(CellTypeCoercionTest, TextValue_CannotBeCoercedToNumber) {
    setCellString(0, 0, "abc");

    EvalResult result = evaluateFormulaString("=VALUE(A1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(CellTypeCoercionTest, MixedTextNumber_CannotCoerce) {
    setCellString(0, 0, "12abc");

    EvalResult result = evaluateFormulaString("=VALUE(A1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(CellTypeCoercionTest, TextConcat_WorksNormally) {
    setCellString(0, 0, "Hello");
    setCellString(0, 1, " World");

    EvalResult result = evaluateFormulaString("=CONCAT(A1,A2)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "Hello World");
}

TEST_F(CellTypeCoercionTest, NumberToString_InConcat) {
    setCellValue(0, 0, 42.0);

    EvalResult result = evaluateFormulaString("=CONCAT(\"Value: \",A1)");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "Value: 42");
}

TEST_F(CellTypeCoercionTest, EmptyString_InArithmetic) {
    // Empty string should coerce to 0 in numeric context
    setCellString(0, 0, "");

    EvalResult result = evaluateFormulaString("=A1+10");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 10.0);
}

// =============================================================================
// 11c: Test Boolean Stored in Various Format Types
// =============================================================================
// Tests boolean value coercion to numbers and strings.

TEST_F(CellTypeCoercionTest, BooleanTrue_CoercesToOne) {
    setCellBoolean(0, 0, true);

    EvalResult result = evaluateFormulaString("=A1+0");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(CellTypeCoercionTest, BooleanFalse_CoercesToZero) {
    setCellBoolean(0, 0, false);

    EvalResult result = evaluateFormulaString("=A1+0");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(CellTypeCoercionTest, BooleanTrue_InMultiplication) {
    setCellBoolean(0, 0, true);

    EvalResult result = evaluateFormulaString("=A1*5");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

TEST_F(CellTypeCoercionTest, BooleanFalse_InMultiplication) {
    setCellBoolean(0, 0, false);

    EvalResult result = evaluateFormulaString("=A1*5");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(CellTypeCoercionTest, BooleanTrue_ToString) {
    setCellBoolean(0, 0, true);

    EvalResult result = evaluateFormulaString("=CONCAT(A1,\"\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "TRUE");
}

TEST_F(CellTypeCoercionTest, BooleanFalse_ToString) {
    setCellBoolean(0, 0, false);

    EvalResult result = evaluateFormulaString("=CONCAT(A1,\"\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "FALSE");
}

TEST_F(CellTypeCoercionTest, BooleanInSum_CountsAsNumber) {
    setCellBoolean(0, 0, true);
    setCellBoolean(0, 1, true);
    setCellBoolean(0, 2, false);

    EvalResult result = evaluateFormulaString("=SUM(A1:A3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);  // true+true+false = 1+1+0
}

TEST_F(CellTypeCoercionTest, BooleanInAverage_CountsAsNumber) {
    setCellBoolean(0, 0, true);
    setCellBoolean(0, 1, false);

    EvalResult result = evaluateFormulaString("=AVERAGE(A1:A2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.5);  // (1+0)/2
}

// =============================================================================
// 11d: Test Date/Time Value Coercion
// =============================================================================
// Tests date serial number handling and coercion.

TEST_F(CellTypeCoercionTest, DateSerial_InArithmetic) {
    // Excel date serial for Jan 1, 2024 (approx 45292)
    setCellValue(0, 0, 45292.0);

    // Adding days to a date
    EvalResult result = evaluateFormulaString("=A1+7");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 45299.0);  // Jan 8, 2024
}

TEST_F(CellTypeCoercionTest, DateSerial_Subtraction) {
    // Two dates
    setCellValue(0, 0, 45299.0);  // Jan 8, 2024
    setCellValue(0, 1, 45292.0);  // Jan 1, 2024

    EvalResult result = evaluateFormulaString("=A1-A2");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 7.0);  // 7 days apart
}

TEST_F(CellTypeCoercionTest, TimeSerial_InArithmetic) {
    // 0.5 = noon (12:00 PM)
    setCellValue(0, 0, 0.5);

    // Add 6 hours (0.25 of a day)
    EvalResult result = evaluateFormulaString("=A1+0.25");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.75);  // 6:00 PM
}

TEST_F(CellTypeCoercionTest, DateTimeSerial_Combined) {
    // Full date+time: Jan 1, 2024 at noon
    setCellValue(0, 0, 45292.5);

    EvalResult result = evaluateFormulaString("=DAY(A1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);
}

TEST_F(CellTypeCoercionTest, DateSerial_HourExtraction) {
    // Jan 1, 2024 at 6:00 PM (0.75 of day)
    setCellValue(0, 0, 45292.75);

    EvalResult result = evaluateFormulaString("=HOUR(A1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 18.0);  // 6 PM = 18:00
}

// =============================================================================
// 11e: Test Formula Referencing Mixed Types
// =============================================================================
// Tests formulas that reference cells with different value types.

TEST_F(CellTypeCoercionTest, MixedTypes_SumIgnoresText) {
    setCellValue(0, 0, 10.0);
    setCellString(0, 1, "hello");
    setCellValue(0, 2, 20.0);

    // SUM ignores non-numeric values in ranges
    EvalResult result = evaluateFormulaString("=SUM(A1:A3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 30.0);
}

TEST_F(CellTypeCoercionTest, MixedTypes_CountOnlyNumbers) {
    setCellValue(0, 0, 10.0);
    setCellString(0, 1, "hello");
    setCellValue(0, 2, 20.0);
    setCellBoolean(0, 3, true);

    // COUNT only counts numbers
    EvalResult result = evaluateFormulaString("=COUNT(A1:A4)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 2.0);  // Only 10 and 20
}

TEST_F(CellTypeCoercionTest, MixedTypes_CountAIncludesAll) {
    setCellValue(0, 0, 10.0);
    setCellString(0, 1, "hello");
    setCellValue(0, 2, 20.0);
    setCellBoolean(0, 3, true);

    // COUNTA counts all non-empty cells
    EvalResult result = evaluateFormulaString("=COUNTA(A1:A4)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 4.0);
}

TEST_F(CellTypeCoercionTest, MixedTypes_AverageIgnoresText) {
    setCellValue(0, 0, 10.0);
    setCellString(0, 1, "hello");
    setCellValue(0, 2, 20.0);

    EvalResult result = evaluateFormulaString("=AVERAGE(A1:A3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 15.0);  // (10+20)/2
}

TEST_F(CellTypeCoercionTest, MixedTypes_MinIgnoresText) {
    setCellValue(0, 0, 10.0);
    setCellString(0, 1, "hello");
    setCellValue(0, 2, 5.0);

    EvalResult result = evaluateFormulaString("=MIN(A1:A3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 5.0);
}

TEST_F(CellTypeCoercionTest, MixedTypes_MaxIgnoresText) {
    setCellValue(0, 0, 10.0);
    setCellString(0, 1, "hello");
    setCellValue(0, 2, 5.0);

    EvalResult result = evaluateFormulaString("=MAX(A1:A3)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 10.0);
}

TEST_F(CellTypeCoercionTest, MixedTypes_ErrorPropagates) {
    setCellValue(0, 0, 10.0);
    setCellError(0, 1, CellError::DIV);
    setCellValue(0, 2, 20.0);

    EvalResult result = evaluateFormulaString("=SUM(A1:A3)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);
}

// =============================================================================
// 11f: Test VALUE(), TEXT(), NUMBERVALUE() Coercion Functions
// =============================================================================
// Tests explicit type conversion functions.

TEST_F(CellTypeCoercionTest, ValueFunction_BasicConversion) {
    setCellString(0, 0, "42");

    EvalResult result = evaluateFormulaString("=VALUE(A1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 42.0);
}

TEST_F(CellTypeCoercionTest, ValueFunction_CurrencyString) {
    setCellString(0, 0, "$1,234.56");

    EvalResult result = evaluateFormulaString("=VALUE(A1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1234.56);
}

TEST_F(CellTypeCoercionTest, ValueFunction_PercentageString) {
    setCellString(0, 0, "50%");

    EvalResult result = evaluateFormulaString("=VALUE(A1)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.5);
}

TEST_F(CellTypeCoercionTest, ValueFunction_NonNumeric_Error) {
    setCellString(0, 0, "not a number");

    EvalResult result = evaluateFormulaString("=VALUE(A1)");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(CellTypeCoercionTest, TextFunction_NumberToText) {
    setCellValue(0, 0, 1234.5);

    EvalResult result = evaluateFormulaString("=TEXT(A1,\"0.00\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "1234.50");
}

TEST_F(CellTypeCoercionTest, TextFunction_Percentage) {
    setCellValue(0, 0, 0.25);

    EvalResult result = evaluateFormulaString("=TEXT(A1,\"0%\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "25%");
}

TEST_F(CellTypeCoercionTest, TextFunction_Currency) {
    setCellValue(0, 0, 1234.5);

    EvalResult result = evaluateFormulaString("=TEXT(A1,\"$#,##0.00\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "$1,234.50");
}

TEST_F(CellTypeCoercionTest, TextFunction_NoDecimalPlaces) {
    setCellValue(0, 0, 1234.567);

    EvalResult result = evaluateFormulaString("=TEXT(A1,\"0\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "1235");  // Rounded
}

TEST_F(CellTypeCoercionTest, TextFunction_NegativeNumber) {
    setCellValue(0, 0, -42.5);

    EvalResult result = evaluateFormulaString("=TEXT(A1,\"0.0\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "-42.5");
}

// =============================================================================
// 11g: Test Implicit Coercion in Arithmetic Operations
// =============================================================================
// Tests how different types are coerced in arithmetic operations.

TEST_F(CellTypeCoercionTest, ImplicitCoercion_AddNumberToNumber) {
    setCellValue(0, 0, 10.0);
    setCellValue(0, 1, 5.0);

    EvalResult result = evaluateFormulaString("=A1+A2");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 15.0);
}

TEST_F(CellTypeCoercionTest, ImplicitCoercion_AddNumberToBoolean) {
    setCellValue(0, 0, 10.0);
    setCellBoolean(0, 1, true);

    EvalResult result = evaluateFormulaString("=A1+A2");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 11.0);
}

TEST_F(CellTypeCoercionTest, ImplicitCoercion_AddNumberToEmpty) {
    setCellValue(0, 0, 10.0);
    // A2 is empty

    EvalResult result = evaluateFormulaString("=A1+A2");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 10.0);  // empty coerces to 0
}

TEST_F(CellTypeCoercionTest, ImplicitCoercion_AddNumberToNumericString) {
    setCellValue(0, 0, 10.0);
    setCellString(0, 1, "5");

    EvalResult result = evaluateFormulaString("=A1+A2");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 15.0);
}

TEST_F(CellTypeCoercionTest, ImplicitCoercion_MultiplyBooleans) {
    setCellBoolean(0, 0, true);
    setCellBoolean(0, 1, true);

    EvalResult result = evaluateFormulaString("=A1*A2");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 1.0);  // 1*1
}

TEST_F(CellTypeCoercionTest, ImplicitCoercion_DivideByBoolean) {
    setCellValue(0, 0, 10.0);
    setCellBoolean(0, 1, true);

    EvalResult result = evaluateFormulaString("=A1/A2");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 10.0);  // 10/1
}

TEST_F(CellTypeCoercionTest, ImplicitCoercion_DivideByFalse_Error) {
    setCellValue(0, 0, 10.0);
    setCellBoolean(0, 1, false);

    EvalResult result = evaluateFormulaString("=A1/A2");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::DIV);  // 10/0 = #DIV/0!
}

TEST_F(CellTypeCoercionTest, ImplicitCoercion_SubtractNumberFromString) {
    setCellString(0, 0, "100");
    setCellValue(0, 1, 30.0);

    EvalResult result = evaluateFormulaString("=A1-A2");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 70.0);
}

TEST_F(CellTypeCoercionTest, ImplicitCoercion_UnaryMinus) {
    setCellString(0, 0, "42");

    EvalResult result = evaluateFormulaString("=-A1");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), -42.0);
}

TEST_F(CellTypeCoercionTest, ImplicitCoercion_Percentage) {
    // Percentage literals are parsed directly (50% = 0.5)
    setCellValue(0, 0, 50.0);

    // Use explicit division for percentage conversion
    EvalResult result = evaluateFormulaString("=A1/100");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.5);  // 50/100
}

TEST_F(CellTypeCoercionTest, ImplicitCoercion_Power) {
    setCellString(0, 0, "2");
    setCellString(0, 1, "3");

    EvalResult result = evaluateFormulaString("=POWER(A1,A2)");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 8.0);  // 2^3
}

TEST_F(CellTypeCoercionTest, ImplicitCoercion_ComparisonNumericString) {
    setCellString(0, 0, "10");
    setCellValue(0, 1, 5.0);

    EvalResult result = evaluateFormulaString("=A1>A2");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

TEST_F(CellTypeCoercionTest, ImplicitCoercion_ComparisonBooleanNumber) {
    setCellBoolean(0, 0, true);
    setCellValue(0, 1, 0.5);

    // true (1) > 0.5
    EvalResult result = evaluateFormulaString("=A1>A2");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());
}

// =============================================================================
// Additional Edge Cases
// =============================================================================

TEST_F(CellTypeCoercionTest, EmptyCell_InNumericContext) {
    // A1 is empty

    EvalResult result = evaluateFormulaString("=A1*2");
    EXPECT_TRUE(result.isNumber());
    EXPECT_DOUBLE_EQ(result.getNumber(), 0.0);
}

TEST_F(CellTypeCoercionTest, EmptyCell_InStringContext) {
    // A1 is empty

    EvalResult result = evaluateFormulaString("=CONCAT(A1,\"test\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "test");  // Empty contributes nothing
}

TEST_F(CellTypeCoercionTest, ErrorCell_PropagatesThrough) {
    setCellError(0, 0, CellError::VALUE);

    EvalResult result = evaluateFormulaString("=A1+1");
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.getError(), CellError::VALUE);
}

TEST_F(CellTypeCoercionTest, ChainedCoercion_StringToNumberToBoolean) {
    // Use a numeric cell instead - strings don't implicitly convert to boolean
    setCellValue(0, 0, 0.0);

    // 0 -> false -> NOT(false) = true
    EvalResult result = evaluateFormulaString("=NOT(A1)");
    EXPECT_TRUE(result.isBoolean());
    EXPECT_TRUE(result.getBoolean());  // NOT(0) = true
}

TEST_F(CellTypeCoercionTest, ChainedCoercion_BooleanToNumberToString) {
    setCellBoolean(0, 0, true);

    // true -> 1 -> "1" (in TEXT function)
    EvalResult result = evaluateFormulaString("=TEXT(A1,\"0\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "1");
}

TEST_F(CellTypeCoercionTest, LargeNumber_InStringCoercion) {
    setCellValue(0, 0, 1e15);

    EvalResult result = evaluateFormulaString("=CONCAT(A1,\"\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "1000000000000000");
}

TEST_F(CellTypeCoercionTest, SmallDecimal_InStringCoercion) {
    setCellValue(0, 0, 0.001);

    EvalResult result = evaluateFormulaString("=CONCAT(A1,\"\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "0.001");
}

TEST_F(CellTypeCoercionTest, ZeroNumber_InBooleanContext) {
    setCellValue(0, 0, 0.0);

    EvalResult result = evaluateFormulaString("=IF(A1,\"yes\",\"no\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "no");  // 0 is falsy
}

TEST_F(CellTypeCoercionTest, NonZeroNumber_InBooleanContext) {
    setCellValue(0, 0, 0.001);

    EvalResult result = evaluateFormulaString("=IF(A1,\"yes\",\"no\")");
    EXPECT_TRUE(result.isString());
    EXPECT_EQ(result.getString(), "yes");  // Non-zero is truthy
}

}  // namespace
}  // namespace cells
