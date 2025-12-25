#include "core/cells/formula_serializer.h"

#include <sstream>

namespace cells {

std::string FormulaSerializer::serialize(const ASTNode* ast) {
    if (ast == nullptr) {
        return "";
    }
    return "=" + nodeToUuidString(ast);
}

std::string FormulaSerializer::refPrefix(bool colAbsolute, bool rowAbsolute) {
    if (colAbsolute && rowAbsolute) {
        return "$$";
    }
    if (colAbsolute) {
        return "$~";
    }
    if (rowAbsolute) {
        return "~$";
    }
    return "~~";
}

std::string FormulaSerializer::nodeToUuidString(const ASTNode* node) {
    if (node == nullptr) {
        return "";
    }

    switch (node->type) {
        case ASTNodeType::NUMBER_LITERAL: {
            auto* numNode = static_cast<const NumberLiteralNode*>(node);
            std::ostringstream oss;
            oss << numNode->value;
            return oss.str();
        }
        case ASTNodeType::STRING_LITERAL: {
            auto* strNode = static_cast<const StringLiteralNode*>(node);
            // Escape double quotes within the string
            std::string escaped;
            for (const char c : strNode->value) {
                if (c == '"') {
                    escaped += "\"\"";
                } else {
                    escaped += c;
                }
            }
            return "\"" + escaped + "\"";
        }
        case ASTNodeType::BOOLEAN_LITERAL: {
            auto* boolNode = static_cast<const BooleanLiteralNode*>(node);
            return boolNode->value ? "TRUE" : "FALSE";
        }
        case ASTNodeType::CELL_REF:
            return cellRefToUuidString(static_cast<const CellRefNode*>(node));
        case ASTNodeType::RANGE_REF:
            return rangeRefToUuidString(static_cast<const RangeRefNode*>(node));
        case ASTNodeType::COLUMN_REF:
            return columnRefToUuidString(static_cast<const ColumnRefNode*>(node));
        case ASTNodeType::ROW_REF:
            return rowRefToUuidString(static_cast<const RowRefNode*>(node));
        case ASTNodeType::COLUMN_RANGE_REF:
            return columnRangeRefToUuidString(static_cast<const ColumnRangeRefNode*>(node));
        case ASTNodeType::ROW_RANGE_REF:
            return rowRangeRefToUuidString(static_cast<const RowRangeRefNode*>(node));
        case ASTNodeType::NAMED_REF:
            return namedRefToUuidString(static_cast<const NamedRefNode*>(node));
        case ASTNodeType::BINARY_OP:
            return binaryOpToUuidString(static_cast<const BinaryOpNode*>(node));
        case ASTNodeType::UNARY_OP:
            return unaryOpToUuidString(static_cast<const UnaryOpNode*>(node));
        case ASTNodeType::FUNCTION_CALL:
            return functionCallToUuidString(static_cast<const FunctionCallNode*>(node));
        case ASTNodeType::ERROR_NODE:
            return errorNodeToUuidString(static_cast<const ErrorNode*>(node));
    }

    return "";
}

std::string FormulaSerializer::cellRefToUuidString(const CellRefNode* node) {
    std::string result;

    // Add sheet prefix if present
    if (!node->sheetName.empty()) {
        result += node->sheetName + "!";
    }

    // If we have a resolved cellId, use UUID format
    if (!node->cellId.empty()) {
        result += refPrefix(node->colAbsolute, node->rowAbsolute);
        result += node->cellId;
        return result;
    }

    // Fall back to original A1 notation (unresolved case)
    if (node->colAbsolute) {
        result += "$";
    }
    result += node->column;
    if (node->rowAbsolute) {
        result += "$";
    }
    result += std::to_string(node->row);

    return result;
}

std::string FormulaSerializer::rangeRefToUuidString(const RangeRefNode* node) {
    return cellRefToUuidString(node->topLeft.get()) + ":" +
           cellRefToUuidString(node->bottomRight.get());
}

std::string FormulaSerializer::columnRefToUuidString(const ColumnRefNode* node) {
    std::string result;

    if (!node->sheetName.empty()) {
        result += node->sheetName + "!";
    }

    // If we have a resolved columnId, use UUID format
    // Column refs use @$ prefix (@ indicates column ref)
    if (!node->columnId.empty()) {
        result += node->absolute ? "@$" : "@~";
        result += node->columnId;
        return result;
    }

    // Fall back to original notation
    if (node->absolute) {
        result += "$";
    }
    result += node->column + ":" + node->column;

    return result;
}

std::string FormulaSerializer::rowRefToUuidString(const RowRefNode* node) {
    std::string result;

    if (!node->sheetName.empty()) {
        result += node->sheetName + "!";
    }

    // If we have a resolved rowId, use UUID format
    // Row refs use #$ prefix (# indicates row ref)
    if (!node->rowId.empty()) {
        result += node->absolute ? "#$" : "#~";
        result += node->rowId;
        return result;
    }

    // Fall back to original notation
    if (node->absolute) {
        result += "$";
    }
    const std::string rowNum = std::to_string(node->row);
    result += rowNum + ":" + rowNum;

    return result;
}

std::string FormulaSerializer::columnRangeRefToUuidString(const ColumnRangeRefNode* node) {
    std::string result;

    if (!node->sheetName.empty()) {
        result += node->sheetName + "!";
    }

    // If resolved, use UUID format
    if (!node->startColumnId.empty() && !node->endColumnId.empty()) {
        result += node->startAbsolute ? "@$" : "@~";
        result += node->startColumnId;
        result += ":";
        result += node->endAbsolute ? "@$" : "@~";
        result += node->endColumnId;
        return result;
    }

    // Fall back to original notation
    if (node->startAbsolute) {
        result += "$";
    }
    result += node->startColumn;
    result += ":";
    if (node->endAbsolute) {
        result += "$";
    }
    result += node->endColumn;

    return result;
}

std::string FormulaSerializer::rowRangeRefToUuidString(const RowRangeRefNode* node) {
    std::string result;

    if (!node->sheetName.empty()) {
        result += node->sheetName + "!";
    }

    // If resolved, use UUID format
    if (!node->startRowId.empty() && !node->endRowId.empty()) {
        result += node->startAbsolute ? "#$" : "#~";
        result += node->startRowId;
        result += ":";
        result += node->endAbsolute ? "#$" : "#~";
        result += node->endRowId;
        return result;
    }

    // Fall back to original notation
    if (node->startAbsolute) {
        result += "$";
    }
    result += std::to_string(node->startRow);
    result += ":";
    if (node->endAbsolute) {
        result += "$";
    }
    result += std::to_string(node->endRow);

    return result;
}

std::string FormulaSerializer::namedRefToUuidString(const NamedRefNode* node) {
    // Named ranges pass through as-is
    return node->name;
}

std::string FormulaSerializer::binaryOpToUuidString(const BinaryOpNode* node) {
    std::string left = nodeToUuidString(node->left.get());
    std::string right = nodeToUuidString(node->right.get());

    // Add parentheses if needed for precedence
    if (needsParentheses(node, node->left.get(), false)) {
        left = "(" + left + ")";
    }
    if (needsParentheses(node, node->right.get(), true)) {
        right = "(" + right + ")";
    }

    return left + BinaryOpNode::opToString(node->op) + right;
}

std::string FormulaSerializer::unaryOpToUuidString(const UnaryOpNode* node) {
    std::string operand = nodeToUuidString(node->operand.get());

    // Unary operators may need parentheses around complex expressions
    if (node->operand->type == ASTNodeType::BINARY_OP) {
        operand = "(" + operand + ")";
    }

    return UnaryOpNode::opToString(node->op) + operand;
}

std::string FormulaSerializer::functionCallToUuidString(const FunctionCallNode* node) {
    std::string result = node->name + "(";

    for (size_t i = 0; i < node->args.size(); ++i) {
        if (i > 0) {
            result += ",";
        }
        result += nodeToUuidString(node->args[i].get());
    }

    result += ")";
    return result;
}

std::string FormulaSerializer::errorNodeToUuidString(const ErrorNode* node) {
    // For error nodes, try to reconstruct what we can from partial children
    std::string result = "#ERROR!";
    for (const auto& child : node->partialChildren) {
        result += nodeToUuidString(child.get());
    }
    return result;
}

bool FormulaSerializer::needsParentheses(const ASTNode* parent, const ASTNode* child,
                                         bool isRight) {
    if (child->type != ASTNodeType::BINARY_OP || parent->type != ASTNodeType::BINARY_OP) {
        return false;
    }

    auto* parentOp = static_cast<const BinaryOpNode*>(parent);
    auto* childOp = static_cast<const BinaryOpNode*>(child);

    // Define operator precedence (higher = binds tighter)
    auto precedence = [](BinaryOp op) -> int {
        switch (op) {
            case BinaryOp::EQUAL:
            case BinaryOp::NOT_EQUAL:
            case BinaryOp::LESS:
            case BinaryOp::LESS_EQUAL:
            case BinaryOp::GREATER:
            case BinaryOp::GREATER_EQUAL:
                return 1;
            case BinaryOp::CONCAT:
                return 2;
            case BinaryOp::ADD:
            case BinaryOp::SUBTRACT:
                return 3;
            case BinaryOp::MULTIPLY:
            case BinaryOp::DIVIDE:
                return 4;
            case BinaryOp::POWER:
                return 5;
        }
        return 0;
    };

    const int parentPrec = precedence(parentOp->op);
    const int childPrec = precedence(childOp->op);

    // Lower precedence child needs parentheses
    if (childPrec < parentPrec) {
        return true;
    }

    // Same precedence, right child needs parentheses for left-associative operators
    // (all except power which is right-associative)
    if (childPrec == parentPrec && isRight && parentOp->op != BinaryOp::POWER) {
        return true;
    }

    return false;
}

}  // namespace cells
