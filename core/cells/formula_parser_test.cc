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

}  // namespace
}  // namespace cells
