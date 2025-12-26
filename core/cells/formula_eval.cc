#include "core/cells/formula_eval.h"

#include <cmath>

#include <string>

#include "core/cells/formula_ast.h"
#include "core/cells/model.h"

namespace cells {

// Forward declarations for evaluation functions
static EvalResult evaluateLiteral(const ASTNode* node);
static EvalResult evaluateCellRef(const CellRefNode* node, EvalContext& ctx);
static EvalResult evaluateBinaryOp(const BinaryOpNode* node, EvalContext& ctx);
static EvalResult evaluateUnaryOp(const UnaryOpNode* node, EvalContext& ctx);

// Convert a CellValue to an EvalResult
static EvalResult cellValueToEvalResult(const CellValue& value) {
    switch (value.type) {
        case CellValueType::NUMBER:
            return EvalResult::Number(value.asNumber());
        case CellValueType::STRING:
            // Empty string is treated as empty cell (returns 0 in numeric context)
            if (value.raw.empty()) {
                return EvalResult::Empty();
            }
            return EvalResult::String(value.asString());
        case CellValueType::BOOLEAN:
            return EvalResult::Boolean(value.asBoolean());
        case CellValueType::ERROR:
            return EvalResult::Error(value.error);
        case CellValueType::FORMULA:
            // Formula cells should have their value evaluated
            // This case handles the cached result
            if (value.error != CellError::NONE) {
                return EvalResult::Error(value.error);
            }
            // Try to parse as number first
            if (!value.raw.empty()) {
                try {
                    size_t pos = 0;
                    double val = std::stod(value.raw, &pos);
                    if (pos == value.raw.size()) {
                        return EvalResult::Number(val);
                    }
                } catch (...) {
                    // Not a number, return as string
                }
                return EvalResult::String(value.raw);
            }
            return EvalResult::Empty();
        case CellValueType::DATE:
        case CellValueType::DATE_TIME:
            // Dates are stored as serial numbers (days since epoch)
            return EvalResult::Number(value.asNumber());
    }
    return EvalResult::Empty();
}

// Evaluate a literal node (NUMBER, STRING, BOOLEAN)
static EvalResult evaluateLiteral(const ASTNode* node) {
    switch (node->type) {
        case ASTNodeType::NUMBER_LITERAL:
            return EvalResult::Number(static_cast<const NumberLiteralNode*>(node)->value);
        case ASTNodeType::STRING_LITERAL:
            return EvalResult::String(static_cast<const StringLiteralNode*>(node)->value);
        case ASTNodeType::BOOLEAN_LITERAL:
            return EvalResult::Boolean(static_cast<const BooleanLiteralNode*>(node)->value);
        default:
            return EvalResult::Error(CellError::VALUE);
    }
}

// Evaluate a cell reference
static EvalResult evaluateCellRef(const CellRefNode* node, EvalContext& ctx) {
    if (!ctx.sheet) {
        return EvalResult::Error(CellError::REF);
    }

    // Get the cell ID (should be resolved already)
    ID cellId(node->cellId);
    if (cellId.isNull()) {
        // Unresolved reference
        return EvalResult::Error(CellError::REF);
    }

    // Check for circular reference
    if (ctx.evaluatingCells && ctx.evaluatingCells->count(cellId)) {
        return EvalResult::Error(CellError::CIRCULAR);
    }

    Cell* cell = ctx.sheet->getCell(cellId);
    if (!cell) {
        // Empty cell reference returns 0
        return EvalResult::Number(0.0);
    }

    // If cell has a formula that needs evaluation, evaluate it
    Formula* formula = cell->getFormula();
    if (formula && formula->dirty && formula->ast) {
        // Mark that we're evaluating this cell (circular reference detection)
        bool addedToSet = false;
        if (ctx.evaluatingCells) {
            ctx.evaluatingCells->insert(cellId);
            addedToSet = true;
        }

        // Check recursion depth
        if (ctx.recursionDepth >= EvalContext::MAX_RECURSION) {
            if (addedToSet) {
                ctx.evaluatingCells->erase(cellId);
            }
            return EvalResult::Error(CellError::CIRCULAR);
        }

        // Recursively evaluate
        EvalContext subCtx = ctx;
        subCtx.currentCellId = cellId;
        subCtx.recursionDepth++;

        EvalResult result = evaluate(formula->ast, subCtx);

        // Store result in cell value
        if (result.isError()) {
            cell->value = CellValue(result.getError());
        } else if (result.isNumber()) {
            cell->value = CellValue(result.getNumber());
        } else if (result.isString()) {
            cell->value = CellValue(result.getString());
        } else if (result.isBoolean()) {
            cell->value = CellValue(result.getBoolean());
        } else {
            cell->value = CellValue("");  // Empty
        }

        // Mark as clean
        formula->dirty = false;

        // Remove from evaluating set
        if (addedToSet) {
            ctx.evaluatingCells->erase(cellId);
        }

        return result;
    }

    // Return the cell's current value
    return cellValueToEvalResult(cell->value);
}

// Compare two EvalResults for equality
static EvalResult compareEqual(const EvalResult& left, const EvalResult& right) {
    // Type coercion for comparison:
    // If both are same type, compare directly
    // If one is number and one is string that looks like number, coerce
    // Booleans compare as numbers (true=1, false=0) when compared to numbers

    if (left.type == right.type) {
        switch (left.type) {
            case EvalResult::Type::NUMBER:
                return EvalResult::Boolean(left.numberValue == right.numberValue);
            case EvalResult::Type::STRING:
                // Case-insensitive string comparison (Excel behavior)
                {
                    std::string l = left.stringValue;
                    std::string r = right.stringValue;
                    for (auto& c : l)
                        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    for (auto& c : r)
                        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    return EvalResult::Boolean(l == r);
                }
            case EvalResult::Type::BOOLEAN:
                return EvalResult::Boolean(left.boolValue == right.boolValue);
            default:
                return EvalResult::Boolean(false);
        }
    }

    // Mixed type comparison - try to coerce to number
    EvalResult leftNum = left.toNumber();
    EvalResult rightNum = right.toNumber();
    if (!leftNum.isError() && !rightNum.isError()) {
        return EvalResult::Boolean(leftNum.numberValue == rightNum.numberValue);
    }

    // Different types that can't be coerced are not equal
    return EvalResult::Boolean(false);
}

// Compare two EvalResults (returns negative, zero, or positive)
static int compareValues(const EvalResult& left, const EvalResult& right) {
    // Same type comparison
    if (left.type == right.type) {
        switch (left.type) {
            case EvalResult::Type::NUMBER:
                if (left.numberValue < right.numberValue)
                    return -1;
                if (left.numberValue > right.numberValue)
                    return 1;
                return 0;
            case EvalResult::Type::STRING:
                return left.stringValue.compare(right.stringValue);
            case EvalResult::Type::BOOLEAN:
                if (left.boolValue == right.boolValue)
                    return 0;
                return left.boolValue ? 1 : -1;  // true > false
            default:
                return 0;
        }
    }

    // Mixed type - try to coerce to number
    EvalResult leftNum = left.toNumber();
    EvalResult rightNum = right.toNumber();
    if (!leftNum.isError() && !rightNum.isError()) {
        if (leftNum.numberValue < rightNum.numberValue)
            return -1;
        if (leftNum.numberValue > rightNum.numberValue)
            return 1;
        return 0;
    }

    // Fallback: type ordering (number < string < boolean)
    auto typeOrder = [](EvalResult::Type t) -> int {
        switch (t) {
            case EvalResult::Type::NUMBER:
                return 0;
            case EvalResult::Type::STRING:
                return 1;
            case EvalResult::Type::BOOLEAN:
                return 2;
            default:
                return 3;
        }
    };
    return typeOrder(left.type) - typeOrder(right.type);
}

// Evaluate a binary operation
static EvalResult evaluateBinaryOp(const BinaryOpNode* node, EvalContext& ctx) {
    EvalResult left = evaluate(node->left.get(), ctx);
    EvalResult right = evaluate(node->right.get(), ctx);

    // Error propagation
    if (left.isError())
        return left;
    if (right.isError())
        return right;

    switch (node->op) {
        case BinaryOp::ADD: {
            EvalResult leftNum = left.toNumber();
            EvalResult rightNum = right.toNumber();
            if (leftNum.isError())
                return leftNum;
            if (rightNum.isError())
                return rightNum;
            return EvalResult::Number(leftNum.numberValue + rightNum.numberValue);
        }
        case BinaryOp::SUBTRACT: {
            EvalResult leftNum = left.toNumber();
            EvalResult rightNum = right.toNumber();
            if (leftNum.isError())
                return leftNum;
            if (rightNum.isError())
                return rightNum;
            return EvalResult::Number(leftNum.numberValue - rightNum.numberValue);
        }
        case BinaryOp::MULTIPLY: {
            EvalResult leftNum = left.toNumber();
            EvalResult rightNum = right.toNumber();
            if (leftNum.isError())
                return leftNum;
            if (rightNum.isError())
                return rightNum;
            return EvalResult::Number(leftNum.numberValue * rightNum.numberValue);
        }
        case BinaryOp::DIVIDE: {
            EvalResult leftNum = left.toNumber();
            EvalResult rightNum = right.toNumber();
            if (leftNum.isError())
                return leftNum;
            if (rightNum.isError())
                return rightNum;
            if (rightNum.numberValue == 0.0) {
                return EvalResult::Error(CellError::DIV);
            }
            return EvalResult::Number(leftNum.numberValue / rightNum.numberValue);
        }
        case BinaryOp::POWER: {
            EvalResult leftNum = left.toNumber();
            EvalResult rightNum = right.toNumber();
            if (leftNum.isError())
                return leftNum;
            if (rightNum.isError())
                return rightNum;
            double result = std::pow(leftNum.numberValue, rightNum.numberValue);
            if (std::isnan(result) || std::isinf(result)) {
                return EvalResult::Error(CellError::NUM);
            }
            return EvalResult::Number(result);
        }
        case BinaryOp::CONCAT: {
            EvalResult leftStr = left.toString();
            EvalResult rightStr = right.toString();
            if (leftStr.isError())
                return leftStr;
            if (rightStr.isError())
                return rightStr;
            return EvalResult::String(leftStr.stringValue + rightStr.stringValue);
        }
        case BinaryOp::EQUAL:
            return compareEqual(left, right);
        case BinaryOp::NOT_EQUAL: {
            EvalResult eq = compareEqual(left, right);
            if (eq.isError())
                return eq;
            return EvalResult::Boolean(!eq.boolValue);
        }
        case BinaryOp::LESS:
            return EvalResult::Boolean(compareValues(left, right) < 0);
        case BinaryOp::LESS_EQUAL:
            return EvalResult::Boolean(compareValues(left, right) <= 0);
        case BinaryOp::GREATER:
            return EvalResult::Boolean(compareValues(left, right) > 0);
        case BinaryOp::GREATER_EQUAL:
            return EvalResult::Boolean(compareValues(left, right) >= 0);
    }

    return EvalResult::Error(CellError::VALUE);
}

// Evaluate a unary operation
static EvalResult evaluateUnaryOp(const UnaryOpNode* node, EvalContext& ctx) {
    EvalResult operand = evaluate(node->operand.get(), ctx);
    if (operand.isError())
        return operand;

    EvalResult num = operand.toNumber();
    if (num.isError())
        return num;

    switch (node->op) {
        case UnaryOp::NEGATE:
            return EvalResult::Number(-num.numberValue);
        case UnaryOp::POSITIVE:
            return EvalResult::Number(num.numberValue);
    }

    return EvalResult::Error(CellError::VALUE);
}

// Main evaluation function
EvalResult evaluate(const ASTNode* node, EvalContext& ctx) {
    if (!node) {
        return EvalResult::Error(CellError::VALUE);
    }

    switch (node->type) {
        // Literals
        case ASTNodeType::NUMBER_LITERAL:
        case ASTNodeType::STRING_LITERAL:
        case ASTNodeType::BOOLEAN_LITERAL:
            return evaluateLiteral(node);

        // References
        case ASTNodeType::CELL_REF:
            return evaluateCellRef(static_cast<const CellRefNode*>(node), ctx);

        // Operators
        case ASTNodeType::BINARY_OP:
            return evaluateBinaryOp(static_cast<const BinaryOpNode*>(node), ctx);

        case ASTNodeType::UNARY_OP:
            return evaluateUnaryOp(static_cast<const UnaryOpNode*>(node), ctx);

        // Range references - these don't evaluate to a single value
        // They're consumed by functions like SUM, AVERAGE, etc.
        case ASTNodeType::RANGE_REF:
        case ASTNodeType::COLUMN_REF:
        case ASTNodeType::ROW_REF:
        case ASTNodeType::COLUMN_RANGE_REF:
        case ASTNodeType::ROW_RANGE_REF:
            // For now, ranges in a scalar context return an error
            // Functions will handle ranges specially
            return EvalResult::Error(CellError::VALUE);

        // Named references
        case ASTNodeType::NAMED_REF:
            // TODO: Look up named range and evaluate
            return EvalResult::Error(CellError::NAME);

        // Function calls
        case ASTNodeType::FUNCTION_CALL:
            // TODO: Implement function evaluation (Phase 3+)
            return EvalResult::Error(CellError::NAME);

        // Error node
        case ASTNodeType::ERROR_NODE:
            return EvalResult::Error(CellError::VALUE);
    }

    return EvalResult::Error(CellError::VALUE);
}

}  // namespace cells
