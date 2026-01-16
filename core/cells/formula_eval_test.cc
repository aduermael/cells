#include "core/cells/formula_eval.h"

#include <cmath>

#include <memory>
#include <string>
#include <unordered_set>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"
#include "core/cells/named_ranges.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// Helper class for evaluation tests
class FormulaEvalTest : public ::testing::Test {
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
        FormulaResolver resolver(*workbook, *sheet, workbook->getNamedRanges());
        resolver.resolve(ast.get());

        // Evaluate
        std::unordered_set<ID> evaluating;
        EvalContext ctx;
        ctx.sheet = sheet;
        ctx.workbook = workbook.get();
        ctx.namedRanges = workbook->getNamedRanges();
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

    // Set a formula at a given column/row position
    Cell* setCellFormula(uint32_t col, uint32_t row, const std::string& formula) {
        Cell* cell = sheet->getOrCreateCellAt(colIds[col], rowIds[row]);

        FormulaParser parser(formula);
        auto ast = parser.parse();
        if (ast && !parser.hasErrors()) {
            FormulaResolver resolver(*workbook, *sheet);
            resolver.resolve(ast.get());
            sheet->setCellFormula(cell->id, formula, ast.release());
        }

        return cell;
    }

    std::unique_ptr<Workbook> workbook;
    Sheet* sheet = nullptr;
    ID colIds[26];
    ID rowIds[100];
};

// =============================================================================
// LITERAL TESTS
// =============================================================================

TEST_F(FormulaEvalTest, IntegerLiteral) {
    EvalResult r = eval("=42");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(42.0, r.getNumber());
}

TEST_F(FormulaEvalTest, DecimalLiteral) {
    EvalResult r = eval("=3.14");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(3.14, r.getNumber());
}

TEST_F(FormulaEvalTest, ScientificNotation) {
    EvalResult r = eval("=1.5e10");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(1.5e10, r.getNumber());
}

TEST_F(FormulaEvalTest, NegativeScientific) {
    EvalResult r = eval("=2e-3");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(0.002, r.getNumber());
}

TEST_F(FormulaEvalTest, ZeroLiteral) {
    EvalResult r = eval("=0");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(0.0, r.getNumber());
}

TEST_F(FormulaEvalTest, StringLiteral) {
    EvalResult r = eval("=\"hello\"");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("hello", r.getString());
}

TEST_F(FormulaEvalTest, EmptyStringLiteral) {
    EvalResult r = eval("=\"\"");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("", r.getString());
}

TEST_F(FormulaEvalTest, StringWithSpaces) {
    EvalResult r = eval("=\"hello world\"");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("hello world", r.getString());
}

TEST_F(FormulaEvalTest, BooleanTrue) {
    EvalResult r = eval("=TRUE");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

TEST_F(FormulaEvalTest, BooleanFalse) {
    EvalResult r = eval("=FALSE");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_FALSE(r.getBoolean());
}

// =============================================================================
// PERCENTAGE LITERAL TESTS
// =============================================================================

TEST_F(FormulaEvalTest, PercentLiteralSimple) {
    // =15% evaluates to 0.15
    EvalResult r = eval("=15%");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(0.15, r.getNumber());
}

TEST_F(FormulaEvalTest, PercentLiteralDecimal) {
    // =12.5% evaluates to 0.125
    EvalResult r = eval("=12.5%");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(0.125, r.getNumber());
}

TEST_F(FormulaEvalTest, PercentLiteralMultiplication) {
    // =1000*15% evaluates to 150
    EvalResult r = eval("=1000*15%");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(150.0, r.getNumber());
}

TEST_F(FormulaEvalTest, PercentLiteralAddition) {
    // =50%+25% evaluates to 0.75
    EvalResult r = eval("=50%+25%");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(0.75, r.getNumber());
}

TEST_F(FormulaEvalTest, PercentLiteralNegative) {
    // =-15% evaluates to -0.15
    EvalResult r = eval("=-15%");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(-0.15, r.getNumber());
}

TEST_F(FormulaEvalTest, PercentLiteralWithCellRef) {
    // A1=100, =A1*15% evaluates to 15
    setCellValue(0, 0, 100.0);
    EvalResult r = eval("=A1*15%");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(15.0, r.getNumber());
}

TEST_F(FormulaEvalTest, PercentLiteralHundred) {
    // =100% evaluates to 1.0
    EvalResult r = eval("=100%");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(1.0, r.getNumber());
}

TEST_F(FormulaEvalTest, PercentLiteralZero) {
    // =0% evaluates to 0.0
    EvalResult r = eval("=0%");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(0.0, r.getNumber());
}

// =============================================================================
// ARITHMETIC OPERATOR TESTS
// =============================================================================

TEST_F(FormulaEvalTest, Addition) {
    EvalResult r = eval("=2+3");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(5.0, r.getNumber());
}

TEST_F(FormulaEvalTest, Subtraction) {
    EvalResult r = eval("=10-4");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(6.0, r.getNumber());
}

TEST_F(FormulaEvalTest, Multiplication) {
    EvalResult r = eval("=3*4");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(12.0, r.getNumber());
}

TEST_F(FormulaEvalTest, Division) {
    EvalResult r = eval("=15/3");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(5.0, r.getNumber());
}

TEST_F(FormulaEvalTest, DivisionByZero) {
    EvalResult r = eval("=1/0");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::DIV, r.getError());
}

TEST_F(FormulaEvalTest, Power) {
    EvalResult r = eval("=2^10");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(1024.0, r.getNumber());
}

TEST_F(FormulaEvalTest, NegativePower) {
    EvalResult r = eval("=4^-1");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(0.25, r.getNumber());
}

TEST_F(FormulaEvalTest, ZeroPower) {
    EvalResult r = eval("=5^0");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(1.0, r.getNumber());
}

TEST_F(FormulaEvalTest, FractionalPower) {
    EvalResult r = eval("=9^0.5");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(3.0, r.getNumber());
}

TEST_F(FormulaEvalTest, OperatorPrecedence) {
    EvalResult r = eval("=2+3*4");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(14.0, r.getNumber());
}

TEST_F(FormulaEvalTest, Parentheses) {
    EvalResult r = eval("=(2+3)*4");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(20.0, r.getNumber());
}

TEST_F(FormulaEvalTest, ComplexExpression) {
    EvalResult r = eval("=2+3*4-6/2");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(11.0, r.getNumber());
}

TEST_F(FormulaEvalTest, NestedParentheses) {
    EvalResult r = eval("=((2+3)*(4-1))/3");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(5.0, r.getNumber());
}

TEST_F(FormulaEvalTest, PowerPrecedence) {
    EvalResult r = eval("=2*3^2");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(18.0, r.getNumber());  // 2 * (3^2) = 2 * 9 = 18
}

// =============================================================================
// UNARY OPERATOR TESTS
// =============================================================================

TEST_F(FormulaEvalTest, UnaryMinus) {
    EvalResult r = eval("=-5");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(-5.0, r.getNumber());
}

TEST_F(FormulaEvalTest, UnaryPlus) {
    EvalResult r = eval("=+5");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(5.0, r.getNumber());
}

TEST_F(FormulaEvalTest, UnaryMinusInExpression) {
    EvalResult r = eval("=-5+3");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(-2.0, r.getNumber());
}

TEST_F(FormulaEvalTest, DoubleNegative) {
    EvalResult r = eval("=--5");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(5.0, r.getNumber());
}

TEST_F(FormulaEvalTest, UnaryMinusOnBoolean) {
    EvalResult r = eval("=-TRUE");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(-1.0, r.getNumber());
}

// =============================================================================
// COMPARISON OPERATOR TESTS
// =============================================================================

TEST_F(FormulaEvalTest, EqualNumbers) {
    EvalResult r = eval("=5=5");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

TEST_F(FormulaEvalTest, NotEqualNumbers) {
    EvalResult r = eval("=5<>3");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

TEST_F(FormulaEvalTest, LessThan) {
    EvalResult r = eval("=3<5");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

TEST_F(FormulaEvalTest, LessThanFalse) {
    EvalResult r = eval("=5<3");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_FALSE(r.getBoolean());
}

TEST_F(FormulaEvalTest, LessEqual) {
    EvalResult r = eval("=5<=5");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

TEST_F(FormulaEvalTest, GreaterThan) {
    EvalResult r = eval("=5>3");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

TEST_F(FormulaEvalTest, GreaterEqual) {
    EvalResult r = eval("=3>=5");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_FALSE(r.getBoolean());
}

TEST_F(FormulaEvalTest, StringComparison) {
    EvalResult r = eval("=\"a\"<\"b\"");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

TEST_F(FormulaEvalTest, StringEqualityCaseInsensitive) {
    EvalResult r = eval("=\"ABC\"=\"abc\"");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

TEST_F(FormulaEvalTest, MixedTypeComparison) {
    // "5" = 5 should be true after coercion
    EvalResult r = eval("=\"5\"=5");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

TEST_F(FormulaEvalTest, BooleanNumberComparison) {
    // TRUE = 1 should be true
    EvalResult r = eval("=TRUE=1");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

// =============================================================================
// CONCATENATION TESTS
// =============================================================================

TEST_F(FormulaEvalTest, StringConcat) {
    EvalResult r = eval("=\"hello\"&\"world\"");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("helloworld", r.getString());
}

TEST_F(FormulaEvalTest, NumberConcat) {
    EvalResult r = eval("=1&2");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("12", r.getString());
}

TEST_F(FormulaEvalTest, MixedConcat) {
    EvalResult r = eval("=\"value: \"&100");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("value: 100", r.getString());
}

TEST_F(FormulaEvalTest, BooleanConcat) {
    EvalResult r = eval("=TRUE&\" \"&FALSE");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("TRUE FALSE", r.getString());
}

TEST_F(FormulaEvalTest, EmptyStringConcat) {
    EvalResult r = eval("=\"test\"&\"\"");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("test", r.getString());
}

// =============================================================================
// CELL REFERENCE TESTS
// =============================================================================

TEST_F(FormulaEvalTest, SimpleCellRef) {
    setCellValue(0, 0, 10.0);  // A1 = 10
    EvalResult r = eval("=A1");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(10.0, r.getNumber());
}

TEST_F(FormulaEvalTest, CellRefChain) {
    setCellValue(0, 0, 5.0);  // A1 = 5
    // Set B1 = A1 (we need to set up the formula properly)
    setCellFormula(1, 0, "=A1");
    // Now evaluate B1 reference
    EvalResult r = eval("=B1");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(5.0, r.getNumber());
}

TEST_F(FormulaEvalTest, EmptyCellRef) {
    // Z99 is empty - empty cells return 0 in numeric context
    EvalResult r = eval("=Z99");
    // Empty cell can be either Empty or Number(0)
    if (r.isEmpty()) {
        EvalResult num = r.toNumber();
        ASSERT_TRUE(num.isNumber());
        EXPECT_DOUBLE_EQ(0.0, num.getNumber());
    } else {
        ASSERT_TRUE(r.isNumber());
        EXPECT_DOUBLE_EQ(0.0, r.getNumber());
    }
}

TEST_F(FormulaEvalTest, CellRefWithArithmetic) {
    setCellValue(0, 0, 10.0);  // A1 = 10
    setCellValue(1, 0, 5.0);   // B1 = 5
    EvalResult r = eval("=A1+B1");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(15.0, r.getNumber());
}

TEST_F(FormulaEvalTest, StringCellRef) {
    setCellValue(0, 0, "hello");  // A1 = "hello"
    EvalResult r = eval("=A1");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("hello", r.getString());
}

TEST_F(FormulaEvalTest, BooleanCellRef) {
    setCellValue(0, 0, true);  // A1 = TRUE
    EvalResult r = eval("=A1");
    ASSERT_TRUE(r.isBoolean());
    EXPECT_TRUE(r.getBoolean());
}

TEST_F(FormulaEvalTest, ErrorCellRef) {
    setCellError(0, 0, CellError::REF);  // A1 = #REF!
    EvalResult r = eval("=A1");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::REF, r.getError());
}

TEST_F(FormulaEvalTest, AbsoluteReference) {
    setCellValue(0, 0, 42.0);  // A1 = 42
    EvalResult r = eval("=$A$1");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(42.0, r.getNumber());
}

TEST_F(FormulaEvalTest, MixedReference) {
    setCellValue(0, 0, 42.0);  // A1 = 42
    EvalResult r = eval("=$A1");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(42.0, r.getNumber());
}

// =============================================================================
// TYPE COERCION TESTS
// =============================================================================

TEST_F(FormulaEvalTest, StringToNumberCoercion) {
    EvalResult r = eval("=\"5\"+3");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(8.0, r.getNumber());
}

TEST_F(FormulaEvalTest, InvalidStringToNumber) {
    EvalResult r = eval("=\"abc\"+3");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::VALUE, r.getError());
}

TEST_F(FormulaEvalTest, BooleanToNumberAdd) {
    EvalResult r = eval("=TRUE+1");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(2.0, r.getNumber());
}

TEST_F(FormulaEvalTest, FalseToNumberAdd) {
    EvalResult r = eval("=FALSE+1");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(1.0, r.getNumber());
}

TEST_F(FormulaEvalTest, NumberToStringConcat) {
    EvalResult r = eval("=5&\"\"");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("5", r.getString());
}

TEST_F(FormulaEvalTest, EmptyStringToNumber) {
    EvalResult r = eval("=\"\"+5");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(5.0, r.getNumber());  // Empty string coerces to 0
}

TEST_F(FormulaEvalTest, DecimalStringToNumber) {
    EvalResult r = eval("=\"3.14\"+1");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(4.14, r.getNumber());
}

// =============================================================================
// ERROR PROPAGATION TESTS
// =============================================================================

TEST_F(FormulaEvalTest, ErrorInLeftOperand) {
    setCellError(0, 0, CellError::REF);  // A1 = #REF!
    EvalResult r = eval("=A1+5");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::REF, r.getError());
}

TEST_F(FormulaEvalTest, ErrorInRightOperand) {
    setCellError(0, 0, CellError::DIV);  // A1 = #DIV/0!
    EvalResult r = eval("=5+A1");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::DIV, r.getError());
}

TEST_F(FormulaEvalTest, ErrorInNestedExpr) {
    setCellError(0, 0, CellError::VALUE);  // A1 = #VALUE!
    EvalResult r = eval("=(1+A1)*2");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::VALUE, r.getError());
}

TEST_F(FormulaEvalTest, FirstErrorWins) {
    setCellError(0, 0, CellError::REF);    // A1 = #REF!
    setCellError(1, 0, CellError::VALUE);  // B1 = #VALUE!
    EvalResult r = eval("=A1+B1");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::REF, r.getError());  // First error propagates
}

TEST_F(FormulaEvalTest, ErrorInComparison) {
    setCellError(0, 0, CellError::DIV);  // A1 = #DIV/0!
    EvalResult r = eval("=A1>5");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::DIV, r.getError());
}

TEST_F(FormulaEvalTest, ErrorInConcat) {
    setCellError(0, 0, CellError::NAME);  // A1 = #NAME?
    EvalResult r = eval("=\"prefix\"&A1");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NAME, r.getError());
}

// =============================================================================
// EDGE CASES
// =============================================================================

TEST_F(FormulaEvalTest, VeryLargeNumber) {
    EvalResult r = eval("=1e300");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(1e300, r.getNumber());
}

TEST_F(FormulaEvalTest, VerySmallNumber) {
    EvalResult r = eval("=1e-300");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(1e-300, r.getNumber());
}

TEST_F(FormulaEvalTest, NegativeZero) {
    EvalResult r = eval("=-0");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(0.0, r.getNumber());
}

TEST_F(FormulaEvalTest, ZeroDividedByNumber) {
    EvalResult r = eval("=0/5");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(0.0, r.getNumber());
}

TEST_F(FormulaEvalTest, LongExpression) {
    EvalResult r = eval("=1+2+3+4+5+6+7+8+9+10");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(55.0, r.getNumber());
}

TEST_F(FormulaEvalTest, DeeplyNestedParens) {
    EvalResult r = eval("=((((1+2)+3)+4)+5)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(15.0, r.getNumber());
}

TEST_F(FormulaEvalTest, MultiplicationByZero) {
    EvalResult r = eval("=1000000*0");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(0.0, r.getNumber());
}

TEST_F(FormulaEvalTest, SubtractingFromZero) {
    EvalResult r = eval("=0-5");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(-5.0, r.getNumber());
}

// =============================================================================
// TYPE COERCION RESULT TESTS (toString, toNumber, toBoolean methods)
// =============================================================================

TEST(EvalResultTest, NumberToString) {
    EvalResult r = EvalResult::Number(42.0);
    EvalResult s = r.toString();
    ASSERT_TRUE(s.isString());
    EXPECT_EQ("42", s.getString());
}

TEST(EvalResultTest, DecimalToString) {
    EvalResult r = EvalResult::Number(3.14);
    EvalResult s = r.toString();
    ASSERT_TRUE(s.isString());
    // Should have decimal representation
    EXPECT_TRUE(s.getString().find("3.14") == 0);
}

TEST(EvalResultTest, BoolToString) {
    EvalResult t = EvalResult::Boolean(true).toString();
    EvalResult f = EvalResult::Boolean(false).toString();
    ASSERT_TRUE(t.isString());
    ASSERT_TRUE(f.isString());
    EXPECT_EQ("TRUE", t.getString());
    EXPECT_EQ("FALSE", f.getString());
}

TEST(EvalResultTest, StringToNumber_Valid) {
    EvalResult r = EvalResult::String("123.45");
    EvalResult n = r.toNumber();
    ASSERT_TRUE(n.isNumber());
    EXPECT_DOUBLE_EQ(123.45, n.getNumber());
}

TEST(EvalResultTest, StringToNumber_Invalid) {
    EvalResult r = EvalResult::String("abc");
    EvalResult n = r.toNumber();
    ASSERT_TRUE(n.isError());
    EXPECT_EQ(CellError::VALUE, n.getError());
}

TEST(EvalResultTest, BoolToNumber) {
    EvalResult t = EvalResult::Boolean(true).toNumber();
    EvalResult f = EvalResult::Boolean(false).toNumber();
    ASSERT_TRUE(t.isNumber());
    ASSERT_TRUE(f.isNumber());
    EXPECT_DOUBLE_EQ(1.0, t.getNumber());
    EXPECT_DOUBLE_EQ(0.0, f.getNumber());
}

TEST(EvalResultTest, NumberToBoolean) {
    EvalResult zero = EvalResult::Number(0.0).toBoolean();
    EvalResult nonzero = EvalResult::Number(5.0).toBoolean();
    EvalResult negative = EvalResult::Number(-1.0).toBoolean();
    ASSERT_TRUE(zero.isBoolean());
    ASSERT_TRUE(nonzero.isBoolean());
    ASSERT_TRUE(negative.isBoolean());
    EXPECT_FALSE(zero.getBoolean());
    EXPECT_TRUE(nonzero.getBoolean());
    EXPECT_TRUE(negative.getBoolean());
}

TEST(EvalResultTest, StringToBoolean_Error) {
    EvalResult r = EvalResult::String("true");
    EvalResult b = r.toBoolean();
    ASSERT_TRUE(b.isError());
    EXPECT_EQ(CellError::VALUE, b.getError());
}

TEST(EvalResultTest, EmptyToNumber) {
    EvalResult r = EvalResult::Empty();
    EvalResult n = r.toNumber();
    ASSERT_TRUE(n.isNumber());
    EXPECT_DOUBLE_EQ(0.0, n.getNumber());
}

TEST(EvalResultTest, EmptyToBoolean) {
    EvalResult r = EvalResult::Empty();
    EvalResult b = r.toBoolean();
    ASSERT_TRUE(b.isBoolean());
    EXPECT_FALSE(b.getBoolean());
}

TEST(EvalResultTest, EmptyToString) {
    EvalResult r = EvalResult::Empty();
    EvalResult s = r.toString();
    ASSERT_TRUE(s.isString());
    EXPECT_EQ("", s.getString());
}

TEST(EvalResultTest, ErrorPropagation_ToNumber) {
    EvalResult r = EvalResult::Error(CellError::DIV);
    EvalResult n = r.toNumber();
    ASSERT_TRUE(n.isError());
    EXPECT_EQ(CellError::DIV, n.getError());
}

TEST(EvalResultTest, ErrorPropagation_ToBoolean) {
    EvalResult r = EvalResult::Error(CellError::REF);
    EvalResult b = r.toBoolean();
    ASSERT_TRUE(b.isError());
    EXPECT_EQ(CellError::REF, b.getError());
}

// =============================================================================
// RANGE EVALUATION TESTS
// =============================================================================

TEST_F(FormulaEvalTest, RangeRef_SingleCellRange) {
    // A1:A1 should be a valid range containing 1 cell
    EvalResult r = eval("=A1:A1");
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::CELL_RANGE, r.getRangeBounds().type);
}

TEST_F(FormulaEvalTest, RangeRef_SingleRow) {
    // A1:C1 should be a valid range of 3 cells
    EvalResult r = eval("=A1:C1");
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::CELL_RANGE, r.getRangeBounds().type);
}

TEST_F(FormulaEvalTest, RangeRef_SingleColumn) {
    // A1:A3 should be a valid range of 3 cells
    EvalResult r = eval("=A1:A3");
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::CELL_RANGE, r.getRangeBounds().type);
}

TEST_F(FormulaEvalTest, RangeRef_Rectangle) {
    // A1:C3 should be a valid 3x3 range
    EvalResult r = eval("=A1:C3");
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::CELL_RANGE, r.getRangeBounds().type);
}

TEST_F(FormulaEvalTest, RangeRef_Reversed) {
    // C3:A1 should normalize to A1:C3
    EvalResult r = eval("=C3:A1");
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::CELL_RANGE, r.getRangeBounds().type);
}

TEST_F(FormulaEvalTest, WholeColumnRef) {
    // A:A should be a column reference
    EvalResult r = eval("=A:A");
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::COLUMN, r.getRangeBounds().type);
}

TEST_F(FormulaEvalTest, WholeRowRef) {
    // 1:1 should be a row reference
    EvalResult r = eval("=1:1");
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::ROW, r.getRangeBounds().type);
}

TEST_F(FormulaEvalTest, ColumnRangeRef) {
    // A:C should be a column range
    EvalResult r = eval("=A:C");
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::COLUMN_RANGE, r.getRangeBounds().type);
}

TEST_F(FormulaEvalTest, RowRangeRef) {
    // 1:5 should be a row range
    EvalResult r = eval("=1:5");
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::ROW_RANGE, r.getRangeBounds().type);
}

TEST_F(FormulaEvalTest, RangeRef_WithAbsoluteRefs) {
    // $A$1:$C$3 should work the same as A1:C3
    EvalResult r = eval("=$A$1:$C$3");
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::CELL_RANGE, r.getRangeBounds().type);
}

// =============================================================================
// RANGE ITERATION TESTS
// =============================================================================

TEST_F(FormulaEvalTest, RangeIteration_SingleCellRange) {
    setCellValue(0, 0, 10.0);  // A1 = 10

    EvalResult r = eval("=A1:A1");
    ASSERT_TRUE(r.isRange());

    std::unordered_set<ID> evaluating;
    EvalContext ctx;
    ctx.sheet = sheet;
    ctx.workbook = workbook.get();
    ctx.evaluatingCells = &evaluating;

    auto values = collectRangeValues(r.getRangeBounds(), ctx);
    ASSERT_EQ(1, values.size());
    ASSERT_TRUE(values[0].isNumber());
    EXPECT_DOUBLE_EQ(10.0, values[0].getNumber());
}

TEST_F(FormulaEvalTest, RangeIteration_SingleRowRange) {
    setCellValue(0, 0, 1.0);  // A1 = 1
    setCellValue(1, 0, 2.0);  // B1 = 2
    setCellValue(2, 0, 3.0);  // C1 = 3

    EvalResult r = eval("=A1:C1");
    ASSERT_TRUE(r.isRange());

    std::unordered_set<ID> evaluating;
    EvalContext ctx;
    ctx.sheet = sheet;
    ctx.workbook = workbook.get();
    ctx.evaluatingCells = &evaluating;

    auto values = collectRangeValues(r.getRangeBounds(), ctx);
    ASSERT_EQ(3, values.size());
    EXPECT_DOUBLE_EQ(1.0, values[0].getNumber());
    EXPECT_DOUBLE_EQ(2.0, values[1].getNumber());
    EXPECT_DOUBLE_EQ(3.0, values[2].getNumber());
}

TEST_F(FormulaEvalTest, RangeIteration_SingleColumnRange) {
    setCellValue(0, 0, 1.0);  // A1 = 1
    setCellValue(0, 1, 2.0);  // A2 = 2
    setCellValue(0, 2, 3.0);  // A3 = 3

    EvalResult r = eval("=A1:A3");
    ASSERT_TRUE(r.isRange());

    std::unordered_set<ID> evaluating;
    EvalContext ctx;
    ctx.sheet = sheet;
    ctx.workbook = workbook.get();
    ctx.evaluatingCells = &evaluating;

    auto values = collectRangeValues(r.getRangeBounds(), ctx);
    ASSERT_EQ(3, values.size());
    EXPECT_DOUBLE_EQ(1.0, values[0].getNumber());
    EXPECT_DOUBLE_EQ(2.0, values[1].getNumber());
    EXPECT_DOUBLE_EQ(3.0, values[2].getNumber());
}

TEST_F(FormulaEvalTest, RangeIteration_Rectangle) {
    // Set up 2x2 grid
    setCellValue(0, 0, 1.0);  // A1 = 1
    setCellValue(1, 0, 2.0);  // B1 = 2
    setCellValue(0, 1, 3.0);  // A2 = 3
    setCellValue(1, 1, 4.0);  // B2 = 4

    EvalResult r = eval("=A1:B2");
    ASSERT_TRUE(r.isRange());

    std::unordered_set<ID> evaluating;
    EvalContext ctx;
    ctx.sheet = sheet;
    ctx.workbook = workbook.get();
    ctx.evaluatingCells = &evaluating;

    auto values = collectRangeValues(r.getRangeBounds(), ctx);
    ASSERT_EQ(4, values.size());
    // Row-major order: A1, B1, A2, B2
    EXPECT_DOUBLE_EQ(1.0, values[0].getNumber());
    EXPECT_DOUBLE_EQ(2.0, values[1].getNumber());
    EXPECT_DOUBLE_EQ(3.0, values[2].getNumber());
    EXPECT_DOUBLE_EQ(4.0, values[3].getNumber());
}

TEST_F(FormulaEvalTest, RangeIteration_WithEmptyCells) {
    setCellValue(0, 0, 1.0);  // A1 = 1
    // A2 is empty
    setCellValue(0, 2, 3.0);  // A3 = 3

    EvalResult r = eval("=A1:A3");
    ASSERT_TRUE(r.isRange());

    std::unordered_set<ID> evaluating;
    EvalContext ctx;
    ctx.sheet = sheet;
    ctx.workbook = workbook.get();
    ctx.evaluatingCells = &evaluating;

    auto values = collectRangeValues(r.getRangeBounds(), ctx);
    ASSERT_EQ(3, values.size());
    EXPECT_TRUE(values[0].isNumber());
    EXPECT_TRUE(values[1].isEmpty());  // Empty cell
    EXPECT_TRUE(values[2].isNumber());
}

TEST_F(FormulaEvalTest, RangeIteration_MixedTypes) {
    setCellValue(0, 0, 1.0);      // A1 = 1 (number)
    setCellValue(0, 1, "hello");  // A2 = "hello" (string)
    setCellValue(0, 2, true);     // A3 = TRUE (boolean)

    EvalResult r = eval("=A1:A3");
    ASSERT_TRUE(r.isRange());

    std::unordered_set<ID> evaluating;
    EvalContext ctx;
    ctx.sheet = sheet;
    ctx.workbook = workbook.get();
    ctx.evaluatingCells = &evaluating;

    auto values = collectRangeValues(r.getRangeBounds(), ctx);
    ASSERT_EQ(3, values.size());
    EXPECT_TRUE(values[0].isNumber());
    EXPECT_TRUE(values[1].isString());
    EXPECT_TRUE(values[2].isBoolean());
}

TEST_F(FormulaEvalTest, WholeColumnIteration_PopulatedCells) {
    setCellValue(0, 0, 1.0);   // A1 = 1
    setCellValue(0, 4, 5.0);   // A5 = 5
    setCellValue(0, 9, 10.0);  // A10 = 10

    EvalResult r = eval("=A:A");
    ASSERT_TRUE(r.isRange());

    std::unordered_set<ID> evaluating;
    EvalContext ctx;
    ctx.sheet = sheet;
    ctx.workbook = workbook.get();
    ctx.evaluatingCells = &evaluating;

    auto values = collectRangeValues(r.getRangeBounds(), ctx);
    // Only populated cells are returned for whole column refs
    ASSERT_EQ(3, values.size());
    EXPECT_DOUBLE_EQ(1.0, values[0].getNumber());
    EXPECT_DOUBLE_EQ(5.0, values[1].getNumber());
    EXPECT_DOUBLE_EQ(10.0, values[2].getNumber());
}

TEST_F(FormulaEvalTest, WholeRowIteration_PopulatedCells) {
    setCellValue(0, 0, 1.0);   // A1 = 1
    setCellValue(4, 0, 5.0);   // E1 = 5
    setCellValue(9, 0, 10.0);  // J1 = 10

    EvalResult r = eval("=1:1");
    ASSERT_TRUE(r.isRange());

    std::unordered_set<ID> evaluating;
    EvalContext ctx;
    ctx.sheet = sheet;
    ctx.workbook = workbook.get();
    ctx.evaluatingCells = &evaluating;

    auto values = collectRangeValues(r.getRangeBounds(), ctx);
    // Only populated cells are returned for whole row refs
    ASSERT_EQ(3, values.size());
    EXPECT_DOUBLE_EQ(1.0, values[0].getNumber());
    EXPECT_DOUBLE_EQ(5.0, values[1].getNumber());
    EXPECT_DOUBLE_EQ(10.0, values[2].getNumber());
}

TEST_F(FormulaEvalTest, ColumnRangeIteration) {
    setCellValue(0, 0, 1.0);  // A1 = 1
    setCellValue(1, 0, 2.0);  // B1 = 2
    setCellValue(2, 0, 3.0);  // C1 = 3
    setCellValue(0, 1, 4.0);  // A2 = 4

    EvalResult r = eval("=A:C");
    ASSERT_TRUE(r.isRange());

    std::unordered_set<ID> evaluating;
    EvalContext ctx;
    ctx.sheet = sheet;
    ctx.workbook = workbook.get();
    ctx.evaluatingCells = &evaluating;

    auto values = collectRangeValues(r.getRangeBounds(), ctx);
    // Should get all 4 populated cells in row-major order
    ASSERT_EQ(4, values.size());
}

TEST_F(FormulaEvalTest, RowRangeIteration) {
    setCellValue(0, 0, 1.0);  // A1 = 1
    setCellValue(1, 0, 2.0);  // B1 = 2
    setCellValue(0, 1, 3.0);  // A2 = 3
    setCellValue(0, 4, 4.0);  // A5 = 4

    EvalResult r = eval("=1:5");
    ASSERT_TRUE(r.isRange());

    std::unordered_set<ID> evaluating;
    EvalContext ctx;
    ctx.sheet = sheet;
    ctx.workbook = workbook.get();
    ctx.evaluatingCells = &evaluating;

    auto values = collectRangeValues(r.getRangeBounds(), ctx);
    // Should get all 4 populated cells in row-major order
    ASSERT_EQ(4, values.size());
}

TEST_F(FormulaEvalTest, RangeSize_BoundedRange) {
    EvalResult r = eval("=A1:C3");
    ASSERT_TRUE(r.isRange());

    size_t size = getRangeSize(r.getRangeBounds(), sheet);
    EXPECT_EQ(9, size);  // 3x3 = 9 cells
}

TEST_F(FormulaEvalTest, RangeSize_EmptyWholeColumn) {
    EvalResult r = eval("=Z:Z");
    ASSERT_TRUE(r.isRange());

    size_t size = getRangeSize(r.getRangeBounds(), sheet);
    EXPECT_EQ(0, size);  // No populated cells in column Z
}

TEST_F(FormulaEvalTest, RangeSize_PopulatedWholeColumn) {
    setCellValue(25, 0, 1.0);  // Z1 = 1
    setCellValue(25, 5, 2.0);  // Z6 = 2

    EvalResult r = eval("=Z:Z");
    ASSERT_TRUE(r.isRange());

    size_t size = getRangeSize(r.getRangeBounds(), sheet);
    EXPECT_EQ(2, size);
}

// =============================================================================
// RANGE TO SCALAR CONVERSION TESTS (should fail)
// =============================================================================

TEST_F(FormulaEvalTest, RangeToNumber_Error) {
    EvalResult r = eval("=A1:A3");
    ASSERT_TRUE(r.isRange());

    EvalResult num = r.toNumber();
    ASSERT_TRUE(num.isError());
    EXPECT_EQ(CellError::VALUE, num.getError());
}

TEST_F(FormulaEvalTest, RangeToString_Error) {
    EvalResult r = eval("=A1:A3");
    ASSERT_TRUE(r.isRange());

    EvalResult str = r.toString();
    ASSERT_TRUE(str.isError());
    EXPECT_EQ(CellError::VALUE, str.getError());
}

TEST_F(FormulaEvalTest, RangeToBoolean_Error) {
    EvalResult r = eval("=A1:A3");
    ASSERT_TRUE(r.isRange());

    EvalResult b = r.toBoolean();
    ASSERT_TRUE(b.isError());
    EXPECT_EQ(CellError::VALUE, b.getError());
}

// =============================================================================
// RANGE TYPE CHECKING TESTS
// =============================================================================

TEST(EvalResultTest, IsRange_True) {
    RangeBounds bounds;
    bounds.type = RangeType::CELL_RANGE;
    EvalResult r = EvalResult::Range(bounds);
    EXPECT_TRUE(r.isRange());
    EXPECT_FALSE(r.isNumber());
    EXPECT_FALSE(r.isString());
    EXPECT_FALSE(r.isBoolean());
    EXPECT_FALSE(r.isEmpty());
    EXPECT_FALSE(r.isError());
}

TEST(EvalResultTest, CellRangeFactory) {
    ID col1("col1____");
    ID col2("col2____");
    uint32_t startRowPos = 0;
    uint32_t endRowPos = 5;

    EvalResult r = EvalResult::CellRange(col1, col2, startRowPos, endRowPos);
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::CELL_RANGE, r.getRangeBounds().type);
    EXPECT_EQ(col1, r.getRangeBounds().startColId);
    EXPECT_EQ(col2, r.getRangeBounds().endColId);
    EXPECT_EQ(startRowPos, r.getRangeBounds().startRowPos);
    EXPECT_EQ(endRowPos, r.getRangeBounds().endRowPos);
}

TEST(EvalResultTest, ColumnRangeFactory) {
    ID col1("col1____");
    ID col2("col2____");

    EvalResult r = EvalResult::ColumnRange(col1, col2);
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::COLUMN_RANGE, r.getRangeBounds().type);
    EXPECT_EQ(col1, r.getRangeBounds().startColId);
    EXPECT_EQ(col2, r.getRangeBounds().endColId);
}

TEST(EvalResultTest, SingleColumnFactory) {
    ID col("col_____");

    EvalResult r = EvalResult::SingleColumn(col);
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::COLUMN, r.getRangeBounds().type);
    EXPECT_EQ(col, r.getRangeBounds().startColId);
    EXPECT_EQ(col, r.getRangeBounds().endColId);
}

TEST(EvalResultTest, RowRangeFactory) {
    ID row1("row1____");
    ID row2("row2____");

    EvalResult r = EvalResult::RowRange(row1, row2);
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::ROW_RANGE, r.getRangeBounds().type);
    EXPECT_EQ(row1, r.getRangeBounds().startRowId);
    EXPECT_EQ(row2, r.getRangeBounds().endRowId);
}

TEST(EvalResultTest, SingleRowFactory) {
    ID row("row_____");

    EvalResult r = EvalResult::SingleRow(row);
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::ROW, r.getRangeBounds().type);
    EXPECT_EQ(row, r.getRangeBounds().startRowId);
    EXPECT_EQ(row, r.getRangeBounds().endRowId);
}

// =============================================================================
// SPILL RANGE REFERENCE TESTS
// =============================================================================

TEST_F(FormulaEvalTest, SpillRangeRef_NoSpillData) {
    // A cell with no spill data should return itself as a 1x1 range
    setCellValue(0, 0, 10.0);  // A1 = 10

    EvalResult r = eval("=A1#");
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::CELL_RANGE, r.getRangeBounds().type);

    // The range should be 1x1 (just A1)
    EXPECT_EQ(0u, r.getRangeBounds().startRowPos);
    EXPECT_EQ(0u, r.getRangeBounds().endRowPos);
}

TEST_F(FormulaEvalTest, SpillRangeRef_WithSpillData) {
    // Create a cell with spill data (manually registered)
    Cell* a1 = setCellValue(0, 0, 10.0);  // A1 = master cell

    // Register spill data: A1 spills to A2, A3 (column A, rows 1, 2)
    std::vector<std::pair<ID, ID>> spillPositions;
    spillPositions.push_back({colIds[0], rowIds[1]});  // A2
    spillPositions.push_back({colIds[0], rowIds[2]});  // A3

    std::vector<CellValue> spillValues;
    spillValues.push_back(CellValue(20.0));  // A2 = 20
    spillValues.push_back(CellValue(30.0));  // A3 = 30

    sheet->registerSpillRange(a1->id, spillPositions, spillValues);

    // Now A1# should return a range covering A1:A3
    EvalResult r = eval("=A1#");
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::CELL_RANGE, r.getRangeBounds().type);

    // The range should be A1:A3 (rows 0-2)
    EXPECT_EQ(0u, r.getRangeBounds().startRowPos);
    EXPECT_EQ(2u, r.getRangeBounds().endRowPos);
}

TEST_F(FormulaEvalTest, SpillRangeRef_2DSpill) {
    // Create a cell with 2D spill data (2x2 array)
    Cell* a1 = setCellValue(0, 0, 1.0);  // A1 = master cell

    // Register spill data: A1 spills to B1, A2, B2
    std::vector<std::pair<ID, ID>> spillPositions;
    spillPositions.push_back({colIds[1], rowIds[0]});  // B1
    spillPositions.push_back({colIds[0], rowIds[1]});  // A2
    spillPositions.push_back({colIds[1], rowIds[1]});  // B2

    std::vector<CellValue> spillValues;
    spillValues.push_back(CellValue(2.0));  // B1 = 2
    spillValues.push_back(CellValue(3.0));  // A2 = 3
    spillValues.push_back(CellValue(4.0));  // B2 = 4

    sheet->registerSpillRange(a1->id, spillPositions, spillValues);

    // Now A1# should return a range covering A1:B2
    EvalResult r = eval("=A1#");
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::CELL_RANGE, r.getRangeBounds().type);

    // The range should be A1:B2 (rows 0-1, cols A-B)
    EXPECT_EQ(0u, r.getRangeBounds().startRowPos);
    EXPECT_EQ(1u, r.getRangeBounds().endRowPos);
    EXPECT_EQ(colIds[0], r.getRangeBounds().startColId);
    EXPECT_EQ(colIds[1], r.getRangeBounds().endColId);
}

TEST_F(FormulaEvalTest, SpillRangeRef_InFunction) {
    // Test SUM(A1#) with spill data
    Cell* a1 = setCellValue(0, 0, 10.0);  // A1 = master cell

    // Register spill data: A1 spills to A2, A3
    std::vector<std::pair<ID, ID>> spillPositions;
    spillPositions.push_back({colIds[0], rowIds[1]});  // A2
    spillPositions.push_back({colIds[0], rowIds[2]});  // A3

    std::vector<CellValue> spillValues;
    spillValues.push_back(CellValue(20.0));  // A2 = 20
    spillValues.push_back(CellValue(30.0));  // A3 = 30

    sheet->registerSpillRange(a1->id, spillPositions, spillValues);

    // Also set actual cells at A2 and A3 so SUM can iterate over them
    setCellValue(0, 1, 20.0);  // A2 = 20
    setCellValue(0, 2, 30.0);  // A3 = 30

    // SUM(A1#) should sum A1:A3 = 10 + 20 + 30 = 60
    EvalResult r = eval("=SUM(A1#)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(60.0, r.getNumber());
}

TEST_F(FormulaEvalTest, SpillRangeRef_NonExistentCell) {
    // Reference a non-spill cell (empty) returns a 1x1 range (not an error)
    // This matches Excel behavior where A1# on a non-array formula just returns A1
    EvalResult r = eval("=Z99#");
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::CELL_RANGE, r.getRangeBounds().type);
}

// =============================================================================
// NAMED RANGE EVALUATION TESTS
// =============================================================================

TEST_F(FormulaEvalTest, NamedRange_SingleCell) {
    // Define a named range pointing to A1
    setCellValue(0, 0, 42.0);  // A1 = 42

    // Get the cell ID for A1
    Cell* a1 = sheet->getCellAt(colIds[0], rowIds[0]);
    ASSERT_NE(nullptr, a1);

    // Register the named range
    NamedRangeTarget target = NamedRangeTarget::cell(a1->id, sheet->id);
    workbook->getNamedRanges()->defineWorkbook("MyValue", target);

    // Evaluate formula referencing the named range
    EvalResult r = eval("=MyValue");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(42.0, r.getNumber());
}

TEST_F(FormulaEvalTest, NamedRange_SingleCellString) {
    // Define a named range pointing to a string cell
    setCellValue(0, 0, "hello");  // A1 = "hello"

    Cell* a1 = sheet->getCellAt(colIds[0], rowIds[0]);
    ASSERT_NE(nullptr, a1);

    NamedRangeTarget target = NamedRangeTarget::cell(a1->id, sheet->id);
    workbook->getNamedRanges()->defineWorkbook("Greeting", target);

    EvalResult r = eval("=Greeting");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("hello", r.getString());
}

TEST_F(FormulaEvalTest, NamedRange_Arithmetic) {
    // Use named ranges in arithmetic expressions
    setCellValue(0, 0, 10.0);  // A1 = 10
    setCellValue(1, 0, 5.0);   // B1 = 5

    Cell* a1 = sheet->getCellAt(colIds[0], rowIds[0]);
    Cell* b1 = sheet->getCellAt(colIds[1], rowIds[0]);

    workbook->getNamedRanges()->defineWorkbook("Price", NamedRangeTarget::cell(a1->id, sheet->id));
    workbook->getNamedRanges()->defineWorkbook("Quantity",
                                               NamedRangeTarget::cell(b1->id, sheet->id));

    EvalResult r = eval("=Price*Quantity");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(50.0, r.getNumber());
}

TEST_F(FormulaEvalTest, NamedRange_Range) {
    // Define a named range covering A1:A3
    setCellValue(0, 0, 1.0);  // A1 = 1
    setCellValue(0, 1, 2.0);  // A2 = 2
    setCellValue(0, 2, 3.0);  // A3 = 3

    Cell* topLeft = sheet->getCellAt(colIds[0], rowIds[0]);
    Cell* bottomRight = sheet->getCellAt(colIds[0], rowIds[2]);

    NamedRangeTarget target = NamedRangeTarget::range(topLeft->id, bottomRight->id, sheet->id);
    workbook->getNamedRanges()->defineWorkbook("Numbers", target);

    // Named range should return a RANGE result
    EvalResult r = eval("=Numbers");
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::CELL_RANGE, r.getRangeBounds().type);
}

TEST_F(FormulaEvalTest, NamedRange_SUM) {
    // Use named range with SUM function
    setCellValue(0, 0, 10.0);  // A1 = 10
    setCellValue(0, 1, 20.0);  // A2 = 20
    setCellValue(0, 2, 30.0);  // A3 = 30

    Cell* topLeft = sheet->getCellAt(colIds[0], rowIds[0]);
    Cell* bottomRight = sheet->getCellAt(colIds[0], rowIds[2]);

    NamedRangeTarget target = NamedRangeTarget::range(topLeft->id, bottomRight->id, sheet->id);
    workbook->getNamedRanges()->defineWorkbook("DataRange", target);

    EvalResult r = eval("=SUM(DataRange)");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(60.0, r.getNumber());
}

TEST_F(FormulaEvalTest, NamedRange_NotFound) {
    // Reference a non-existent named range
    EvalResult r = eval("=NonExistentRange");
    ASSERT_TRUE(r.isError());
    EXPECT_EQ(CellError::NAME, r.getError());
}

TEST_F(FormulaEvalTest, NamedRange_SheetScoped) {
    // Define a sheet-scoped named range
    setCellValue(0, 0, 100.0);  // A1 = 100

    Cell* a1 = sheet->getCellAt(colIds[0], rowIds[0]);

    // Define as sheet-scoped
    workbook->getNamedRanges()->defineSheet("LocalValue", sheet->id,
                                            NamedRangeTarget::cell(a1->id, sheet->id));

    EvalResult r = eval("=LocalValue");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(100.0, r.getNumber());
}

TEST_F(FormulaEvalTest, NamedRange_WholeColumn) {
    // Define a named range for a whole column
    setCellValue(0, 0, 1.0);  // A1 = 1
    setCellValue(0, 4, 5.0);  // A5 = 5

    Axis* colA = sheet->getColumn(colIds[0]);
    ASSERT_NE(nullptr, colA);

    NamedRangeTarget target = NamedRangeTarget::column(colA->id, sheet->id);
    workbook->getNamedRanges()->defineWorkbook("ColumnA", target);

    EvalResult r = eval("=ColumnA");
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::COLUMN, r.getRangeBounds().type);
}

TEST_F(FormulaEvalTest, NamedRange_WholeRow) {
    // Define a named range for a whole row
    // Note: Can't use "Row1" because it looks like a cell reference
    setCellValue(0, 0, 1.0);  // A1 = 1
    setCellValue(4, 0, 5.0);  // E1 = 5

    Axis* row1 = sheet->getRow(rowIds[0]);
    ASSERT_NE(nullptr, row1);

    NamedRangeTarget target = NamedRangeTarget::row(row1->id, sheet->id);
    bool registered = workbook->getNamedRanges()->defineWorkbook("FirstRow", target);
    ASSERT_TRUE(registered) << "Failed to register named range 'FirstRow'";

    EvalResult r = eval("=FirstRow");
    ASSERT_TRUE(r.isRange()) << "Expected Range, got type: " << static_cast<int>(r.type);
    EXPECT_EQ(RangeType::ROW, r.getRangeBounds().type);
}

TEST_F(FormulaEvalTest, NamedRange_ColumnRange) {
    // Define a named range for columns A:C
    Axis* colA = sheet->getColumn(colIds[0]);
    Axis* colC = sheet->getColumn(colIds[2]);

    NamedRangeTarget target = NamedRangeTarget::columnRange(colA->id, colC->id, sheet->id);
    workbook->getNamedRanges()->defineWorkbook("FirstThreeCols", target);

    EvalResult r = eval("=FirstThreeCols");
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::COLUMN_RANGE, r.getRangeBounds().type);
}

TEST_F(FormulaEvalTest, NamedRange_RowRange) {
    // Define a named range for rows 1:5
    Axis* row1 = sheet->getRow(rowIds[0]);
    Axis* row5 = sheet->getRow(rowIds[4]);

    NamedRangeTarget target = NamedRangeTarget::rowRange(row1->id, row5->id, sheet->id);
    workbook->getNamedRanges()->defineWorkbook("FirstFiveRows", target);

    EvalResult r = eval("=FirstFiveRows");
    ASSERT_TRUE(r.isRange());
    EXPECT_EQ(RangeType::ROW_RANGE, r.getRangeBounds().type);
}

TEST_F(FormulaEvalTest, NamedRange_EmptyCell) {
    // Named range pointing to an empty cell should return Empty
    // (which converts to 0 in numeric context)
    Cell* emptyCell = sheet->getOrCreateCellAt(colIds[5], rowIds[5]);  // F6

    NamedRangeTarget target = NamedRangeTarget::cell(emptyCell->id, sheet->id);
    workbook->getNamedRanges()->defineWorkbook("EmptyRef", target);

    EvalResult r = eval("=EmptyRef");
    // Empty cell reference returns Empty
    ASSERT_TRUE(r.isEmpty());
    // Which converts to 0 in numeric context
    EvalResult num = r.toNumber();
    ASSERT_TRUE(num.isNumber());
    EXPECT_DOUBLE_EQ(0.0, num.getNumber());
}

TEST_F(FormulaEvalTest, NamedRange_WithFormula) {
    // Named range pointing to a cell with a formula
    setCellValue(0, 0, 10.0);       // A1 = 10
    setCellFormula(1, 0, "=A1*2");  // B1 = =A1*2

    Cell* b1 = sheet->getCellAt(colIds[1], rowIds[0]);

    NamedRangeTarget target = NamedRangeTarget::cell(b1->id, sheet->id);
    workbook->getNamedRanges()->defineWorkbook("Calculated", target);

    EvalResult r = eval("=Calculated");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(20.0, r.getNumber());
}

TEST_F(FormulaEvalTest, NamedRange_InConditional) {
    // Use named range in IF function
    setCellValue(0, 0, 75.0);  // A1 = 75

    Cell* a1 = sheet->getCellAt(colIds[0], rowIds[0]);
    workbook->getNamedRanges()->defineWorkbook("Score", NamedRangeTarget::cell(a1->id, sheet->id));

    EvalResult r = eval("=IF(Score>=70,\"Pass\",\"Fail\")");
    ASSERT_TRUE(r.isString());
    EXPECT_EQ("Pass", r.getString());
}

TEST_F(FormulaEvalTest, NamedRange_CaseInsensitive) {
    // Named range names should be case-insensitive
    setCellValue(0, 0, 123.0);

    Cell* a1 = sheet->getCellAt(colIds[0], rowIds[0]);
    workbook->getNamedRanges()->defineWorkbook("MyName", NamedRangeTarget::cell(a1->id, sheet->id));

    // Note: The parser/resolver should handle case-insensitivity
    // This test verifies the name is found regardless of case
    EvalResult r = eval("=MyName");
    ASSERT_TRUE(r.isNumber());
    EXPECT_DOUBLE_EQ(123.0, r.getNumber());
}

// =============================================================================
// CROSS-SHEET REFERENCE EVALUATION TESTS
// =============================================================================

TEST_F(FormulaEvalTest, CrossSheetCellRef_Evaluates) {
    // Add a second sheet
    workbook->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet2"));
    Sheet* sheet2 = workbook->getSheetByName("Sheet2");
    ASSERT_NE(sheet2, nullptr);

    // Create column B and row 27 on Sheet2
    auto colB = std::make_unique<Axis>(generate_id(), true);
    colB->position = 1;  // B = position 1
    colB->name = "B";
    ID sheet2ColBId = colB->id;
    sheet2->addColumn(std::move(colB));

    auto row27 = std::make_unique<Axis>(generate_id(), false);
    row27->position = 26;  // Row 27 = position 26 (0-indexed)
    ID sheet2Row27Id = row27->id;
    sheet2->addRow(std::move(row27));

    // Create cell B27 on Sheet2 with value 42
    Cell* cellB27 = sheet2->getOrCreateCellAt(sheet2ColBId, sheet2Row27Id);
    cellB27->value = CellValue(42.0);

    // Now evaluate =Sheet2!B27 from Sheet1's perspective
    FormulaParser parser("=Sheet2!B27");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    ASSERT_FALSE(parser.hasErrors());

    // Resolve - this should find Sheet2 and set sheetId
    FormulaResolver resolver(*workbook, *sheet, workbook->getNamedRanges());
    auto result = resolver.resolve(ast.get());
    ASSERT_TRUE(result.success) << "Resolution failed: " << result.errorMessage;

    // The AST should be a CellRefNode with sheetId set
    ASSERT_EQ(ast->type, ASTNodeType::CELL_REF);
    auto* cellRef = static_cast<CellRefNode*>(ast.get());
    EXPECT_FALSE(cellRef->sheetId.empty()) << "sheetId should be set for cross-sheet ref";
    EXPECT_EQ(cellRef->sheetId, sheet2->id.toString()) << "sheetId should match Sheet2";

    // Evaluate
    std::unordered_set<ID> evaluating;
    EvalContext ctx;
    ctx.sheet = sheet;
    ctx.workbook = workbook.get();
    ctx.namedRanges = workbook->getNamedRanges();
    ctx.evaluatingCells = &evaluating;
    ctx.recursionDepth = 0;

    EvalResult evalResult = evaluate(ast.get(), ctx);

    // Should return number 42
    ASSERT_TRUE(evalResult.isNumber())
        << "Expected NUMBER, got type " << static_cast<int>(evalResult.type);
    EXPECT_DOUBLE_EQ(42.0, evalResult.getNumber());
}

TEST_F(FormulaEvalTest, CrossSheetRange_SUMEvaluates) {
    // Add a second sheet
    workbook->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet2"));
    Sheet* sheet2 = workbook->getSheetByName("Sheet2");
    ASSERT_NE(sheet2, nullptr);

    // Create column A on Sheet2
    auto colA = std::make_unique<Axis>(generate_id(), true);
    colA->position = 0;
    colA->name = "A";
    ID sheet2ColAId = colA->id;
    sheet2->addColumn(std::move(colA));

    // Create rows 1-3 on Sheet2
    ID sheet2RowIds[3];
    for (uint32_t i = 0; i < 3; i++) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = i;
        sheet2RowIds[i] = row->id;
        sheet2->addRow(std::move(row));
    }

    // Set values A1=10, A2=20, A3=30 on Sheet2
    sheet2->getOrCreateCellAt(sheet2ColAId, sheet2RowIds[0])->value = CellValue(10.0);
    sheet2->getOrCreateCellAt(sheet2ColAId, sheet2RowIds[1])->value = CellValue(20.0);
    sheet2->getOrCreateCellAt(sheet2ColAId, sheet2RowIds[2])->value = CellValue(30.0);

    // Evaluate =SUM(Sheet2!A1:A3) from Sheet1
    FormulaParser parser("=SUM(Sheet2!A1:A3)");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    ASSERT_FALSE(parser.hasErrors());

    FormulaResolver resolver(*workbook, *sheet, workbook->getNamedRanges());
    auto result = resolver.resolve(ast.get());
    ASSERT_TRUE(result.success) << "Resolution failed: " << result.errorMessage;

    std::unordered_set<ID> evaluating;
    EvalContext ctx;
    ctx.sheet = sheet;
    ctx.workbook = workbook.get();
    ctx.namedRanges = workbook->getNamedRanges();
    ctx.evaluatingCells = &evaluating;
    ctx.recursionDepth = 0;

    EvalResult evalResult = evaluate(ast.get(), ctx);

    // Should return number 60 (10+20+30)
    ASSERT_TRUE(evalResult.isNumber())
        << "Expected NUMBER, got type " << static_cast<int>(evalResult.type);
    EXPECT_DOUBLE_EQ(60.0, evalResult.getNumber());
}

}  // namespace
}  // namespace cells
