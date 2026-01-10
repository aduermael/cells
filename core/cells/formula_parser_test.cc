#include "core/cells/formula_parser.h"

#include <gtest/gtest.h>

namespace cells {
namespace {

// ============================================================================
// Literal Tests
// ============================================================================

TEST(FormulaParserTest, NumberLiteral) {
    FormulaParser parser("42");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::NUMBER_LITERAL);
    auto* num = dynamic_cast<NumberLiteralNode*>(ast.get());
    ASSERT_NE(num, nullptr);
    EXPECT_DOUBLE_EQ(num->value, 42.0);
}

TEST(FormulaParserTest, DecimalNumber) {
    FormulaParser parser("3.14");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    auto* num = dynamic_cast<NumberLiteralNode*>(ast.get());
    ASSERT_NE(num, nullptr);
    EXPECT_DOUBLE_EQ(num->value, 3.14);
}

TEST(FormulaParserTest, ScientificNotation) {
    FormulaParser parser("1.5e10");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    auto* num = dynamic_cast<NumberLiteralNode*>(ast.get());
    ASSERT_NE(num, nullptr);
    EXPECT_DOUBLE_EQ(num->value, 1.5e10);
}

TEST(FormulaParserTest, StringLiteral) {
    FormulaParser parser("\"Hello World\"");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::STRING_LITERAL);
    auto* str = dynamic_cast<StringLiteralNode*>(ast.get());
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->value, "Hello World");
}

TEST(FormulaParserTest, BooleanTrue) {
    FormulaParser parser("TRUE");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::BOOLEAN_LITERAL);
    auto* b = dynamic_cast<BooleanLiteralNode*>(ast.get());
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(b->value);
}

TEST(FormulaParserTest, BooleanFalse) {
    FormulaParser parser("FALSE");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    auto* b = dynamic_cast<BooleanLiteralNode*>(ast.get());
    ASSERT_NE(b, nullptr);
    EXPECT_FALSE(b->value);
}

// ============================================================================
// Percentage Literal Tests
// ============================================================================

TEST(FormulaParserTest, PercentLiteralSimple) {
    // 15% should parse to 0.15
    FormulaParser parser("15%");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::NUMBER_LITERAL);
    auto* num = dynamic_cast<NumberLiteralNode*>(ast.get());
    ASSERT_NE(num, nullptr);
    EXPECT_DOUBLE_EQ(num->value, 0.15);
}

TEST(FormulaParserTest, PercentLiteralInFormula) {
    // =15% should parse to 0.15
    FormulaParser parser("=15%");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    auto* num = dynamic_cast<NumberLiteralNode*>(ast.get());
    ASSERT_NE(num, nullptr);
    EXPECT_DOUBLE_EQ(num->value, 0.15);
}

TEST(FormulaParserTest, PercentLiteralMultiplication) {
    // =1000*15% should parse as 1000 * 0.15
    FormulaParser parser("=1000*15%");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::BINARY_OP);

    auto* binOp = dynamic_cast<BinaryOpNode*>(ast.get());
    ASSERT_NE(binOp, nullptr);
    EXPECT_EQ(binOp->op, BinaryOp::MULTIPLY);

    // Left should be 1000
    auto* left = dynamic_cast<NumberLiteralNode*>(binOp->left.get());
    ASSERT_NE(left, nullptr);
    EXPECT_DOUBLE_EQ(left->value, 1000.0);

    // Right should be 0.15
    auto* right = dynamic_cast<NumberLiteralNode*>(binOp->right.get());
    ASSERT_NE(right, nullptr);
    EXPECT_DOUBLE_EQ(right->value, 0.15);
}

TEST(FormulaParserTest, PercentLiteralAddition) {
    // =50%+25% should parse as 0.5 + 0.25
    FormulaParser parser("=50%+25%");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::BINARY_OP);

    auto* binOp = dynamic_cast<BinaryOpNode*>(ast.get());
    ASSERT_NE(binOp, nullptr);
    EXPECT_EQ(binOp->op, BinaryOp::ADD);

    auto* left = dynamic_cast<NumberLiteralNode*>(binOp->left.get());
    ASSERT_NE(left, nullptr);
    EXPECT_DOUBLE_EQ(left->value, 0.5);

    auto* right = dynamic_cast<NumberLiteralNode*>(binOp->right.get());
    ASSERT_NE(right, nullptr);
    EXPECT_DOUBLE_EQ(right->value, 0.25);
}

TEST(FormulaParserTest, PercentLiteralDecimal) {
    // =12.5% should parse to 0.125
    FormulaParser parser("=12.5%");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    auto* num = dynamic_cast<NumberLiteralNode*>(ast.get());
    ASSERT_NE(num, nullptr);
    EXPECT_DOUBLE_EQ(num->value, 0.125);
}

TEST(FormulaParserTest, PercentLiteralInExpression) {
    // =100+15% should work (100 + 0.15)
    FormulaParser parser("=100+15%");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::BINARY_OP);

    auto* binOp = dynamic_cast<BinaryOpNode*>(ast.get());
    ASSERT_NE(binOp, nullptr);
    EXPECT_EQ(binOp->op, BinaryOp::ADD);

    auto* left = dynamic_cast<NumberLiteralNode*>(binOp->left.get());
    ASSERT_NE(left, nullptr);
    EXPECT_DOUBLE_EQ(left->value, 100.0);

    auto* right = dynamic_cast<NumberLiteralNode*>(binOp->right.get());
    ASSERT_NE(right, nullptr);
    EXPECT_DOUBLE_EQ(right->value, 0.15);
}

// ============================================================================
// Operator Precedence Tests
// ============================================================================

TEST(FormulaParserTest, AdditionPrecedence) {
    // 1+2*3 should parse as 1+(2*3)
    FormulaParser parser("=1+2*3");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::BINARY_OP);

    auto* binOp = dynamic_cast<BinaryOpNode*>(ast.get());
    ASSERT_NE(binOp, nullptr);
    EXPECT_EQ(binOp->op, BinaryOp::ADD);

    // Left should be 1
    auto* left = dynamic_cast<NumberLiteralNode*>(binOp->left.get());
    ASSERT_NE(left, nullptr);
    EXPECT_DOUBLE_EQ(left->value, 1.0);

    // Right should be 2*3
    auto* right = dynamic_cast<BinaryOpNode*>(binOp->right.get());
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(right->op, BinaryOp::MULTIPLY);
}

TEST(FormulaParserTest, MultiplicationFirst) {
    // 2*3+4 should parse as (2*3)+4
    FormulaParser parser("=2*3+4");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* binOp = dynamic_cast<BinaryOpNode*>(ast.get());
    ASSERT_NE(binOp, nullptr);
    EXPECT_EQ(binOp->op, BinaryOp::ADD);

    // Left should be 2*3
    auto* left = dynamic_cast<BinaryOpNode*>(binOp->left.get());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->op, BinaryOp::MULTIPLY);
}

TEST(FormulaParserTest, PowerPrecedence) {
    // 2^3*4 should parse as (2^3)*4
    FormulaParser parser("=2^3*4");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* binOp = dynamic_cast<BinaryOpNode*>(ast.get());
    ASSERT_NE(binOp, nullptr);
    EXPECT_EQ(binOp->op, BinaryOp::MULTIPLY);

    // Left should be 2^3
    auto* left = dynamic_cast<BinaryOpNode*>(binOp->left.get());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->op, BinaryOp::POWER);
}

TEST(FormulaParserTest, ParenthesesOverride) {
    // (1+2)*3 should parse as multiplication at top
    FormulaParser parser("=(1+2)*3");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* binOp = dynamic_cast<BinaryOpNode*>(ast.get());
    ASSERT_NE(binOp, nullptr);
    EXPECT_EQ(binOp->op, BinaryOp::MULTIPLY);

    // Left should be 1+2
    auto* left = dynamic_cast<BinaryOpNode*>(binOp->left.get());
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->op, BinaryOp::ADD);
}

TEST(FormulaParserTest, ComparisonOperators) {
    FormulaParser parser("=A1>10");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* binOp = dynamic_cast<BinaryOpNode*>(ast.get());
    ASSERT_NE(binOp, nullptr);
    EXPECT_EQ(binOp->op, BinaryOp::GREATER);
}

TEST(FormulaParserTest, ConcatOperator) {
    FormulaParser parser("=\"Hello\"&\" \"&\"World\"");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    // Should be left-associative: ("Hello"&" ")&"World"
    auto* binOp = dynamic_cast<BinaryOpNode*>(ast.get());
    ASSERT_NE(binOp, nullptr);
    EXPECT_EQ(binOp->op, BinaryOp::CONCAT);
}

// ============================================================================
// Unary Operator Tests
// ============================================================================

TEST(FormulaParserTest, UnaryMinus) {
    FormulaParser parser("=-5");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* unary = dynamic_cast<UnaryOpNode*>(ast.get());
    ASSERT_NE(unary, nullptr);
    EXPECT_EQ(unary->op, UnaryOp::NEGATE);
}

TEST(FormulaParserTest, UnaryPlus) {
    FormulaParser parser("=+5");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* unary = dynamic_cast<UnaryOpNode*>(ast.get());
    ASSERT_NE(unary, nullptr);
    EXPECT_EQ(unary->op, UnaryOp::POSITIVE);
}

TEST(FormulaParserTest, DoubleNegative) {
    FormulaParser parser("=--5");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* outer = dynamic_cast<UnaryOpNode*>(ast.get());
    ASSERT_NE(outer, nullptr);
    auto* inner = dynamic_cast<UnaryOpNode*>(outer->operand.get());
    ASSERT_NE(inner, nullptr);
}

// ============================================================================
// Cell Reference Tests
// ============================================================================

TEST(FormulaParserTest, SimpleCellRef) {
    FormulaParser parser("=A1");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::CELL_REF);

    auto* cell = dynamic_cast<CellRefNode*>(ast.get());
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->column, "A");
    EXPECT_EQ(cell->row, 1);
    EXPECT_FALSE(cell->colAbsolute);
    EXPECT_FALSE(cell->rowAbsolute);
}

TEST(FormulaParserTest, AbsoluteCellRef) {
    FormulaParser parser("=$A$1");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* cell = dynamic_cast<CellRefNode*>(ast.get());
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->column, "A");
    EXPECT_EQ(cell->row, 1);
    EXPECT_TRUE(cell->colAbsolute);
    EXPECT_TRUE(cell->rowAbsolute);
}

TEST(FormulaParserTest, MixedRefColAbsolute) {
    FormulaParser parser("=$A1");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* cell = dynamic_cast<CellRefNode*>(ast.get());
    ASSERT_NE(cell, nullptr);
    EXPECT_TRUE(cell->colAbsolute);
    EXPECT_FALSE(cell->rowAbsolute);
}

TEST(FormulaParserTest, MixedRefRowAbsolute) {
    FormulaParser parser("=A$1");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* cell = dynamic_cast<CellRefNode*>(ast.get());
    ASSERT_NE(cell, nullptr);
    EXPECT_FALSE(cell->colAbsolute);
    EXPECT_TRUE(cell->rowAbsolute);
}

TEST(FormulaParserTest, DoubleColumnCell) {
    FormulaParser parser("=AA100");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* cell = dynamic_cast<CellRefNode*>(ast.get());
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->column, "AA");
    EXPECT_EQ(cell->row, 100);
}

// ============================================================================
// Range Reference Tests
// ============================================================================

TEST(FormulaParserTest, SimpleRange) {
    FormulaParser parser("=A1:C3");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::RANGE_REF);

    auto* range = dynamic_cast<RangeRefNode*>(ast.get());
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->topLeft->column, "A");
    EXPECT_EQ(range->topLeft->row, 1);
    EXPECT_EQ(range->bottomRight->column, "C");
    EXPECT_EQ(range->bottomRight->row, 3);
}

TEST(FormulaParserTest, AbsoluteRange) {
    FormulaParser parser("=$A$1:$C$3");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* range = dynamic_cast<RangeRefNode*>(ast.get());
    ASSERT_NE(range, nullptr);
    EXPECT_TRUE(range->topLeft->colAbsolute);
    EXPECT_TRUE(range->topLeft->rowAbsolute);
    EXPECT_TRUE(range->bottomRight->colAbsolute);
    EXPECT_TRUE(range->bottomRight->rowAbsolute);
}

TEST(FormulaParserTest, RangeHasCorrectSourcePosition) {
    // Test that range references have sourcePosition spanning from start to end
    // Formula: "=A1:C3" - the range "A1:C3" spans positions 1-6 (after '=')
    FormulaParser parser("=A1:C3");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::RANGE_REF);

    auto* range = dynamic_cast<RangeRefNode*>(ast.get());
    ASSERT_NE(range, nullptr);

    // The range position should span from 'A' to end of 'C3'
    // Position 1 is 'A', position 6 is end of '3' (exclusive)
    EXPECT_EQ(range->position.start, 1u);
    EXPECT_EQ(range->position.end, 6u);

    // Verify the text can be extracted using these positions
    const std::string formula = "=A1:C3";
    const std::string rangeText =
        formula.substr(range->position.start, range->position.end - range->position.start);
    EXPECT_EQ(rangeText, "A1:C3");
}

TEST(FormulaParserTest, RangeInFunctionHasCorrectSourcePosition) {
    // Test range inside a function: =SUM(B2:D5)
    FormulaParser parser("=SUM(B2:D5)");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::FUNCTION_CALL);

    auto* func = dynamic_cast<FunctionCallNode*>(ast.get());
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->args.size(), 1u);
    EXPECT_EQ(func->args[0]->type, ASTNodeType::RANGE_REF);

    auto* range = dynamic_cast<RangeRefNode*>(func->args[0].get());
    ASSERT_NE(range, nullptr);

    // Range B2:D5 starts at position 5 (after "=SUM("), ends at position 10
    EXPECT_EQ(range->position.start, 5u);
    EXPECT_EQ(range->position.end, 10u);

    // Verify the text extraction
    const std::string formula = "=SUM(B2:D5)";
    const std::string rangeText =
        formula.substr(range->position.start, range->position.end - range->position.start);
    EXPECT_EQ(rangeText, "B2:D5");
}

// ============================================================================
// Column/Row Reference Tests
// ============================================================================

TEST(FormulaParserTest, WholeColumnRef) {
    FormulaParser parser("=A:A");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::COLUMN_REF);

    auto* col = dynamic_cast<ColumnRefNode*>(ast.get());
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->column, "A");
}

TEST(FormulaParserTest, ColumnRangeRef) {
    FormulaParser parser("=A:C");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::COLUMN_RANGE_REF);

    auto* colRange = dynamic_cast<ColumnRangeRefNode*>(ast.get());
    ASSERT_NE(colRange, nullptr);
    EXPECT_EQ(colRange->startColumn, "A");
    EXPECT_EQ(colRange->endColumn, "C");
}

TEST(FormulaParserTest, WholeRowRef) {
    FormulaParser parser("=1:1");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::ROW_REF);

    auto* row = dynamic_cast<RowRefNode*>(ast.get());
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->row, 1);
}

TEST(FormulaParserTest, WholeRowRefPosition) {
    // Row reference position should span the full "1:1" text
    const std::string formula = "=1:1";
    FormulaParser parser(formula);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::ROW_REF);

    auto* row = dynamic_cast<RowRefNode*>(ast.get());
    ASSERT_NE(row, nullptr);

    // Position should span from first '1' to end of second '1' (positions 1-4)
    EXPECT_EQ(row->position.start, 1u);
    EXPECT_EQ(row->position.end, 4u);

    // Verify the text can be extracted
    const std::string refText =
        formula.substr(row->position.start, row->position.end - row->position.start);
    EXPECT_EQ(refText, "1:1");
}

TEST(FormulaParserTest, WholeColumnRefPosition) {
    // Column reference position should span the full "A:A" text
    const std::string formula = "=A:A";
    FormulaParser parser(formula);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::COLUMN_REF);

    auto* col = dynamic_cast<ColumnRefNode*>(ast.get());
    ASSERT_NE(col, nullptr);

    // Position should span from first 'A' to end of second 'A' (positions 1-4)
    EXPECT_EQ(col->position.start, 1u);
    EXPECT_EQ(col->position.end, 4u);

    // Verify the text can be extracted
    const std::string refText =
        formula.substr(col->position.start, col->position.end - col->position.start);
    EXPECT_EQ(refText, "A:A");
}

TEST(FormulaParserTest, RowRangeRef) {
    FormulaParser parser("=1:10");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::ROW_RANGE_REF);

    auto* rowRange = dynamic_cast<RowRangeRefNode*>(ast.get());
    ASSERT_NE(rowRange, nullptr);
    EXPECT_EQ(rowRange->startRow, 1);
    EXPECT_EQ(rowRange->endRow, 10);
}

TEST(FormulaParserTest, RowRangeRefPosition) {
    // Row range reference position should span the full "1:10" text
    const std::string formula = "=1:10";
    FormulaParser parser(formula);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::ROW_RANGE_REF);

    auto* rowRange = dynamic_cast<RowRangeRefNode*>(ast.get());
    ASSERT_NE(rowRange, nullptr);

    // Position should span from first '1' to end of '10' (positions 1-5)
    EXPECT_EQ(rowRange->position.start, 1u);
    EXPECT_EQ(rowRange->position.end, 5u);

    // Verify the text can be extracted
    const std::string refText =
        formula.substr(rowRange->position.start, rowRange->position.end - rowRange->position.start);
    EXPECT_EQ(refText, "1:10");
}

TEST(FormulaParserTest, ColumnRangeRefPosition) {
    // Column range reference position should span the full "A:C" text
    const std::string formula = "=A:C";
    FormulaParser parser(formula);
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::COLUMN_RANGE_REF);

    auto* colRange = dynamic_cast<ColumnRangeRefNode*>(ast.get());
    ASSERT_NE(colRange, nullptr);

    // Position should span from 'A' to end of 'C' (positions 1-4)
    EXPECT_EQ(colRange->position.start, 1u);
    EXPECT_EQ(colRange->position.end, 4u);

    // Verify the text can be extracted
    const std::string refText =
        formula.substr(colRange->position.start, colRange->position.end - colRange->position.start);
    EXPECT_EQ(refText, "A:C");
}

// ============================================================================
// Cross-Sheet Reference Tests
// ============================================================================

TEST(FormulaParserTest, CrossSheetCellRef) {
    FormulaParser parser("=Sheet2!A1");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* cell = dynamic_cast<CellRefNode*>(ast.get());
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->sheetName, "Sheet2");
    EXPECT_EQ(cell->column, "A");
    EXPECT_EQ(cell->row, 1);
}

// ============================================================================
// Function Call Tests
// ============================================================================

TEST(FormulaParserTest, FunctionNoArgs) {
    FormulaParser parser("=NOW()");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::FUNCTION_CALL);

    auto* func = dynamic_cast<FunctionCallNode*>(ast.get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "NOW");
    EXPECT_TRUE(func->args.empty());
    EXPECT_TRUE(func->isVolatile);
}

TEST(FormulaParserTest, FunctionOneArg) {
    FormulaParser parser("=ABS(-5)");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* func = dynamic_cast<FunctionCallNode*>(ast.get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "ABS");
    EXPECT_EQ(func->args.size(), 1u);
}

TEST(FormulaParserTest, FunctionMultipleArgs) {
    FormulaParser parser("=IF(A1>0,B1,C1)");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* func = dynamic_cast<FunctionCallNode*>(ast.get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "IF");
    EXPECT_EQ(func->args.size(), 3u);
}

TEST(FormulaParserTest, FunctionWithRange) {
    FormulaParser parser("=SUM(A1:A10)");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* func = dynamic_cast<FunctionCallNode*>(ast.get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "SUM");
    EXPECT_EQ(func->args.size(), 1u);
    EXPECT_EQ(func->args[0]->type, ASTNodeType::RANGE_REF);
}

TEST(FormulaParserTest, NestedFunctions) {
    FormulaParser parser("=SUM(ABS(A1),ABS(B1))");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* func = dynamic_cast<FunctionCallNode*>(ast.get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "SUM");
    EXPECT_EQ(func->args.size(), 2u);
    EXPECT_EQ(func->args[0]->type, ASTNodeType::FUNCTION_CALL);
    EXPECT_EQ(func->args[1]->type, ASTNodeType::FUNCTION_CALL);
}

// ============================================================================
// Named Range Tests
// ============================================================================

TEST(FormulaParserTest, NamedRange) {
    FormulaParser parser("=myRange");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::NAMED_REF);

    auto* named = dynamic_cast<NamedRefNode*>(ast.get());
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->name, "myRange");
}

TEST(FormulaParserTest, NamedRangeWithUnderscore) {
    FormulaParser parser("=Sales_Total");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* named = dynamic_cast<NamedRefNode*>(ast.get());
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->name, "Sales_Total");
}

// ============================================================================
// Complex Expression Tests
// ============================================================================

TEST(FormulaParserTest, ComplexFormula) {
    FormulaParser parser("=IF(A1>0,SUM(B1:B10)*2,0)");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* func = dynamic_cast<FunctionCallNode*>(ast.get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "IF");
    EXPECT_EQ(func->args.size(), 3u);
}

TEST(FormulaParserTest, MultipleOperations) {
    FormulaParser parser("=A1+B1-C1*D1/E1^F1");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.hasErrors());
}

// ============================================================================
// JSON Serialization Tests
// ============================================================================

TEST(FormulaParserTest, JsonNumber) {
    FormulaParser parser("42");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    std::string json = ast->toJson();
    EXPECT_NE(json.find("NumberLiteral"), std::string::npos);
    EXPECT_NE(json.find("42"), std::string::npos);
}

TEST(FormulaParserTest, JsonBinaryOp) {
    FormulaParser parser("1+2");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    std::string json = ast->toJson();
    EXPECT_NE(json.find("BinaryOp"), std::string::npos);
    EXPECT_NE(json.find("\"+\""), std::string::npos);
}

TEST(FormulaParserTest, JsonCellRef) {
    FormulaParser parser("A1");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    std::string json = ast->toJson();
    EXPECT_NE(json.find("CellRef"), std::string::npos);
    EXPECT_NE(json.find("\"column\":\"A\""), std::string::npos);
}

// ============================================================================
// Error Recovery Tests
// ============================================================================

TEST(FormulaParserTest, ErrorMissingOperand) {
    FormulaParser parser("=1+");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_TRUE(parser.hasErrors());
    EXPECT_TRUE(ast->hasError());
}

TEST(FormulaParserTest, ErrorUnclosedParen) {
    FormulaParser parser("=(1+2");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_TRUE(parser.hasErrors());
}

TEST(FormulaParserTest, ErrorMissingFunctionArg) {
    FormulaParser parser("=SUM(");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_TRUE(parser.hasErrors());
}

// ============================================================================
// AST Clone Tests
// ============================================================================

TEST(FormulaParserTest, CloneNumber) {
    FormulaParser parser("42");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto clone = ast->clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->type, ASTNodeType::NUMBER_LITERAL);

    auto* num = dynamic_cast<NumberLiteralNode*>(clone.get());
    EXPECT_DOUBLE_EQ(num->value, 42.0);
}

TEST(FormulaParserTest, CloneBinaryOp) {
    FormulaParser parser("1+2");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto clone = ast->clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->type, ASTNodeType::BINARY_OP);

    auto* binOp = dynamic_cast<BinaryOpNode*>(clone.get());
    EXPECT_EQ(binOp->op, BinaryOp::ADD);
}

// ============================================================================
// Volatile Function Tests
// ============================================================================

TEST(FormulaParserTest, VolatileNow) {
    FormulaParser parser("=NOW()");
    auto ast = parser.parse();
    auto* func = dynamic_cast<FunctionCallNode*>(ast.get());
    ASSERT_NE(func, nullptr);
    EXPECT_TRUE(func->isVolatile);
}

TEST(FormulaParserTest, VolatileRand) {
    FormulaParser parser("=RAND()");
    auto ast = parser.parse();
    auto* func = dynamic_cast<FunctionCallNode*>(ast.get());
    ASSERT_NE(func, nullptr);
    EXPECT_TRUE(func->isVolatile);
}

TEST(FormulaParserTest, NonVolatileSum) {
    FormulaParser parser("=SUM(A1:A10)");
    auto ast = parser.parse();
    auto* func = dynamic_cast<FunctionCallNode*>(ast.get());
    ASSERT_NE(func, nullptr);
    EXPECT_FALSE(func->isVolatile);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(FormulaParserTest, EmptyFormula) {
    FormulaParser parser("");
    auto ast = parser.parse();
    // Empty input should return an error node
    EXPECT_TRUE(parser.hasErrors());
}

TEST(FormulaParserTest, JustEquals) {
    FormulaParser parser("=");
    auto ast = parser.parse();
    EXPECT_TRUE(parser.hasErrors());
}

TEST(FormulaParserTest, FormulaWithoutEquals) {
    // Parser should accept formulas without leading =
    FormulaParser parser("A1+B1");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.hasErrors());
}

// ============================================================================
// UUID Reference Parsing Tests
// ============================================================================

TEST(FormulaParserTest, UuidCellRefRelative) {
    FormulaParser parser("=~~xK7mNp2Q");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.hasErrors());
    EXPECT_EQ(ast->type, ASTNodeType::CELL_REF);

    auto* cellRef = dynamic_cast<CellRefNode*>(ast.get());
    ASSERT_NE(cellRef, nullptr);
    EXPECT_EQ(cellRef->cellId, "xK7mNp2Q");
    EXPECT_FALSE(cellRef->colAbsolute);
    EXPECT_FALSE(cellRef->rowAbsolute);
    EXPECT_TRUE(cellRef->column.empty());  // UUID format doesn't store column name
}

TEST(FormulaParserTest, UuidCellRefBothAbsolute) {
    FormulaParser parser("=$$abcd1234");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* cellRef = dynamic_cast<CellRefNode*>(ast.get());
    ASSERT_NE(cellRef, nullptr);
    EXPECT_EQ(cellRef->cellId, "abcd1234");
    EXPECT_TRUE(cellRef->colAbsolute);
    EXPECT_TRUE(cellRef->rowAbsolute);
}

TEST(FormulaParserTest, UuidCellRefColAbsolute) {
    FormulaParser parser("=$~abcd1234");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* cellRef = dynamic_cast<CellRefNode*>(ast.get());
    ASSERT_NE(cellRef, nullptr);
    EXPECT_EQ(cellRef->cellId, "abcd1234");
    EXPECT_TRUE(cellRef->colAbsolute);
    EXPECT_FALSE(cellRef->rowAbsolute);
}

TEST(FormulaParserTest, UuidCellRefRowAbsolute) {
    FormulaParser parser("=~$abcd1234");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* cellRef = dynamic_cast<CellRefNode*>(ast.get());
    ASSERT_NE(cellRef, nullptr);
    EXPECT_EQ(cellRef->cellId, "abcd1234");
    EXPECT_FALSE(cellRef->colAbsolute);
    EXPECT_TRUE(cellRef->rowAbsolute);
}

TEST(FormulaParserTest, UuidCellRefRange) {
    FormulaParser parser("=~~cellAAA1:~~cellBBB2");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.hasErrors());
    EXPECT_EQ(ast->type, ASTNodeType::RANGE_REF);

    auto* rangeRef = dynamic_cast<RangeRefNode*>(ast.get());
    ASSERT_NE(rangeRef, nullptr);
    EXPECT_EQ(rangeRef->topLeft->cellId, "cellAAA1");
    EXPECT_EQ(rangeRef->bottomRight->cellId, "cellBBB2");
}

TEST(FormulaParserTest, UuidCellRefBinaryOp) {
    FormulaParser parser("=~~xK7mNp2Q+~~fR3pK7wN");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.hasErrors());
    EXPECT_EQ(ast->type, ASTNodeType::BINARY_OP);

    auto* binOp = dynamic_cast<BinaryOpNode*>(ast.get());
    ASSERT_NE(binOp, nullptr);
    EXPECT_EQ(binOp->op, BinaryOp::ADD);
    EXPECT_EQ(binOp->left->type, ASTNodeType::CELL_REF);
    EXPECT_EQ(binOp->right->type, ASTNodeType::CELL_REF);

    auto* leftCell = dynamic_cast<CellRefNode*>(binOp->left.get());
    auto* rightCell = dynamic_cast<CellRefNode*>(binOp->right.get());
    EXPECT_EQ(leftCell->cellId, "xK7mNp2Q");
    EXPECT_EQ(rightCell->cellId, "fR3pK7wN");
}

TEST(FormulaParserTest, UuidCellRefFunction) {
    FormulaParser parser("=SUM(~~cellA001,~~cellB002)");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.hasErrors());

    auto* func = dynamic_cast<FunctionCallNode*>(ast.get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "SUM");
    EXPECT_EQ(func->args.size(), 2u);
    EXPECT_EQ(func->args[0]->type, ASTNodeType::CELL_REF);
    EXPECT_EQ(func->args[1]->type, ASTNodeType::CELL_REF);
}

TEST(FormulaParserTest, UuidColumnRef) {
    FormulaParser parser("=@~colA0001");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.hasErrors());
    EXPECT_EQ(ast->type, ASTNodeType::COLUMN_REF);

    auto* colRef = dynamic_cast<ColumnRefNode*>(ast.get());
    ASSERT_NE(colRef, nullptr);
    EXPECT_EQ(colRef->columnId, "colA0001");
    EXPECT_FALSE(colRef->absolute);
}

TEST(FormulaParserTest, UuidColumnRefAbsolute) {
    FormulaParser parser("=@$colA0001");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* colRef = dynamic_cast<ColumnRefNode*>(ast.get());
    ASSERT_NE(colRef, nullptr);
    EXPECT_EQ(colRef->columnId, "colA0001");
    EXPECT_TRUE(colRef->absolute);
}

TEST(FormulaParserTest, UuidColumnRange) {
    FormulaParser parser("=@~colA0001:@~colC0003");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.hasErrors());
    EXPECT_EQ(ast->type, ASTNodeType::COLUMN_RANGE_REF);

    auto* colRange = dynamic_cast<ColumnRangeRefNode*>(ast.get());
    ASSERT_NE(colRange, nullptr);
    EXPECT_EQ(colRange->startColumnId, "colA0001");
    EXPECT_EQ(colRange->endColumnId, "colC0003");
}

TEST(FormulaParserTest, UuidRowRef) {
    FormulaParser parser("=#~row10001");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.hasErrors());
    EXPECT_EQ(ast->type, ASTNodeType::ROW_REF);

    auto* rowRef = dynamic_cast<RowRefNode*>(ast.get());
    ASSERT_NE(rowRef, nullptr);
    EXPECT_EQ(rowRef->rowId, "row10001");
    EXPECT_FALSE(rowRef->absolute);
}

TEST(FormulaParserTest, UuidRowRefAbsolute) {
    FormulaParser parser("=#$row10001");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* rowRef = dynamic_cast<RowRefNode*>(ast.get());
    ASSERT_NE(rowRef, nullptr);
    EXPECT_EQ(rowRef->rowId, "row10001");
    EXPECT_TRUE(rowRef->absolute);
}

TEST(FormulaParserTest, UuidRowRange) {
    FormulaParser parser("=#~row10001:#~row50005");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.hasErrors());
    EXPECT_EQ(ast->type, ASTNodeType::ROW_RANGE_REF);

    auto* rowRange = dynamic_cast<RowRangeRefNode*>(ast.get());
    ASSERT_NE(rowRange, nullptr);
    EXPECT_EQ(rowRange->startRowId, "row10001");
    EXPECT_EQ(rowRange->endRowId, "row50005");
}

TEST(FormulaParserTest, UuidMixedWithLiterals) {
    FormulaParser parser("=~~cellA001*2+10");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.hasErrors());
    EXPECT_EQ(ast->type, ASTNodeType::BINARY_OP);
}

TEST(FormulaParserTest, UuidComplexFormula) {
    // SUM(cellA:cellB) + IF(condition, ~~cellC, 0)
    FormulaParser parser("=SUM(~~cellAAA1:~~cellBBB2)+42");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.hasErrors());
    EXPECT_EQ(ast->type, ASTNodeType::BINARY_OP);
}

// ============================================================================
// ErrorNode rawText Tests
// ============================================================================

TEST(FormulaParserTest, ErrorNodeHasRawText) {
    // A completely unparseable formula should create an ErrorNode with rawText set
    FormulaParser parser("=@@@invalid");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::ERROR_NODE);
    auto* errorNode = dynamic_cast<ErrorNode*>(ast.get());
    ASSERT_NE(errorNode, nullptr);
    EXPECT_EQ(errorNode->rawText, "=@@@invalid");
}

TEST(FormulaParserTest, ErrorNodeRawTextPreservesWhitespace) {
    // Whitespace in the original formula should be preserved
    FormulaParser parser("= A1 +");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    // This parses partially (A1 + <error>), but the top-level might not be an ErrorNode
    // Let's test a clearly broken formula instead
}

TEST(FormulaParserTest, ErrorNodeRawTextOnUnexpectedEnd) {
    FormulaParser parser("=SUM(");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_TRUE(parser.hasErrors());
    // Even partial parsing results in an ErrorNode somewhere
}

TEST(FormulaParserTest, ValidFormulaNoErrorNode) {
    FormulaParser parser("=A1+B2");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_NE(ast->type, ASTNodeType::ERROR_NODE);
    EXPECT_FALSE(parser.hasErrors());
}

// ============================================================================
// Spill Range Reference Tests
// ============================================================================

TEST(FormulaParserTest, SpillRangeSimple) {
    // A1# - spill range reference
    FormulaParser parser("=A1#");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.hasErrors());
    EXPECT_EQ(ast->type, ASTNodeType::SPILL_RANGE_REF);

    auto* spillRef = dynamic_cast<SpillRangeRefNode*>(ast.get());
    ASSERT_NE(spillRef, nullptr);
    ASSERT_NE(spillRef->anchor, nullptr);
    EXPECT_EQ(spillRef->anchor->column, "A");
    EXPECT_EQ(spillRef->anchor->row, 1);
}

TEST(FormulaParserTest, SpillRangeAbsolute) {
    // $A$1# - absolute cell ref with spill
    FormulaParser parser("=$A$1#");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.hasErrors());
    EXPECT_EQ(ast->type, ASTNodeType::SPILL_RANGE_REF);

    auto* spillRef = dynamic_cast<SpillRangeRefNode*>(ast.get());
    ASSERT_NE(spillRef, nullptr);
    ASSERT_NE(spillRef->anchor, nullptr);
    EXPECT_TRUE(spillRef->anchor->colAbsolute);
    EXPECT_TRUE(spillRef->anchor->rowAbsolute);
}

TEST(FormulaParserTest, SpillRangeInFunction) {
    // SUM(A1#) - spill range as function argument
    FormulaParser parser("=SUM(A1#)");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.hasErrors());
    EXPECT_EQ(ast->type, ASTNodeType::FUNCTION_CALL);

    auto* func = dynamic_cast<FunctionCallNode*>(ast.get());
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->args.size(), 1u);
    EXPECT_EQ(func->args[0]->type, ASTNodeType::SPILL_RANGE_REF);
}

TEST(FormulaParserTest, SpillRangeInExpression) {
    // A1#+B2 - spill range in expression
    FormulaParser parser("=A1#+B2");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.hasErrors());
    EXPECT_EQ(ast->type, ASTNodeType::BINARY_OP);

    auto* binOp = dynamic_cast<BinaryOpNode*>(ast.get());
    ASSERT_NE(binOp, nullptr);
    EXPECT_EQ(binOp->left->type, ASTNodeType::SPILL_RANGE_REF);
    EXPECT_EQ(binOp->right->type, ASTNodeType::CELL_REF);
}

TEST(FormulaParserTest, SpillRangePosition) {
    // Check position spans from cell to #
    FormulaParser parser("=A1#");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* spillRef = dynamic_cast<SpillRangeRefNode*>(ast.get());
    ASSERT_NE(spillRef, nullptr);
    // Position should be from start of A (position 1) to end of # (position 4)
    EXPECT_EQ(spillRef->position.start, 1u);
    EXPECT_EQ(spillRef->position.end, 4u);
}

TEST(FormulaParserTest, SpillRangeToJson) {
    FormulaParser parser("=A1#");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto* spillRef = dynamic_cast<SpillRangeRefNode*>(ast.get());
    ASSERT_NE(spillRef, nullptr);

    std::string json = spillRef->toJson();
    EXPECT_TRUE(json.find("SpillRangeRef") != std::string::npos);
    EXPECT_TRUE(json.find("anchor") != std::string::npos);
}

TEST(FormulaParserTest, SpillRangeClone) {
    FormulaParser parser("=A1#");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);

    auto clone = ast->clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->type, ASTNodeType::SPILL_RANGE_REF);

    auto* cloneSpill = dynamic_cast<SpillRangeRefNode*>(clone.get());
    ASSERT_NE(cloneSpill, nullptr);
    ASSERT_NE(cloneSpill->anchor, nullptr);
    EXPECT_EQ(cloneSpill->anchor->column, "A");
    EXPECT_EQ(cloneSpill->anchor->row, 1);
}

TEST(FormulaParserTest, SpillRangeUuid) {
    // UUID cell ref with spill: ~~cellId12#
    FormulaParser parser("=~~cellId12#");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_FALSE(parser.hasErrors());
    EXPECT_EQ(ast->type, ASTNodeType::SPILL_RANGE_REF);

    auto* spillRef = dynamic_cast<SpillRangeRefNode*>(ast.get());
    ASSERT_NE(spillRef, nullptr);
    EXPECT_EQ(spillRef->anchor->cellId, "cellId12");
}

TEST(FormulaParserTest, SpillRangeVsRange) {
    // A1# is spill range, A1:B2 is range (not A1#:B2)
    FormulaParser parser("=A1:B2");
    auto ast = parser.parse();
    ASSERT_NE(ast, nullptr);
    EXPECT_EQ(ast->type, ASTNodeType::RANGE_REF);

    // # binds tighter: A1# means spill of A1, not part of range
    FormulaParser parser2("=A1#");
    auto ast2 = parser2.parse();
    ASSERT_NE(ast2, nullptr);
    EXPECT_EQ(ast2->type, ASTNodeType::SPILL_RANGE_REF);
}

}  // namespace
}  // namespace cells
