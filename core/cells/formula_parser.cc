#include "core/cells/formula_parser.h"

#include <cctype>

#include <algorithm>

namespace cells {

FormulaParser::FormulaParser(std::string_view source) : lexer_(source), source_(source) {
    advance();
}

std::unique_ptr<ASTNode> FormulaParser::parse() {
    auto result = formula();
    // If the result is an ErrorNode, populate rawText with the original formula
    if (result && result->type == ASTNodeType::ERROR_NODE) {
        auto* errorNodePtr = static_cast<ErrorNode*>(result.get());
        errorNodePtr->rawText = source_;
    }
    return result;
}

// ============================================================================
// Token handling
// ============================================================================

void FormulaParser::advance() {
    previous_ = current_;
    current_ = lexer_.nextToken();
}

bool FormulaParser::check(TokenType type) const {
    return current_.type == type;
}

bool FormulaParser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool FormulaParser::expect(TokenType type, const std::string& message) {
    if (check(type)) {
        advance();
        return true;
    }
    error(message);
    return false;
}

// ============================================================================
// Error handling
// ============================================================================

void FormulaParser::error(const std::string& message) {
    errors_.push_back(message);
}

std::unique_ptr<ASTNode> FormulaParser::errorNode(const std::string& message) {
    error(message);
    return std::make_unique<ErrorNode>(message, current_.position);
}

void FormulaParser::synchronize() {
    // Skip tokens until we find a reasonable recovery point
    while (!check(TokenType::END_OF_INPUT)) {
        // Recovery points: comma (next arg), rparen (end of function), end
        if (check(TokenType::COMMA) || check(TokenType::RPAREN)) {
            return;
        }
        advance();
    }
}

// ============================================================================
// Grammar rules
// ============================================================================

std::unique_ptr<ASTNode> FormulaParser::formula() {
    // formula = "=" expression | expression
    // The leading "=" is optional (we support both "=A1+B1" and "A1+B1")
    (void)match(TokenType::EQUAL);
    return expression();
}

std::unique_ptr<ASTNode> FormulaParser::expression() {
    return comparison();
}

std::unique_ptr<ASTNode> FormulaParser::comparison() {
    // comparison = concat (("=" | "<>" | "<" | "<=" | ">" | ">=") concat)*
    auto left = concat();
    if (!left) {
        return nullptr;
    }

    while (true) {
        BinaryOp op = BinaryOp::EQUAL;  // Initialize to suppress warning
        if (match(TokenType::EQUAL)) {
            op = BinaryOp::EQUAL;
        } else if (match(TokenType::NOT_EQUAL)) {
            op = BinaryOp::NOT_EQUAL;
        } else if (match(TokenType::LESS)) {
            op = BinaryOp::LESS;
        } else if (match(TokenType::LESS_EQUAL)) {
            op = BinaryOp::LESS_EQUAL;
        } else if (match(TokenType::GREATER)) {
            op = BinaryOp::GREATER;
        } else if (match(TokenType::GREATER_EQUAL)) {
            op = BinaryOp::GREATER_EQUAL;
        } else {
            break;
        }

        auto right = concat();
        if (!right) {
            right = errorNode("Expected expression after comparison operator");
        }
        left = std::make_unique<BinaryOpNode>(op, std::move(left), std::move(right));
    }

    return left;
}

std::unique_ptr<ASTNode> FormulaParser::concat() {
    // concat = additive ("&" additive)*
    auto left = additive();
    if (!left) {
        return nullptr;
    }

    while (match(TokenType::AMPERSAND)) {
        auto right = additive();
        if (!right) {
            right = errorNode("Expected expression after '&'");
        }
        left = std::make_unique<BinaryOpNode>(BinaryOp::CONCAT, std::move(left), std::move(right));
    }

    return left;
}

std::unique_ptr<ASTNode> FormulaParser::additive() {
    // additive = multiplicative (("+" | "-") multiplicative)*
    auto left = multiplicative();
    if (!left) {
        return nullptr;
    }

    while (true) {
        BinaryOp op = BinaryOp::ADD;  // Initialize to suppress warning
        if (match(TokenType::PLUS)) {
            op = BinaryOp::ADD;
        } else if (match(TokenType::MINUS)) {
            op = BinaryOp::SUBTRACT;
        } else {
            break;
        }

        auto right = multiplicative();
        if (!right) {
            right = errorNode("Expected expression after operator");
        }
        left = std::make_unique<BinaryOpNode>(op, std::move(left), std::move(right));
    }

    return left;
}

std::unique_ptr<ASTNode> FormulaParser::multiplicative() {
    // multiplicative = power (("*" | "/") power)*
    auto left = power();
    if (!left) {
        return nullptr;
    }

    while (true) {
        BinaryOp op = BinaryOp::MULTIPLY;  // Initialize to suppress warning
        if (match(TokenType::STAR)) {
            op = BinaryOp::MULTIPLY;
        } else if (match(TokenType::SLASH)) {
            op = BinaryOp::DIVIDE;
        } else {
            break;
        }

        auto right = power();
        if (!right) {
            right = errorNode("Expected expression after operator");
        }
        left = std::make_unique<BinaryOpNode>(op, std::move(left), std::move(right));
    }

    return left;
}

std::unique_ptr<ASTNode> FormulaParser::power() {
    // power = unary ("^" unary)*
    // Note: ^ is right-associative in Excel, but we parse left-to-right here
    // The resolver can adjust if needed
    auto left = unary();
    if (!left) {
        return nullptr;
    }

    while (match(TokenType::CARET)) {
        auto right = unary();
        if (!right) {
            right = errorNode("Expected expression after '^'");
        }
        left = std::make_unique<BinaryOpNode>(BinaryOp::POWER, std::move(left), std::move(right));
    }

    return left;
}

std::unique_ptr<ASTNode> FormulaParser::unary() {
    // unary = ("-" | "+")? primary
    if (match(TokenType::MINUS)) {
        const SourcePosition pos = previous_.position;
        auto operand = unary();
        if (!operand) {
            operand = errorNode("Expected expression after '-'");
        }
        return std::make_unique<UnaryOpNode>(UnaryOp::NEGATE, std::move(operand), pos);
    }
    if (match(TokenType::PLUS)) {
        const SourcePosition pos = previous_.position;
        auto operand = unary();
        if (!operand) {
            operand = errorNode("Expected expression after '+'");
        }
        return std::make_unique<UnaryOpNode>(UnaryOp::POSITIVE, std::move(operand), pos);
    }
    return primary();
}

std::unique_ptr<ASTNode> FormulaParser::primary() {
    // primary = literal | reference | function_call | "(" expression ")"

    // UUID reference tokens (stored formula format)
    if (check(TokenType::UUID_CELL_REF)) {
        return parseUuidCellRef();
    }
    if (check(TokenType::UUID_COLUMN_REF)) {
        return parseUuidColumnRef();
    }
    if (check(TokenType::UUID_ROW_REF)) {
        return parseUuidRowRef();
    }

    // Check for NUMBER followed by : (row reference)
    // Must check this BEFORE treating as number literal
    if (check(TokenType::NUMBER)) {
        // Peek to see if followed by colon (row reference like 1:1)
        const Token numToken = current_;
        advance();
        if (check(TokenType::COLON)) {
            // It's a row reference, parse it
            const int startRow = static_cast<int>(numToken.numberValue());
            advance();  // Consume :
            return parseRowRef(false, startRow, "");
        }
        // Just a number literal
        return std::make_unique<NumberLiteralNode>(numToken.numberValue(), numToken.position);
    }

    // String literal
    if (match(TokenType::STRING)) {
        return std::make_unique<StringLiteralNode>(previous_.stringValue(), previous_.position);
    }

    // Boolean literal - but check if it's actually a function call like TRUE() or FALSE()
    if (check(TokenType::BOOLEAN)) {
        const Token boolToken = current_;
        advance();
        if (check(TokenType::LPAREN)) {
            // It's a function call: TRUE() or FALSE()
            return parseFunctionCall(std::string(boolToken.text));
        }
        // Just a boolean literal
        return std::make_unique<BooleanLiteralNode>(boolToken.booleanValue(), boolToken.position);
    }

    // Parenthesized expression
    if (match(TokenType::LPAREN)) {
        auto expr = expression();
        if (!expr) {
            expr = errorNode("Expected expression after '('");
        }
        expect(TokenType::RPAREN, "Expected ')' after expression");
        return expr;
    }

    // Reference (cell, range, column, row, named) or function call
    return parseReference();
}

// ============================================================================
// Reference parsing
// ============================================================================

std::unique_ptr<ASTNode> FormulaParser::parseReference() {
    // Check for sheet prefix: SheetName! or 'Sheet Name'!
    std::string sheetName;

    // IDENTIFIER followed by ! is a sheet prefix
    if (check(TokenType::IDENTIFIER)) {
        // Peek ahead to see if this is SheetName!ref
        const Token id = current_;
        advance();
        if (check(TokenType::BANG)) {
            // It's a sheet prefix
            sheetName = std::string(id.text);
            advance();  // Consume !
        } else if (check(TokenType::LPAREN)) {
            // It's a function call: IDENTIFIER(
            return parseFunctionCall(std::string(id.text));
        } else {
            // It's either a named range or part of a cell/column reference
            // Check if it could be a column name followed by row or : or $
            const std::string name(id.text);
            if (isValidColumnName(name)) {
                // Could be column reference
                // Check what follows: number, $number, :
                if (check(TokenType::NUMBER)) {
                    // Cell reference like "A" then "1" -> A1
                    const int row = static_cast<int>(current_.numberValue());
                    // Extend position to include row number token
                    const SourcePosition fullPos{id.position.start, current_.position.end};
                    advance();
                    auto cellRef = std::make_unique<CellRefNode>(name, row, false, false, fullPos);

                    // Check for range
                    if (match(TokenType::COLON)) {
                        return parseCellOrRangeRef(sheetName);  // Parse second part of range
                    }
                    return cellRef;
                }
                if (check(TokenType::DOLLAR)) {
                    // A$1 - row absolute
                    advance();
                    if (check(TokenType::NUMBER)) {
                        const int row = static_cast<int>(current_.numberValue());
                        // Extend position to include $ and row number
                        const SourcePosition fullPos{id.position.start, current_.position.end};
                        advance();
                        auto cellRef =
                            std::make_unique<CellRefNode>(name, row, false, true, fullPos);
                        if (match(TokenType::COLON)) {
                            // Range like A$1:B$2
                            auto second = parseCellOrRangeRef("");
                            if (auto* secondCell = dynamic_cast<CellRefNode*>(second.get())) {
                                // Compute position spanning from start of first cell to end of
                                // second
                                const SourcePosition rangePos{cellRef->position.start,
                                                              secondCell->position.end};
                                return std::make_unique<RangeRefNode>(
                                    std::unique_ptr<CellRefNode>(
                                        static_cast<CellRefNode*>(cellRef.release())),
                                    std::unique_ptr<CellRefNode>(
                                        static_cast<CellRefNode*>(second.release())),
                                    rangePos);
                            }
                        }
                        return cellRef;
                    }
                }
                if (check(TokenType::COLON)) {
                    // Whole column reference: A:A or A:B
                    advance();
                    if (check(TokenType::IDENTIFIER) || check(TokenType::COLUMN)) {
                        const std::string endCol(current_.text);
                        advance();
                        if (isValidColumnName(endCol)) {
                            if (name == endCol) {
                                // Single column: A:A
                                return std::make_unique<ColumnRefNode>(name, false, id.position);
                            }
                            // Column range: A:C
                            return std::make_unique<ColumnRangeRefNode>(name, endCol, false, false);
                        }
                    }
                    if (check(TokenType::DOLLAR)) {
                        // A:$B
                        advance();
                        if (check(TokenType::IDENTIFIER) || check(TokenType::COLUMN)) {
                            const std::string endCol(current_.text);
                            advance();
                            return std::make_unique<ColumnRangeRefNode>(name, endCol, false, true);
                        }
                    }
                }
            }
            // It's a named range
            return std::make_unique<NamedRefNode>(name, ASTNamedRangeScope::WORKBOOK, id.position);
        }
    }

    // COLUMN token - could be cell ref (A1), column ref (A:A), or column range (A:C)
    if (check(TokenType::COLUMN)) {
        const std::string col(current_.text);
        const SourcePosition pos = current_.position;
        advance();

        // Check what follows the column
        if (check(TokenType::NUMBER)) {
            // Cell reference: A1
            const int row = static_cast<int>(current_.numberValue());
            // Extend position to include row number token
            const SourcePosition fullPos{pos.start, current_.position.end};
            advance();
            auto cellRef = std::make_unique<CellRefNode>(col, row, false, false, fullPos);
            cellRef->sheetName = sheetName;

            // Check for range (A1:B2)
            if (match(TokenType::COLON)) {
                auto second = parseCellOrRangeRef(sheetName);
                if (auto* secondCell = dynamic_cast<CellRefNode*>(second.get())) {
                    // Compute position spanning from start of first cell to end of second
                    const SourcePosition rangePos{cellRef->position.start,
                                                  secondCell->position.end};
                    return std::make_unique<RangeRefNode>(
                        std::move(cellRef),
                        std::unique_ptr<CellRefNode>(static_cast<CellRefNode*>(second.release())),
                        rangePos);
                }
                return errorNode("Invalid range reference");
            }
            return cellRef;
        }
        if (check(TokenType::DOLLAR)) {
            // A$1 - row absolute
            advance();
            if (check(TokenType::NUMBER)) {
                const int row = static_cast<int>(current_.numberValue());
                // Extend position to include $ and row number
                const SourcePosition fullPos{pos.start, current_.position.end};
                advance();
                auto cellRef = std::make_unique<CellRefNode>(col, row, false, true, fullPos);
                cellRef->sheetName = sheetName;

                // Check for range
                if (match(TokenType::COLON)) {
                    auto second = parseCellOrRangeRef(sheetName);
                    if (auto* secondCell = dynamic_cast<CellRefNode*>(second.get())) {
                        // Compute position spanning from start of first cell to end of second
                        const SourcePosition rangePos{cellRef->position.start,
                                                      secondCell->position.end};
                        return std::make_unique<RangeRefNode>(
                            std::move(cellRef),
                            std::unique_ptr<CellRefNode>(
                                static_cast<CellRefNode*>(second.release())),
                            rangePos);
                    }
                }
                return cellRef;
            }
            return errorNode("Expected row number after '$'");
        }
        if (check(TokenType::COLON)) {
            // Whole column reference: A:A or A:C
            advance();
            // Parse second column
            const bool secondAbsolute = match(TokenType::DOLLAR);
            if (check(TokenType::COLUMN) || check(TokenType::IDENTIFIER)) {
                const std::string endCol(current_.text);
                advance();
                if (isValidColumnName(endCol)) {
                    if (col == endCol && !secondAbsolute) {
                        // Single column: A:A
                        auto node = std::make_unique<ColumnRefNode>(col, false, pos);
                        node->sheetName = sheetName;
                        return node;
                    }
                    // Column range: A:C
                    auto node =
                        std::make_unique<ColumnRangeRefNode>(col, endCol, false, secondAbsolute);
                    node->sheetName = sheetName;
                    return node;
                }
            }
            return errorNode("Expected column after ':'");
        }
        // Just column letter at end - treat as named ref (unusual but possible)
        return std::make_unique<NamedRefNode>(col, ASTNamedRangeScope::WORKBOOK, pos);
    }

    // $ followed by column/identifier (absolute reference)
    if (check(TokenType::DOLLAR)) {
        return parseCellOrRangeRef(sheetName);
    }

    // NUMBER followed by : could be row reference
    if (check(TokenType::NUMBER)) {
        const int startRow = static_cast<int>(current_.numberValue());
        const SourcePosition pos = current_.position;
        advance();
        if (match(TokenType::COLON)) {
            return parseRowRef(false, startRow, sheetName);
        }
        // Just a number literal (shouldn't normally happen in formula context)
        return std::make_unique<NumberLiteralNode>(static_cast<double>(startRow), pos);
    }

    // End of input or unexpected token
    if (check(TokenType::END_OF_INPUT)) {
        return errorNode("Unexpected end of formula");
    }

    return errorNode("Unexpected token: " + std::string(current_.text));
}

std::unique_ptr<ASTNode> FormulaParser::parseCellOrRangeRef(const std::string& sheetName) {
    auto components = parseCellRefComponents();
    if (!components.valid) {
        return errorNode("Invalid cell reference");
    }

    auto cellRef =
        std::make_unique<CellRefNode>(components.column, components.row, components.colAbsolute,
                                      components.rowAbsolute, components.position);
    cellRef->sheetName = sheetName;

    // Check for range
    if (match(TokenType::COLON)) {
        auto secondComponents = parseCellRefComponents();
        if (!secondComponents.valid) {
            // Might be column range (A1:B) or row range (A1:5)
            // For simplicity, create error
            return errorNode("Invalid range reference");
        }

        auto secondCell = std::make_unique<CellRefNode>(
            secondComponents.column, secondComponents.row, secondComponents.colAbsolute,
            secondComponents.rowAbsolute, secondComponents.position);
        secondCell->sheetName = sheetName;

        // Compute position spanning from start of first cell to end of second
        const SourcePosition rangePos{cellRef->position.start, secondCell->position.end};
        return std::make_unique<RangeRefNode>(std::move(cellRef), std::move(secondCell), rangePos);
    }

    return cellRef;
}

FormulaParser::CellRefComponents FormulaParser::parseCellRefComponents() {
    CellRefComponents result;

    // Track start position (accounting for optional $ before column)
    const size_t startPos = current_.position.start;

    // Optional $ for column absolute
    if (match(TokenType::DOLLAR)) {
        result.colAbsolute = true;
    }

    // Column: COLUMN token or IDENTIFIER that's a valid column name
    if (check(TokenType::COLUMN)) {
        result.column = std::string(current_.text);
        advance();
    } else if (check(TokenType::IDENTIFIER)) {
        const std::string name(current_.text);
        if (isValidColumnName(name)) {
            result.column = name;
            advance();
        } else {
            return result;  // Invalid
        }
    } else {
        return result;  // Invalid
    }

    // Optional $ for row absolute
    if (match(TokenType::DOLLAR)) {
        result.rowAbsolute = true;
    }

    // Row number
    if (check(TokenType::NUMBER)) {
        result.row = static_cast<int>(current_.numberValue());
        // Set position to span from start to end of row number
        result.position = {startPos, current_.position.end};
        advance();
        result.valid = true;
    }

    return result;
}

std::unique_ptr<ASTNode> FormulaParser::parseRowRef(bool startAbsolute, int startRow,
                                                    const std::string& sheetName) {
    // After seeing "N:" we expect another number
    const bool endAbsolute = match(TokenType::DOLLAR);

    if (check(TokenType::NUMBER)) {
        const int endRow = static_cast<int>(current_.numberValue());
        advance();

        if (startRow == endRow && startAbsolute == endAbsolute) {
            // Single row: 1:1
            auto node = std::make_unique<RowRefNode>(startRow, startAbsolute);
            node->sheetName = sheetName;
            return node;
        }
        // Row range: 1:5
        auto node = std::make_unique<RowRangeRefNode>(startRow, endRow, startAbsolute, endAbsolute);
        node->sheetName = sheetName;
        return node;
    }

    return errorNode("Expected row number after ':'");
}

std::unique_ptr<ASTNode> FormulaParser::parseFunctionCall(const std::string& name) {
    const SourcePosition pos = previous_.position;  // Position of function name
    expect(TokenType::LPAREN, "Expected '(' after function name");

    auto func = std::make_unique<FunctionCallNode>(name, pos);
    func->isVolatile = FunctionCallNode::isVolatileFunction(name);

    // Parse arguments
    if (!check(TokenType::RPAREN)) {
        do {
            auto arg = expression();
            if (!arg) {
                arg = errorNode("Expected expression in function argument");
            }
            func->args.push_back(std::move(arg));
        } while (match(TokenType::COMMA));
    }

    expect(TokenType::RPAREN, "Expected ')' after function arguments");
    return func;
}

bool FormulaParser::isValidColumnName(const std::string& name) {
    // Valid column names are 1-3 letters A-Z (case insensitive)
    if (name.empty() || name.size() > 3) {
        return false;
    }
    for (const char c : name) {
        if (std::isalpha(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// UUID Reference Parsing (stored formula format)
// ============================================================================

std::unique_ptr<ASTNode> FormulaParser::parseUuidCellRef() {
    // Token text format: PREFIX + 8-char UUID
    // PREFIX: $$ (both abs), $~ (col abs), ~$ (row abs), ~~ (both rel)
    const Token tok = current_;
    const std::string text(tok.text);
    advance();

    if (text.size() != 10) {
        return errorNode("Invalid UUID cell reference: expected 10 characters");
    }

    // Parse prefix to get absolute flags
    const bool colAbsolute = (text[0] == '$');
    const bool rowAbsolute = (text[1] == '$');
    const std::string cellId = text.substr(2, 8);

    // Create CellRefNode with cellId already filled in (no resolution needed)
    // We set column="" and row=0 since we have the UUID directly
    auto node = std::make_unique<CellRefNode>("", 0, colAbsolute, rowAbsolute, tok.position);
    node->cellId = cellId;

    // Check for range (UUID_CELL_REF : UUID_CELL_REF)
    if (match(TokenType::COLON)) {
        if (check(TokenType::UUID_CELL_REF)) {
            auto secondNode = parseUuidCellRef();
            if (auto* secondCell = dynamic_cast<CellRefNode*>(secondNode.get())) {
                // Compute position spanning from start of first cell to end of second
                const SourcePosition rangePos{node->position.start, secondCell->position.end};
                return std::make_unique<RangeRefNode>(
                    std::move(node),
                    std::unique_ptr<CellRefNode>(static_cast<CellRefNode*>(secondNode.release())),
                    rangePos);
            }
        }
        return errorNode("Expected UUID cell reference after ':'");
    }

    return node;
}

std::unique_ptr<ASTNode> FormulaParser::parseUuidColumnRef() {
    // Token text format: @$ or @~ followed by 8-char UUID
    const Token tok = current_;
    const std::string text(tok.text);
    advance();

    if (text.size() != 10) {
        return errorNode("Invalid UUID column reference: expected 10 characters");
    }

    // Parse prefix: @$ = absolute, @~ = relative
    const bool absolute = (text[1] == '$');
    const std::string columnId = text.substr(2, 8);

    // Create ColumnRefNode with columnId already filled in
    auto node = std::make_unique<ColumnRefNode>("", absolute, tok.position);
    node->columnId = columnId;

    // Check for column range (@~colId1 : @~colId2)
    if (match(TokenType::COLON)) {
        if (check(TokenType::UUID_COLUMN_REF)) {
            auto secondNode = parseUuidColumnRef();
            if (auto* secondCol = dynamic_cast<ColumnRefNode*>(secondNode.get())) {
                // Convert to column range
                auto rangeNode =
                    std::make_unique<ColumnRangeRefNode>("", "", absolute, secondCol->absolute);
                rangeNode->startColumnId = columnId;
                rangeNode->endColumnId = secondCol->columnId;
                return rangeNode;
            }
        }
        return errorNode("Expected UUID column reference after ':'");
    }

    return node;
}

std::unique_ptr<ASTNode> FormulaParser::parseUuidRowRef() {
    // Token text format: #$ or #~ followed by 8-char UUID
    const Token tok = current_;
    const std::string text(tok.text);
    advance();

    if (text.size() != 10) {
        return errorNode("Invalid UUID row reference: expected 10 characters");
    }

    // Parse prefix: #$ = absolute, #~ = relative
    const bool absolute = (text[1] == '$');
    const std::string rowId = text.substr(2, 8);

    // Create RowRefNode with rowId already filled in
    auto node = std::make_unique<RowRefNode>(0, absolute, tok.position);
    node->rowId = rowId;

    // Check for row range (#~rowId1 : #~rowId2)
    if (match(TokenType::COLON)) {
        if (check(TokenType::UUID_ROW_REF)) {
            auto secondNode = parseUuidRowRef();
            if (auto* secondRow = dynamic_cast<RowRefNode*>(secondNode.get())) {
                // Convert to row range
                auto rangeNode =
                    std::make_unique<RowRangeRefNode>(0, 0, absolute, secondRow->absolute);
                rangeNode->startRowId = rowId;
                rangeNode->endRowId = secondRow->rowId;
                return rangeNode;
            }
        }
        return errorNode("Expected UUID row reference after ':'");
    }

    return node;
}

}  // namespace cells
