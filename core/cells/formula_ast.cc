#include "core/cells/formula_ast.h"

#include <algorithm>
#include <sstream>

namespace cells {

// ============================================================================
// JSON serialization helpers
// ============================================================================

namespace {

std::string escapeJsonString(const std::string& s) {
    std::ostringstream oss;
    for (const char c : s) {
        switch (c) {
            case '"':
                oss << "\\\"";
                break;
            case '\\':
                oss << "\\\\";
                break;
            case '\n':
                oss << "\\n";
                break;
            case '\r':
                oss << "\\r";
                break;
            case '\t':
                oss << "\\t";
                break;
            default:
                oss << c;
                break;
        }
    }
    return oss.str();
}

}  // namespace

// ============================================================================
// NumberLiteralNode
// ============================================================================

std::string NumberLiteralNode::toJson() const {
    std::ostringstream oss;
    oss << R"({"type":"NumberLiteral","value":)" << value << "}";
    return oss.str();
}

// ============================================================================
// StringLiteralNode
// ============================================================================

std::string StringLiteralNode::toJson() const {
    std::ostringstream oss;
    oss << R"({"type":"StringLiteral","value":")" << escapeJsonString(value) << "\"}";
    return oss.str();
}

// ============================================================================
// BooleanLiteralNode
// ============================================================================

std::string BooleanLiteralNode::toJson() const {
    std::ostringstream oss;
    oss << R"({"type":"BooleanLiteral","value":)" << (value ? "true" : "false") << "}";
    return oss.str();
}

// ============================================================================
// CellRefNode
// ============================================================================

std::string CellRefNode::toJson() const {
    std::ostringstream oss;
    oss << R"({"type":"CellRef","column":")" << column << R"(","row":)" << row
        << R"(,"colAbsolute":)" << (colAbsolute ? "true" : "false") << R"(,"rowAbsolute":)"
        << (rowAbsolute ? "true" : "false");
    if (!sheetName.empty()) {
        oss << R"(,"sheet":")" << escapeJsonString(sheetName) << "\"";
    }
    if (!cellId.empty()) {
        oss << R"(,"cellId":")" << cellId << "\"";
    }
    oss << "}";
    return oss.str();
}

// ============================================================================
// RangeRefNode
// ============================================================================

std::string RangeRefNode::toJson() const {
    std::ostringstream oss;
    oss << R"({"type":"RangeRef","topLeft":)" << topLeft->toJson() << R"(,"bottomRight":)"
        << bottomRight->toJson() << "}";
    return oss.str();
}

// ============================================================================
// ColumnRefNode
// ============================================================================

std::string ColumnRefNode::toJson() const {
    std::ostringstream oss;
    oss << R"({"type":"ColumnRef","column":")" << column << R"(","absolute":)"
        << (absolute ? "true" : "false");
    if (!sheetName.empty()) {
        oss << R"(,"sheet":")" << escapeJsonString(sheetName) << "\"";
    }
    if (!columnId.empty()) {
        oss << R"(,"columnId":")" << columnId << "\"";
    }
    oss << "}";
    return oss.str();
}

// ============================================================================
// RowRefNode
// ============================================================================

std::string RowRefNode::toJson() const {
    std::ostringstream oss;
    oss << R"({"type":"RowRef","row":)" << row << R"(,"absolute":)"
        << (absolute ? "true" : "false");
    if (!sheetName.empty()) {
        oss << R"(,"sheet":")" << escapeJsonString(sheetName) << "\"";
    }
    if (!rowId.empty()) {
        oss << R"(,"rowId":")" << rowId << "\"";
    }
    oss << "}";
    return oss.str();
}

// ============================================================================
// ColumnRangeRefNode
// ============================================================================

std::string ColumnRangeRefNode::toJson() const {
    std::ostringstream oss;
    oss << R"({"type":"ColumnRangeRef","startColumn":")" << startColumn << R"(","endColumn":")"
        << endColumn << R"(","startAbsolute":)" << (startAbsolute ? "true" : "false")
        << R"(,"endAbsolute":)" << (endAbsolute ? "true" : "false");
    if (!sheetName.empty()) {
        oss << R"(,"sheet":")" << escapeJsonString(sheetName) << "\"";
    }
    oss << "}";
    return oss.str();
}

// ============================================================================
// RowRangeRefNode
// ============================================================================

std::string RowRangeRefNode::toJson() const {
    std::ostringstream oss;
    oss << R"({"type":"RowRangeRef","startRow":)" << startRow << R"(,"endRow":)" << endRow
        << R"(,"startAbsolute":)" << (startAbsolute ? "true" : "false") << R"(,"endAbsolute":)"
        << (endAbsolute ? "true" : "false");
    if (!sheetName.empty()) {
        oss << R"(,"sheet":")" << escapeJsonString(sheetName) << "\"";
    }
    oss << "}";
    return oss.str();
}

// ============================================================================
// NamedRefNode
// ============================================================================

std::string NamedRefNode::toJson() const {
    std::ostringstream oss;
    oss << R"({"type":"NamedRef","name":")" << escapeJsonString(name) << R"(","scope":")"
        << (scope == ASTNamedRangeScope::WORKBOOK ? "workbook" : "sheet") << "\"}";
    return oss.str();
}

// ============================================================================
// BinaryOpNode
// ============================================================================

const char* BinaryOpNode::opToString(BinaryOp op) {
    switch (op) {
        case BinaryOp::ADD:
            return "+";
        case BinaryOp::SUBTRACT:
            return "-";
        case BinaryOp::MULTIPLY:
            return "*";
        case BinaryOp::DIVIDE:
            return "/";
        case BinaryOp::POWER:
            return "^";
        case BinaryOp::CONCAT:
            return "&";
        case BinaryOp::EQUAL:
            return "=";
        case BinaryOp::NOT_EQUAL:
            return "<>";
        case BinaryOp::LESS:
            return "<";
        case BinaryOp::LESS_EQUAL:
            return "<=";
        case BinaryOp::GREATER:
            return ">";
        case BinaryOp::GREATER_EQUAL:
            return ">=";
    }
    return "?";
}

std::string BinaryOpNode::toJson() const {
    std::ostringstream oss;
    oss << R"({"type":"BinaryOp","op":")" << opToString(op) << R"(","left":)" << left->toJson()
        << R"(,"right":)" << right->toJson() << "}";
    return oss.str();
}

// ============================================================================
// UnaryOpNode
// ============================================================================

const char* UnaryOpNode::opToString(UnaryOp op) {
    switch (op) {
        case UnaryOp::NEGATE:
            return "-";
        case UnaryOp::POSITIVE:
            return "+";
    }
    return "?";
}

std::string UnaryOpNode::toJson() const {
    std::ostringstream oss;
    oss << R"({"type":"UnaryOp","op":")" << opToString(op) << R"(","operand":)" << operand->toJson()
        << "}";
    return oss.str();
}

// ============================================================================
// FunctionCallNode
// ============================================================================

bool FunctionCallNode::isVolatileFunction(const std::string& name) {
    // Convert to uppercase for case-insensitive comparison
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    return upper == "NOW" || upper == "TODAY" || upper == "RAND" || upper == "RANDBETWEEN" ||
           upper == "OFFSET" || upper == "INDIRECT";
}

std::string FunctionCallNode::toJson() const {
    std::ostringstream oss;
    oss << R"({"type":"FunctionCall","name":")" << escapeJsonString(name) << R"(","isVolatile":)"
        << (isVolatile ? "true" : "false") << R"(,"args":[)";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << args[i]->toJson();
    }
    oss << "]}";
    return oss.str();
}

// ============================================================================
// ErrorNode
// ============================================================================

std::string ErrorNode::toJson() const {
    std::ostringstream oss;
    oss << R"({"type":"Error","message":")" << escapeJsonString(message) << R"(")";
    if (!rawText.empty()) {
        oss << R"(,"rawText":")" << escapeJsonString(rawText) << R"(")";
    }
    oss << R"(,"partialChildren":[)";
    for (size_t i = 0; i < partialChildren.size(); ++i) {
        if (i > 0) {
            oss << ",";
        }
        oss << partialChildren[i]->toJson();
    }
    oss << "]}";
    return oss.str();
}

}  // namespace cells
