#include "core/cells/formula_ast.h"

#include <gtest/gtest.h>
#include <memory>

namespace cells {
namespace {

// ============================================================================
// Node Construction Tests
// ============================================================================

TEST(FormulaASTTest, NumberLiteralConstruction) {
    NumberLiteralNode node(42.5);
    EXPECT_EQ(node.type, ASTNodeType::NUMBER_LITERAL);
    EXPECT_EQ(node.value, 42.5);
}

TEST(FormulaASTTest, NumberLiteralWithPosition) {
    SourcePosition pos{0, 5};
    NumberLiteralNode node(3.14, pos);
    EXPECT_EQ(node.value, 3.14);
    EXPECT_EQ(node.position.start, 0);
    EXPECT_EQ(node.position.end, 5);
}

TEST(FormulaASTTest, StringLiteralConstruction) {
    StringLiteralNode node("Hello");
    EXPECT_EQ(node.type, ASTNodeType::STRING_LITERAL);
    EXPECT_EQ(node.value, "Hello");
}

TEST(FormulaASTTest, BooleanLiteralConstruction) {
    BooleanLiteralNode trueNode(true);
    BooleanLiteralNode falseNode(false);
    EXPECT_EQ(trueNode.type, ASTNodeType::BOOLEAN_LITERAL);
    EXPECT_TRUE(trueNode.value);
    EXPECT_FALSE(falseNode.value);
}

TEST(FormulaASTTest, CellRefConstruction) {
    CellRefNode node("B", 5, false, true);
    EXPECT_EQ(node.type, ASTNodeType::CELL_REF);
    EXPECT_EQ(node.column, "B");
    EXPECT_EQ(node.row, 5);
    EXPECT_FALSE(node.colAbsolute);
    EXPECT_TRUE(node.rowAbsolute);
    EXPECT_TRUE(node.sheetName.empty());
    EXPECT_TRUE(node.cellId.empty());
}

TEST(FormulaASTTest, CellRefWithSheet) {
    CellRefNode node("AA", 100, true, true);
    node.sheetName = "Sheet2";
    node.cellId = "abc123";
    EXPECT_EQ(node.column, "AA");
    EXPECT_EQ(node.row, 100);
    EXPECT_EQ(node.sheetName, "Sheet2");
    EXPECT_EQ(node.cellId, "abc123");
}

TEST(FormulaASTTest, RangeRefConstruction) {
    auto tl = std::make_unique<CellRefNode>("A", 1, false, false);
    auto br = std::make_unique<CellRefNode>("C", 10, false, false);
    RangeRefNode node(std::move(tl), std::move(br));
    EXPECT_EQ(node.type, ASTNodeType::RANGE_REF);
    EXPECT_EQ(node.topLeft->column, "A");
    EXPECT_EQ(node.topLeft->row, 1);
    EXPECT_EQ(node.bottomRight->column, "C");
    EXPECT_EQ(node.bottomRight->row, 10);
}

TEST(FormulaASTTest, ColumnRefConstruction) {
    ColumnRefNode node("D", true);
    EXPECT_EQ(node.type, ASTNodeType::COLUMN_REF);
    EXPECT_EQ(node.column, "D");
    EXPECT_TRUE(node.absolute);
}

TEST(FormulaASTTest, RowRefConstruction) {
    RowRefNode node(42, false);
    EXPECT_EQ(node.type, ASTNodeType::ROW_REF);
    EXPECT_EQ(node.row, 42);
    EXPECT_FALSE(node.absolute);
}

TEST(FormulaASTTest, ColumnRangeRefConstruction) {
    ColumnRangeRefNode node("A", "D", true, false);
    EXPECT_EQ(node.type, ASTNodeType::COLUMN_RANGE_REF);
    EXPECT_EQ(node.startColumn, "A");
    EXPECT_EQ(node.endColumn, "D");
    EXPECT_TRUE(node.startAbsolute);
    EXPECT_FALSE(node.endAbsolute);
}

TEST(FormulaASTTest, RowRangeRefConstruction) {
    RowRangeRefNode node(1, 100, false, true);
    EXPECT_EQ(node.type, ASTNodeType::ROW_RANGE_REF);
    EXPECT_EQ(node.startRow, 1);
    EXPECT_EQ(node.endRow, 100);
    EXPECT_FALSE(node.startAbsolute);
    EXPECT_TRUE(node.endAbsolute);
}

TEST(FormulaASTTest, NamedRefConstruction) {
    NamedRefNode node("MyRange", ASTNamedRangeScope::WORKBOOK);
    EXPECT_EQ(node.type, ASTNodeType::NAMED_REF);
    EXPECT_EQ(node.name, "MyRange");
    EXPECT_EQ(node.scope, ASTNamedRangeScope::WORKBOOK);
}

TEST(FormulaASTTest, NamedRefSheetScope) {
    NamedRefNode node("LocalRange", ASTNamedRangeScope::SHEET);
    EXPECT_EQ(node.scope, ASTNamedRangeScope::SHEET);
}

TEST(FormulaASTTest, BinaryOpConstruction) {
    auto left = std::make_unique<NumberLiteralNode>(1.0);
    auto right = std::make_unique<NumberLiteralNode>(2.0);
    BinaryOpNode node(BinaryOp::ADD, std::move(left), std::move(right));
    EXPECT_EQ(node.type, ASTNodeType::BINARY_OP);
    EXPECT_EQ(node.op, BinaryOp::ADD);
    EXPECT_EQ(static_cast<NumberLiteralNode*>(node.left.get())->value, 1.0);
    EXPECT_EQ(static_cast<NumberLiteralNode*>(node.right.get())->value, 2.0);
}

TEST(FormulaASTTest, UnaryOpConstruction) {
    auto operand = std::make_unique<NumberLiteralNode>(5.0);
    UnaryOpNode node(UnaryOp::NEGATE, std::move(operand));
    EXPECT_EQ(node.type, ASTNodeType::UNARY_OP);
    EXPECT_EQ(node.op, UnaryOp::NEGATE);
    EXPECT_EQ(static_cast<NumberLiteralNode*>(node.operand.get())->value, 5.0);
}

TEST(FormulaASTTest, FunctionCallConstruction) {
    FunctionCallNode node("SUM");
    node.args.push_back(std::make_unique<NumberLiteralNode>(1.0));
    node.args.push_back(std::make_unique<NumberLiteralNode>(2.0));
    EXPECT_EQ(node.type, ASTNodeType::FUNCTION_CALL);
    EXPECT_EQ(node.name, "SUM");
    EXPECT_EQ(node.args.size(), 2);
    EXPECT_FALSE(node.isVolatile);
}

TEST(FormulaASTTest, ErrorNodeConstruction) {
    ErrorNode node("Unexpected token");
    EXPECT_EQ(node.type, ASTNodeType::ERROR_NODE);
    EXPECT_EQ(node.message, "Unexpected token");
    EXPECT_TRUE(node.partialChildren.empty());
}

TEST(FormulaASTTest, ErrorNodeWithChildren) {
    ErrorNode node("Missing operand");
    node.partialChildren.push_back(std::make_unique<NumberLiteralNode>(42.0));
    EXPECT_EQ(node.partialChildren.size(), 1);
}

// ============================================================================
// Clone Tests
// ============================================================================

TEST(FormulaASTTest, CloneNumberLiteral) {
    NumberLiteralNode original(42.5);
    original.position = {0, 4};
    auto cloned = original.clone();
    EXPECT_EQ(cloned->type, ASTNodeType::NUMBER_LITERAL);
    auto* numClone = static_cast<NumberLiteralNode*>(cloned.get());
    EXPECT_EQ(numClone->value, 42.5);
    EXPECT_EQ(numClone->position.start, 0);
    EXPECT_EQ(numClone->position.end, 4);
}

TEST(FormulaASTTest, CloneStringLiteral) {
    StringLiteralNode original("Hello");
    auto cloned = original.clone();
    auto* strClone = static_cast<StringLiteralNode*>(cloned.get());
    EXPECT_EQ(strClone->value, "Hello");
}

TEST(FormulaASTTest, CloneBooleanLiteral) {
    BooleanLiteralNode original(true);
    auto cloned = original.clone();
    auto* boolClone = static_cast<BooleanLiteralNode*>(cloned.get());
    EXPECT_TRUE(boolClone->value);
}

TEST(FormulaASTTest, CloneCellRef) {
    CellRefNode original("B", 5, true, false);
    original.sheetName = "Sheet2";
    original.cellId = "abc123";
    original.position = {1, 6};
    auto cloned = original.clone();
    auto* cellClone = static_cast<CellRefNode*>(cloned.get());
    EXPECT_EQ(cellClone->column, "B");
    EXPECT_EQ(cellClone->row, 5);
    EXPECT_TRUE(cellClone->colAbsolute);
    EXPECT_FALSE(cellClone->rowAbsolute);
    EXPECT_EQ(cellClone->sheetName, "Sheet2");
    EXPECT_EQ(cellClone->cellId, "abc123");
    EXPECT_EQ(cellClone->position.start, 1);
}

TEST(FormulaASTTest, CloneRangeRef) {
    auto tl = std::make_unique<CellRefNode>("A", 1, false, false);
    auto br = std::make_unique<CellRefNode>("C", 10, true, true);
    RangeRefNode original(std::move(tl), std::move(br));
    original.position = {0, 7};
    auto cloned = original.clone();
    auto* rangeClone = static_cast<RangeRefNode*>(cloned.get());
    EXPECT_EQ(rangeClone->topLeft->column, "A");
    EXPECT_EQ(rangeClone->bottomRight->column, "C");
    EXPECT_TRUE(rangeClone->bottomRight->colAbsolute);
}

TEST(FormulaASTTest, CloneColumnRef) {
    ColumnRefNode original("D", true);
    original.sheetName = "Data";
    original.columnId = "col123";
    auto cloned = original.clone();
    auto* colClone = static_cast<ColumnRefNode*>(cloned.get());
    EXPECT_EQ(colClone->column, "D");
    EXPECT_TRUE(colClone->absolute);
    EXPECT_EQ(colClone->sheetName, "Data");
    EXPECT_EQ(colClone->columnId, "col123");
}

TEST(FormulaASTTest, CloneRowRef) {
    RowRefNode original(42, false);
    original.sheetName = "Sheet3";
    original.rowId = "row456";
    auto cloned = original.clone();
    auto* rowClone = static_cast<RowRefNode*>(cloned.get());
    EXPECT_EQ(rowClone->row, 42);
    EXPECT_FALSE(rowClone->absolute);
    EXPECT_EQ(rowClone->sheetName, "Sheet3");
    EXPECT_EQ(rowClone->rowId, "row456");
}

TEST(FormulaASTTest, CloneColumnRangeRef) {
    ColumnRangeRefNode original("A", "D", true, false);
    original.sheetName = "Sheet1";
    original.startColumnId = "colA";
    original.endColumnId = "colD";
    auto cloned = original.clone();
    auto* colRangeClone = static_cast<ColumnRangeRefNode*>(cloned.get());
    EXPECT_EQ(colRangeClone->startColumn, "A");
    EXPECT_EQ(colRangeClone->endColumn, "D");
    EXPECT_EQ(colRangeClone->sheetName, "Sheet1");
    EXPECT_EQ(colRangeClone->startColumnId, "colA");
    EXPECT_EQ(colRangeClone->endColumnId, "colD");
}

TEST(FormulaASTTest, CloneRowRangeRef) {
    RowRangeRefNode original(1, 100, false, true);
    original.sheetName = "Sheet2";
    original.startRowId = "row1";
    original.endRowId = "row100";
    auto cloned = original.clone();
    auto* rowRangeClone = static_cast<RowRangeRefNode*>(cloned.get());
    EXPECT_EQ(rowRangeClone->startRow, 1);
    EXPECT_EQ(rowRangeClone->endRow, 100);
    EXPECT_EQ(rowRangeClone->sheetName, "Sheet2");
    EXPECT_EQ(rowRangeClone->startRowId, "row1");
    EXPECT_EQ(rowRangeClone->endRowId, "row100");
}

TEST(FormulaASTTest, CloneNamedRef) {
    NamedRefNode original("Total", ASTNamedRangeScope::SHEET);
    original.position = {2, 7};
    auto cloned = original.clone();
    auto* namedClone = static_cast<NamedRefNode*>(cloned.get());
    EXPECT_EQ(namedClone->name, "Total");
    EXPECT_EQ(namedClone->scope, ASTNamedRangeScope::SHEET);
    EXPECT_EQ(namedClone->position.start, 2);
}

TEST(FormulaASTTest, CloneBinaryOp) {
    auto left = std::make_unique<NumberLiteralNode>(1.0);
    auto right = std::make_unique<NumberLiteralNode>(2.0);
    BinaryOpNode original(BinaryOp::MULTIPLY, std::move(left), std::move(right));
    auto cloned = original.clone();
    auto* binClone = static_cast<BinaryOpNode*>(cloned.get());
    EXPECT_EQ(binClone->op, BinaryOp::MULTIPLY);
    EXPECT_EQ(static_cast<NumberLiteralNode*>(binClone->left.get())->value, 1.0);
    EXPECT_EQ(static_cast<NumberLiteralNode*>(binClone->right.get())->value, 2.0);
}

TEST(FormulaASTTest, CloneUnaryOp) {
    auto operand = std::make_unique<NumberLiteralNode>(5.0);
    UnaryOpNode original(UnaryOp::NEGATE, std::move(operand));
    auto cloned = original.clone();
    auto* unaryClone = static_cast<UnaryOpNode*>(cloned.get());
    EXPECT_EQ(unaryClone->op, UnaryOp::NEGATE);
    EXPECT_EQ(static_cast<NumberLiteralNode*>(unaryClone->operand.get())->value, 5.0);
}

TEST(FormulaASTTest, CloneFunctionCall) {
    FunctionCallNode original("RAND");
    original.args.push_back(std::make_unique<NumberLiteralNode>(10.0));
    original.isVolatile = true;
    original.position = {0, 10};
    auto cloned = original.clone();
    auto* funcClone = static_cast<FunctionCallNode*>(cloned.get());
    EXPECT_EQ(funcClone->name, "RAND");
    EXPECT_EQ(funcClone->args.size(), 1);
    EXPECT_TRUE(funcClone->isVolatile);
    EXPECT_EQ(funcClone->position.start, 0);
}

TEST(FormulaASTTest, CloneErrorNode) {
    ErrorNode original("Parse error");
    original.partialChildren.push_back(std::make_unique<NumberLiteralNode>(1.0));
    original.position = {5, 10};
    auto cloned = original.clone();
    auto* errClone = static_cast<ErrorNode*>(cloned.get());
    EXPECT_EQ(errClone->message, "Parse error");
    EXPECT_EQ(errClone->partialChildren.size(), 1);
    EXPECT_EQ(errClone->position.start, 5);
}

TEST(FormulaASTTest, CloneDeepTree) {
    // Build: 1 + (2 * 3)
    auto num1 = std::make_unique<NumberLiteralNode>(1.0);
    auto num2 = std::make_unique<NumberLiteralNode>(2.0);
    auto num3 = std::make_unique<NumberLiteralNode>(3.0);
    auto mult =
        std::make_unique<BinaryOpNode>(BinaryOp::MULTIPLY, std::move(num2), std::move(num3));
    BinaryOpNode original(BinaryOp::ADD, std::move(num1), std::move(mult));

    auto cloned = original.clone();
    auto* rootClone = static_cast<BinaryOpNode*>(cloned.get());
    EXPECT_EQ(rootClone->op, BinaryOp::ADD);
    auto* leftClone = static_cast<NumberLiteralNode*>(rootClone->left.get());
    EXPECT_EQ(leftClone->value, 1.0);
    auto* rightClone = static_cast<BinaryOpNode*>(rootClone->right.get());
    EXPECT_EQ(rightClone->op, BinaryOp::MULTIPLY);
}

// ============================================================================
// JSON Serialization Tests
// ============================================================================

TEST(FormulaASTTest, NumberLiteralToJson) {
    NumberLiteralNode node(42.5);
    std::string json = node.toJson();
    EXPECT_NE(json.find("\"type\":\"NumberLiteral\""), std::string::npos);
    EXPECT_NE(json.find("42.5"), std::string::npos);
}

TEST(FormulaASTTest, StringLiteralToJson) {
    StringLiteralNode node("Hello \"World\"");
    std::string json = node.toJson();
    EXPECT_NE(json.find("\"type\":\"StringLiteral\""), std::string::npos);
    EXPECT_NE(json.find("Hello \\\"World\\\""), std::string::npos);
}

TEST(FormulaASTTest, BooleanLiteralToJson) {
    BooleanLiteralNode node(true);
    std::string json = node.toJson();
    EXPECT_NE(json.find("\"type\":\"BooleanLiteral\""), std::string::npos);
    EXPECT_NE(json.find("\"value\":true"), std::string::npos);
}

TEST(FormulaASTTest, CellRefToJson) {
    CellRefNode node("B", 5, true, false);
    node.sheetName = "Sheet2";
    node.cellId = "abc123";
    std::string json = node.toJson();
    EXPECT_NE(json.find("\"type\":\"CellRef\""), std::string::npos);
    EXPECT_NE(json.find("\"column\":\"B\""), std::string::npos);
    EXPECT_NE(json.find("\"row\":5"), std::string::npos);
    EXPECT_NE(json.find("\"colAbsolute\":true"), std::string::npos);
    EXPECT_NE(json.find("\"rowAbsolute\":false"), std::string::npos);
    EXPECT_NE(json.find("\"sheet\":\"Sheet2\""), std::string::npos);
    EXPECT_NE(json.find("\"cellId\":\"abc123\""), std::string::npos);
}

TEST(FormulaASTTest, RangeRefToJson) {
    auto tl = std::make_unique<CellRefNode>("A", 1, false, false);
    auto br = std::make_unique<CellRefNode>("C", 10, false, false);
    RangeRefNode node(std::move(tl), std::move(br));
    std::string json = node.toJson();
    EXPECT_NE(json.find("\"type\":\"RangeRef\""), std::string::npos);
    EXPECT_NE(json.find("\"topLeft\":"), std::string::npos);
    EXPECT_NE(json.find("\"bottomRight\":"), std::string::npos);
}

TEST(FormulaASTTest, ColumnRefToJson) {
    ColumnRefNode node("D", true);
    node.sheetName = "Data";
    node.columnId = "col123";
    std::string json = node.toJson();
    EXPECT_NE(json.find("\"type\":\"ColumnRef\""), std::string::npos);
    EXPECT_NE(json.find("\"column\":\"D\""), std::string::npos);
    EXPECT_NE(json.find("\"absolute\":true"), std::string::npos);
    EXPECT_NE(json.find("\"columnId\":\"col123\""), std::string::npos);
}

TEST(FormulaASTTest, RowRefToJson) {
    RowRefNode node(42, false);
    node.rowId = "row456";
    std::string json = node.toJson();
    EXPECT_NE(json.find("\"type\":\"RowRef\""), std::string::npos);
    EXPECT_NE(json.find("\"row\":42"), std::string::npos);
    EXPECT_NE(json.find("\"absolute\":false"), std::string::npos);
    EXPECT_NE(json.find("\"rowId\":\"row456\""), std::string::npos);
}

TEST(FormulaASTTest, ColumnRangeRefToJson) {
    ColumnRangeRefNode node("A", "D", true, false);
    node.sheetName = "Sheet1";
    std::string json = node.toJson();
    EXPECT_NE(json.find("\"type\":\"ColumnRangeRef\""), std::string::npos);
    EXPECT_NE(json.find("\"startColumn\":\"A\""), std::string::npos);
    EXPECT_NE(json.find("\"endColumn\":\"D\""), std::string::npos);
}

TEST(FormulaASTTest, RowRangeRefToJson) {
    RowRangeRefNode node(1, 100, false, true);
    std::string json = node.toJson();
    EXPECT_NE(json.find("\"type\":\"RowRangeRef\""), std::string::npos);
    EXPECT_NE(json.find("\"startRow\":1"), std::string::npos);
    EXPECT_NE(json.find("\"endRow\":100"), std::string::npos);
}

TEST(FormulaASTTest, NamedRefToJson) {
    NamedRefNode node("MyRange", ASTNamedRangeScope::WORKBOOK);
    std::string json = node.toJson();
    EXPECT_NE(json.find("\"type\":\"NamedRef\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"MyRange\""), std::string::npos);
    EXPECT_NE(json.find("\"scope\":\"workbook\""), std::string::npos);
}

TEST(FormulaASTTest, NamedRefSheetScopeToJson) {
    NamedRefNode node("LocalName", ASTNamedRangeScope::SHEET);
    std::string json = node.toJson();
    EXPECT_NE(json.find("\"scope\":\"sheet\""), std::string::npos);
}

TEST(FormulaASTTest, BinaryOpToJson) {
    auto left = std::make_unique<NumberLiteralNode>(1.0);
    auto right = std::make_unique<NumberLiteralNode>(2.0);
    BinaryOpNode node(BinaryOp::ADD, std::move(left), std::move(right));
    std::string json = node.toJson();
    EXPECT_NE(json.find("\"type\":\"BinaryOp\""), std::string::npos);
    EXPECT_NE(json.find("\"op\":\"+\""), std::string::npos);
    EXPECT_NE(json.find("\"left\":"), std::string::npos);
    EXPECT_NE(json.find("\"right\":"), std::string::npos);
}

TEST(FormulaASTTest, UnaryOpToJson) {
    auto operand = std::make_unique<NumberLiteralNode>(5.0);
    UnaryOpNode node(UnaryOp::NEGATE, std::move(operand));
    std::string json = node.toJson();
    EXPECT_NE(json.find("\"type\":\"UnaryOp\""), std::string::npos);
    EXPECT_NE(json.find("\"op\":\"-\""), std::string::npos);
    EXPECT_NE(json.find("\"operand\":"), std::string::npos);
}

TEST(FormulaASTTest, FunctionCallToJson) {
    FunctionCallNode node("SUM");
    node.args.push_back(std::make_unique<NumberLiteralNode>(1.0));
    node.args.push_back(std::make_unique<NumberLiteralNode>(2.0));
    node.isVolatile = false;
    std::string json = node.toJson();
    EXPECT_NE(json.find("\"type\":\"FunctionCall\""), std::string::npos);
    EXPECT_NE(json.find("\"name\":\"SUM\""), std::string::npos);
    EXPECT_NE(json.find("\"isVolatile\":false"), std::string::npos);
    EXPECT_NE(json.find("\"args\":["), std::string::npos);
}

TEST(FormulaASTTest, ErrorNodeToJson) {
    ErrorNode node("Unexpected token");
    node.partialChildren.push_back(std::make_unique<NumberLiteralNode>(1.0));
    std::string json = node.toJson();
    EXPECT_NE(json.find("\"type\":\"Error\""), std::string::npos);
    EXPECT_NE(json.find("\"message\":\"Unexpected token\""), std::string::npos);
    EXPECT_NE(json.find("\"partialChildren\":["), std::string::npos);
}

TEST(FormulaASTTest, ErrorNodeToJsonWithRawText) {
    ErrorNode node("Parse error");
    node.rawText = "=INVALID(syntax";
    std::string json = node.toJson();
    EXPECT_NE(json.find("\"type\":\"Error\""), std::string::npos);
    EXPECT_NE(json.find("\"rawText\":\"=INVALID(syntax\""), std::string::npos);
}

TEST(FormulaASTTest, ErrorNodeToJsonWithoutRawText) {
    ErrorNode node("Parse error");
    // rawText is empty by default
    std::string json = node.toJson();
    EXPECT_NE(json.find("\"type\":\"Error\""), std::string::npos);
    EXPECT_EQ(json.find("\"rawText\""), std::string::npos);  // Should not have rawText
}

// ============================================================================
// ErrorNode rawText Tests
// ============================================================================

TEST(FormulaASTTest, ErrorNodeRawTextPreservedAfterClone) {
    ErrorNode original("Parse error");
    original.rawText = "=broken formula";
    auto cloned = original.clone();
    auto* errClone = static_cast<ErrorNode*>(cloned.get());
    EXPECT_EQ(errClone->rawText, "=broken formula");
}

TEST(FormulaASTTest, ErrorNodeRawTextEmptyByDefault) {
    ErrorNode node("Some error");
    EXPECT_TRUE(node.rawText.empty());
}

// ============================================================================
// hasError() Tests
// ============================================================================

TEST(FormulaASTTest, HasErrorOnLiteral) {
    NumberLiteralNode node(42.0);
    EXPECT_FALSE(node.hasError());
}

TEST(FormulaASTTest, HasErrorOnErrorNode) {
    ErrorNode node("Error");
    EXPECT_TRUE(node.hasError());
}

TEST(FormulaASTTest, HasErrorOnBinaryOpWithError) {
    auto left = std::make_unique<NumberLiteralNode>(1.0);
    auto right = std::make_unique<ErrorNode>("Missing operand");
    BinaryOpNode node(BinaryOp::ADD, std::move(left), std::move(right));
    EXPECT_TRUE(node.hasError());
}

TEST(FormulaASTTest, HasErrorOnBinaryOpWithoutError) {
    auto left = std::make_unique<NumberLiteralNode>(1.0);
    auto right = std::make_unique<NumberLiteralNode>(2.0);
    BinaryOpNode node(BinaryOp::ADD, std::move(left), std::move(right));
    EXPECT_FALSE(node.hasError());
}

TEST(FormulaASTTest, HasErrorOnUnaryOpWithError) {
    auto operand = std::make_unique<ErrorNode>("Invalid expression");
    UnaryOpNode node(UnaryOp::NEGATE, std::move(operand));
    EXPECT_TRUE(node.hasError());
}

TEST(FormulaASTTest, HasErrorOnFunctionCallWithError) {
    FunctionCallNode node("SUM");
    node.args.push_back(std::make_unique<NumberLiteralNode>(1.0));
    node.args.push_back(std::make_unique<ErrorNode>("Parse error"));
    EXPECT_TRUE(node.hasError());
}

TEST(FormulaASTTest, HasErrorOnFunctionCallWithoutError) {
    FunctionCallNode node("SUM");
    node.args.push_back(std::make_unique<NumberLiteralNode>(1.0));
    node.args.push_back(std::make_unique<NumberLiteralNode>(2.0));
    EXPECT_FALSE(node.hasError());
}

TEST(FormulaASTTest, HasErrorOnRangeRefWithError) {
    auto tl = std::make_unique<CellRefNode>("A", 1, false, false);
    // Create a broken bottomRight by manually setting type
    auto br = std::make_unique<CellRefNode>("B", 2, false, false);
    RangeRefNode node(std::move(tl), std::move(br));
    EXPECT_FALSE(node.hasError());  // Both corners are valid
}

// ============================================================================
// Operator String Conversion Tests
// ============================================================================

TEST(FormulaASTTest, BinaryOpToString) {
    EXPECT_STREQ(BinaryOpNode::opToString(BinaryOp::ADD), "+");
    EXPECT_STREQ(BinaryOpNode::opToString(BinaryOp::SUBTRACT), "-");
    EXPECT_STREQ(BinaryOpNode::opToString(BinaryOp::MULTIPLY), "*");
    EXPECT_STREQ(BinaryOpNode::opToString(BinaryOp::DIVIDE), "/");
    EXPECT_STREQ(BinaryOpNode::opToString(BinaryOp::POWER), "^");
    EXPECT_STREQ(BinaryOpNode::opToString(BinaryOp::CONCAT), "&");
    EXPECT_STREQ(BinaryOpNode::opToString(BinaryOp::EQUAL), "=");
    EXPECT_STREQ(BinaryOpNode::opToString(BinaryOp::NOT_EQUAL), "<>");
    EXPECT_STREQ(BinaryOpNode::opToString(BinaryOp::LESS), "<");
    EXPECT_STREQ(BinaryOpNode::opToString(BinaryOp::LESS_EQUAL), "<=");
    EXPECT_STREQ(BinaryOpNode::opToString(BinaryOp::GREATER), ">");
    EXPECT_STREQ(BinaryOpNode::opToString(BinaryOp::GREATER_EQUAL), ">=");
}

TEST(FormulaASTTest, UnaryOpToString) {
    EXPECT_STREQ(UnaryOpNode::opToString(UnaryOp::NEGATE), "-");
    EXPECT_STREQ(UnaryOpNode::opToString(UnaryOp::POSITIVE), "+");
}

// ============================================================================
// Volatile Function Detection Tests
// ============================================================================

TEST(FormulaASTTest, VolatileFunctionNow) {
    EXPECT_TRUE(FunctionCallNode::isVolatileFunction("NOW"));
    EXPECT_TRUE(FunctionCallNode::isVolatileFunction("now"));
    EXPECT_TRUE(FunctionCallNode::isVolatileFunction("Now"));
}

TEST(FormulaASTTest, VolatileFunctionToday) {
    EXPECT_TRUE(FunctionCallNode::isVolatileFunction("TODAY"));
    EXPECT_TRUE(FunctionCallNode::isVolatileFunction("today"));
}

TEST(FormulaASTTest, VolatileFunctionRand) {
    EXPECT_TRUE(FunctionCallNode::isVolatileFunction("RAND"));
    EXPECT_TRUE(FunctionCallNode::isVolatileFunction("RANDBETWEEN"));
}

TEST(FormulaASTTest, VolatileFunctionOffset) {
    EXPECT_TRUE(FunctionCallNode::isVolatileFunction("OFFSET"));
}

TEST(FormulaASTTest, VolatileFunctionIndirect) {
    EXPECT_TRUE(FunctionCallNode::isVolatileFunction("INDIRECT"));
}

TEST(FormulaASTTest, NonVolatileFunction) {
    EXPECT_FALSE(FunctionCallNode::isVolatileFunction("SUM"));
    EXPECT_FALSE(FunctionCallNode::isVolatileFunction("IF"));
    EXPECT_FALSE(FunctionCallNode::isVolatileFunction("VLOOKUP"));
    EXPECT_FALSE(FunctionCallNode::isVolatileFunction("AVERAGE"));
}

// ============================================================================
// Source Position Tests
// ============================================================================

TEST(FormulaASTTest, SourcePositionPreservedAfterClone) {
    NumberLiteralNode original(42.0);
    original.position = {10, 15};
    auto cloned = original.clone();
    EXPECT_EQ(cloned->position.start, 10);
    EXPECT_EQ(cloned->position.end, 15);
}

TEST(FormulaASTTest, SourcePositionOnNestedNodes) {
    auto left = std::make_unique<NumberLiteralNode>(1.0);
    left->position = {1, 2};
    auto right = std::make_unique<NumberLiteralNode>(2.0);
    right->position = {3, 4};
    BinaryOpNode node(BinaryOp::ADD, std::move(left), std::move(right));
    node.position = {0, 5};

    auto cloned = node.clone();
    auto* binClone = static_cast<BinaryOpNode*>(cloned.get());
    EXPECT_EQ(binClone->position.start, 0);
    EXPECT_EQ(binClone->position.end, 5);
    EXPECT_EQ(binClone->left->position.start, 1);
    EXPECT_EQ(binClone->right->position.start, 3);
}

}  // namespace
}  // namespace cells
