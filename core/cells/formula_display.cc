#include "core/cells/formula_display.h"

#include <cctype>

#include <algorithm>
#include <sstream>

#include "core/cells/model.h"

namespace cells {

FormulaDisplayConverter::FormulaDisplayConverter(const Sheet& sheet, const Workbook* workbook)
    : _sheet(sheet), _workbook(workbook) {}

// Helper: check if sheet name needs quoting (contains spaces, quotes, !, or [)
static bool sheetNameNeedsQuotes(const std::string& name) {
    for (const char c : name) {
        if (c == ' ' || c == '\'' || c == '!' || c == '[') {
            return true;
        }
    }
    return false;
}

// Helper: format sheet name with proper quoting for A1 notation
static std::string formatSheetName(const std::string& name) {
    if (!sheetNameNeedsQuotes(name)) {
        return name + "!";
    }
    // Quote the name and escape single quotes by doubling them
    std::string result = "'";
    for (const char c : name) {
        if (c == '\'') {
            result += "''";  // Escape ' as ''
        } else {
            result += c;
        }
    }
    result += "'!";
    return result;
}

std::string FormulaDisplayConverter::getSheetPrefix(const std::string& sheetId,
                                                    const std::string& sheetName) const {
    // Prefer sheetId lookup (more stable across renames)
    if (!sheetId.empty() && _workbook != nullptr) {
        const ID sheetIdObj(sheetId);
        const Sheet* sheet = _workbook->getSheet(sheetIdObj);
        if (sheet != nullptr) {
            return formatSheetName(sheet->name);
        }
    }

    // Fall back to sheetName
    if (!sheetName.empty()) {
        return formatSheetName(sheetName);
    }

    return "";
}

std::string FormulaDisplayConverter::toDisplayString(const ASTNode* ast) const {
    if (ast == nullptr) {
        return "";
    }
    return "=" + nodeToString(ast);
}

std::string FormulaDisplayConverter::nodeToString(const ASTNode* node) const {
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
            return "\"" + strNode->value + "\"";
        }
        case ASTNodeType::BOOLEAN_LITERAL: {
            auto* boolNode = static_cast<const BooleanLiteralNode*>(node);
            return boolNode->value ? "TRUE" : "FALSE";
        }
        case ASTNodeType::CELL_REF:
            return cellRefToString(static_cast<const CellRefNode*>(node));
        case ASTNodeType::RANGE_REF:
            return rangeRefToString(static_cast<const RangeRefNode*>(node));
        case ASTNodeType::COLUMN_REF:
            return columnRefToString(static_cast<const ColumnRefNode*>(node));
        case ASTNodeType::ROW_REF:
            return rowRefToString(static_cast<const RowRefNode*>(node));
        case ASTNodeType::COLUMN_RANGE_REF:
            return columnRangeRefToString(static_cast<const ColumnRangeRefNode*>(node));
        case ASTNodeType::ROW_RANGE_REF:
            return rowRangeRefToString(static_cast<const RowRangeRefNode*>(node));
        case ASTNodeType::NAMED_REF:
            return namedRefToString(static_cast<const NamedRefNode*>(node));
        case ASTNodeType::SPILL_RANGE_REF:
            return spillRangeRefToString(static_cast<const SpillRangeRefNode*>(node));
        case ASTNodeType::BINARY_OP:
            return binaryOpToString(static_cast<const BinaryOpNode*>(node));
        case ASTNodeType::UNARY_OP:
            return unaryOpToString(static_cast<const UnaryOpNode*>(node));
        case ASTNodeType::FUNCTION_CALL:
            return functionCallToString(static_cast<const FunctionCallNode*>(node));
        case ASTNodeType::ERROR_NODE:
            return errorNodeToString(static_cast<const ErrorNode*>(node));
    }

    return "";
}

std::string FormulaDisplayConverter::cellRefToString(const CellRefNode* node) const {
    return cellRefToStringInternal(node, false);
}

std::string FormulaDisplayConverter::cellRefToStringInternal(const CellRefNode* node,
                                                             bool suppressSheetPrefix) const {
    std::string result;
    std::string sheetPrefix;

    // Determine which sheet to look up the cell on
    const Sheet* lookupSheet = &_sheet;

    // If sheetId is explicitly set, use it (existing storage format)
    if (!node->sheetId.empty() && _workbook != nullptr) {
        const ID sheetIdObj(node->sheetId);
        const Sheet* crossSheet = _workbook->getSheet(sheetIdObj);
        if (crossSheet != nullptr) {
            lookupSheet = crossSheet;
            // Add sheet prefix since it's explicitly a cross-sheet ref (unless suppressed)
            if (!suppressSheetPrefix) {
                sheetPrefix = getSheetPrefix(node->sheetId, node->sheetName);
            }
        }
    } else if (!node->sheetName.empty() && !suppressSheetPrefix) {
        // Legacy sheetName reference
        sheetPrefix = getSheetPrefix("", node->sheetName);
    }

    // If we have a resolved cellId, look up the current position
    if (!node->cellId.empty() && _workbook != nullptr) {
        const ID cellIdObj(node->cellId);
        const Cell* foundCell = nullptr;

        // For simplified storage: search all sheets if sheetId is empty
        if (node->sheetId.empty()) {
            auto [cell, sheet] = _workbook->findCell(cellIdObj);
            if (cell && sheet) {
                foundCell = cell;
                lookupSheet = sheet;
                // Check if this is a cross-sheet reference by comparing the cell's column sheetId
                // with the formula's sheet. Use workbook-level lookup for reliability.
                const Axis* col = _workbook->getColumn(foundCell->colId);
                if (col != nullptr && col->sheetId != _sheet.id && !suppressSheetPrefix) {
                    // Cross-sheet reference - add sheet prefix
                    sheetPrefix = formatSheetName(lookupSheet->name);
                }
            }
        } else {
            // Explicit sheet - look up directly
            foundCell = _workbook->getCell(cellIdObj);
        }

        // If we found the cell, get its column and row positions from workbook storage
        if (foundCell) {
            const Axis* col = _workbook->getColumn(foundCell->colId);
            const Axis* row = _workbook->getRow(foundCell->rowId);

            if (col != nullptr && row != nullptr) {
                result += sheetPrefix;
                if (node->colAbsolute) {
                    result += "$";
                }
                result += Sheet::positionToColumnName(col->position);
                if (node->rowAbsolute) {
                    result += "$";
                }
                result += std::to_string(row->position + 1);  // Convert to 1-indexed
                return result;
            }
        }
    }

    // Fall back to original column/row (uppercase column for normalization)
    result += sheetPrefix;
    if (node->colAbsolute) {
        result += "$";
    }
    std::string upperColumn = node->column;
    std::transform(upperColumn.begin(), upperColumn.end(), upperColumn.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    result += upperColumn;
    if (node->rowAbsolute) {
        result += "$";
    }
    result += std::to_string(node->row);

    return result;
}

std::string FormulaDisplayConverter::rangeRefToString(const RangeRefNode* node) const {
    // Get the first cell reference with full sheet prefix handling
    const std::string firstRef = cellRefToString(node->topLeft.get());

    // For the second cell, check if it's on the same sheet as the first
    // If so, omit the sheet prefix (Excel-like behavior: Sheet2!A1:A3, not Sheet2!A1:Sheet2!A3)
    const CellRefNode* topLeft = node->topLeft.get();
    const CellRefNode* bottomRight = node->bottomRight.get();

    // Helper to get sheet ID for a cell reference
    auto getSheetIdForRef = [this](const CellRefNode* ref) -> std::string {
        if (!ref->sheetId.empty()) {
            return ref->sheetId;
        }
        if (!ref->cellId.empty() && _workbook != nullptr) {
            const ID cellIdObj(ref->cellId);
            auto [cell, sheet] = _workbook->findCell(cellIdObj);
            if (sheet != nullptr) {
                return sheet->id.toString();
            }
        }
        // If no sheet can be determined, return the current sheet ID
        return _sheet.id.toString();
    };

    const std::string firstSheetId = getSheetIdForRef(topLeft);
    const std::string secondSheetId = getSheetIdForRef(bottomRight);

    // If both cells are on the same sheet, suppress the sheet prefix for the second cell
    const bool sameSheet = (firstSheetId == secondSheetId);
    const std::string secondRef = cellRefToStringInternal(bottomRight, sameSheet);

    return firstRef + ":" + secondRef;
}

std::string FormulaDisplayConverter::columnRefToString(const ColumnRefNode* node) const {
    std::string result;
    std::string sheetPrefix;

    // Determine which sheet to look up on
    const Sheet* lookupSheet = &_sheet;

    // If sheetId is explicitly set, use it (existing storage format)
    if (!node->sheetId.empty() && _workbook != nullptr) {
        const ID sheetIdObj(node->sheetId);
        const Sheet* crossSheet = _workbook->getSheet(sheetIdObj);
        if (crossSheet != nullptr) {
            lookupSheet = crossSheet;
            sheetPrefix = getSheetPrefix(node->sheetId, node->sheetName);
        }
    } else if (!node->sheetName.empty()) {
        sheetPrefix = getSheetPrefix("", node->sheetName);
    }

    // If we have a resolved columnId, look up the current position
    if (!node->columnId.empty() && _workbook != nullptr) {
        const ID colIdObj(node->columnId);

        // For simplified storage: search all sheets if sheetId is empty
        if (node->sheetId.empty()) {
            const Sheet* foundSheet = _workbook->findAxisSheet(colIdObj);
            if (foundSheet) {
                lookupSheet = foundSheet;
                // Check if cross-sheet by comparing column's sheetId with formula's sheet
                const Axis* colCheck = _workbook->getColumn(colIdObj);
                if (colCheck != nullptr && colCheck->sheetId != _sheet.id) {
                    sheetPrefix = formatSheetName(lookupSheet->name);
                }
            }
        }

        // Use workbook-level lookup for reliability
        const Axis* col = _workbook->getColumn(colIdObj);
        if (col != nullptr) {
            result += sheetPrefix;
            if (node->absolute) {
                result += "$";
            }
            const std::string colName = Sheet::positionToColumnName(col->position);
            result += colName;
            result += ":";
            result += colName;
            return result;
        }
    }

    // Fall back to original (uppercase column for normalization)
    result += sheetPrefix;
    if (node->absolute) {
        result += "$";
    }
    std::string upperColumn = node->column;
    std::transform(upperColumn.begin(), upperColumn.end(), upperColumn.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    result += upperColumn + ":" + upperColumn;

    return result;
}

std::string FormulaDisplayConverter::rowRefToString(const RowRefNode* node) const {
    std::string result;
    std::string sheetPrefix;

    // Determine which sheet to look up on
    const Sheet* lookupSheet = &_sheet;

    // If sheetId is explicitly set, use it (existing storage format)
    if (!node->sheetId.empty() && _workbook != nullptr) {
        const ID sheetIdObj(node->sheetId);
        const Sheet* crossSheet = _workbook->getSheet(sheetIdObj);
        if (crossSheet != nullptr) {
            lookupSheet = crossSheet;
            sheetPrefix = getSheetPrefix(node->sheetId, node->sheetName);
        }
    } else if (!node->sheetName.empty()) {
        sheetPrefix = getSheetPrefix("", node->sheetName);
    }

    // If we have a resolved rowId, look up the current position
    if (!node->rowId.empty() && _workbook != nullptr) {
        const ID rowIdObj(node->rowId);

        // For simplified storage: search all sheets if sheetId is empty
        if (node->sheetId.empty()) {
            const Sheet* foundSheet = _workbook->findAxisSheet(rowIdObj);
            if (foundSheet) {
                lookupSheet = foundSheet;
                // Check if cross-sheet by comparing row's sheetId with formula's sheet
                const Axis* rowCheck = _workbook->getRow(rowIdObj);
                if (rowCheck != nullptr && rowCheck->sheetId != _sheet.id) {
                    sheetPrefix = formatSheetName(lookupSheet->name);
                }
            }
        }

        // Use workbook-level lookup for reliability
        const Axis* row = _workbook->getRow(rowIdObj);
        if (row != nullptr) {
            result += sheetPrefix;
            if (node->absolute) {
                result += "$";
            }
            const std::string rowNum = std::to_string(row->position + 1);  // Convert to 1-indexed
            result += rowNum;
            result += ":";
            result += rowNum;
            return result;
        }
    }

    // Fall back to original
    result += sheetPrefix;
    if (node->absolute) {
        result += "$";
    }
    const std::string rowNum = std::to_string(node->row);
    result += rowNum;
    result += ":";
    result += rowNum;

    return result;
}

std::string FormulaDisplayConverter::columnRangeRefToString(const ColumnRangeRefNode* node) const {
    std::string result;
    std::string sheetPrefix;

    // Determine which sheet to look up on
    const Sheet* lookupSheet = &_sheet;

    // If sheetId is explicitly set, use it (existing storage format)
    if (!node->sheetId.empty() && _workbook != nullptr) {
        const ID sheetIdObj(node->sheetId);
        const Sheet* crossSheet = _workbook->getSheet(sheetIdObj);
        if (crossSheet != nullptr) {
            lookupSheet = crossSheet;
            sheetPrefix = getSheetPrefix(node->sheetId, node->sheetName);
        }
    } else if (!node->sheetName.empty()) {
        sheetPrefix = getSheetPrefix("", node->sheetName);
    }

    // For simplified storage: search all sheets if sheetId is empty
    if (node->sheetId.empty() && !node->startColumnId.empty() && _workbook != nullptr) {
        const ID startColIdObj(node->startColumnId);
        const Sheet* foundSheet = _workbook->findAxisSheet(startColIdObj);
        if (foundSheet) {
            lookupSheet = foundSheet;
            // Check if cross-sheet - use workbook-level lookup
            const Axis* colCheck = _workbook->getColumn(startColIdObj);
            if (colCheck != nullptr && colCheck->sheetId != _sheet.id) {
                sheetPrefix = formatSheetName(lookupSheet->name);
            }
        }
    }

    std::string startCol = node->startColumn;
    std::string endCol = node->endColumn;

    // If resolved, look up current positions from workbook storage
    if (!node->startColumnId.empty() && _workbook != nullptr) {
        const ID startColIdObj(node->startColumnId);
        const Axis* col = _workbook->getColumn(startColIdObj);
        if (col != nullptr) {
            startCol = Sheet::positionToColumnName(col->position);
        }
    }
    if (!node->endColumnId.empty() && _workbook != nullptr) {
        const ID endColIdObj(node->endColumnId);
        const Axis* col = _workbook->getColumn(endColIdObj);
        if (col != nullptr) {
            endCol = Sheet::positionToColumnName(col->position);
        }
    }

    // Normalize column names to uppercase (resolved lookups already uppercase)
    std::transform(startCol.begin(), startCol.end(), startCol.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    std::transform(endCol.begin(), endCol.end(), endCol.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    result += sheetPrefix;
    if (node->startAbsolute) {
        result += "$";
    }
    result += startCol;
    result += ":";
    if (node->endAbsolute) {
        result += "$";
    }
    result += endCol;

    return result;
}

std::string FormulaDisplayConverter::rowRangeRefToString(const RowRangeRefNode* node) const {
    std::string result;
    std::string sheetPrefix;

    // Determine which sheet to look up on
    const Sheet* lookupSheet = &_sheet;

    // If sheetId is explicitly set, use it (existing storage format)
    if (!node->sheetId.empty() && _workbook != nullptr) {
        const ID sheetIdObj(node->sheetId);
        const Sheet* crossSheet = _workbook->getSheet(sheetIdObj);
        if (crossSheet != nullptr) {
            lookupSheet = crossSheet;
            sheetPrefix = getSheetPrefix(node->sheetId, node->sheetName);
        }
    } else if (!node->sheetName.empty()) {
        sheetPrefix = getSheetPrefix("", node->sheetName);
    }

    // For simplified storage: search all sheets if sheetId is empty
    if (node->sheetId.empty() && !node->startRowId.empty() && _workbook != nullptr) {
        const ID startRowIdObj(node->startRowId);
        const Sheet* foundSheet = _workbook->findAxisSheet(startRowIdObj);
        if (foundSheet) {
            lookupSheet = foundSheet;
            // Check if cross-sheet - use workbook-level lookup
            const Axis* rowCheck = _workbook->getRow(startRowIdObj);
            if (rowCheck != nullptr && rowCheck->sheetId != _sheet.id) {
                sheetPrefix = formatSheetName(lookupSheet->name);
            }
        }
    }

    int startRow = node->startRow;
    int endRow = node->endRow;

    // If resolved, look up current positions from workbook storage
    if (!node->startRowId.empty() && _workbook != nullptr) {
        const ID startRowIdObj(node->startRowId);
        const Axis* row = _workbook->getRow(startRowIdObj);
        if (row != nullptr) {
            startRow = static_cast<int>(row->position + 1);  // Convert to 1-indexed
        }
    }
    if (!node->endRowId.empty() && _workbook != nullptr) {
        const ID endRowIdObj(node->endRowId);
        const Axis* row = _workbook->getRow(endRowIdObj);
        if (row != nullptr) {
            endRow = static_cast<int>(row->position + 1);  // Convert to 1-indexed
        }
    }

    result += sheetPrefix;
    if (node->startAbsolute) {
        result += "$";
    }
    result += std::to_string(startRow);
    result += ":";
    if (node->endAbsolute) {
        result += "$";
    }
    result += std::to_string(endRow);

    return result;
}

std::string FormulaDisplayConverter::namedRefToString(const NamedRefNode* node) const {
    return node->name;
}

std::string FormulaDisplayConverter::spillRangeRefToString(const SpillRangeRefNode* node) const {
    // Convert anchor cell ref to string and append #
    return cellRefToString(node->anchor.get()) + "#";
}

std::string FormulaDisplayConverter::binaryOpToString(const BinaryOpNode* node) const {
    std::string left = nodeToString(node->left.get());
    std::string right = nodeToString(node->right.get());

    // Add parentheses if needed for precedence
    if (needsParentheses(node, node->left.get(), false)) {
        left = "(" + left + ")";
    }
    if (needsParentheses(node, node->right.get(), true)) {
        right = "(" + right + ")";
    }

    return left + BinaryOpNode::opToString(node->op) + right;
}

std::string FormulaDisplayConverter::unaryOpToString(const UnaryOpNode* node) const {
    std::string operand = nodeToString(node->operand.get());

    // Unary operators may need parentheses around complex expressions
    if (node->operand->type == ASTNodeType::BINARY_OP) {
        operand = "(" + operand + ")";
    }

    return UnaryOpNode::opToString(node->op) + operand;
}

std::string FormulaDisplayConverter::functionCallToString(const FunctionCallNode* node) const {
    std::string result = node->name + "(";

    for (size_t i = 0; i < node->args.size(); ++i) {
        if (i > 0) {
            result += ",";
        }
        result += nodeToString(node->args[i].get());
    }

    result += ")";
    return result;
}

std::string FormulaDisplayConverter::errorNodeToString(const ErrorNode* node) const {
    // If rawText is available, return it (without leading "=" since toDisplayString adds it)
    if (!node->rawText.empty()) {
        // Strip leading "=" if present since toDisplayString adds it
        if (node->rawText[0] == '=') {
            return node->rawText.substr(1);
        }
        return node->rawText;
    }
    // For error nodes without rawText, try to reconstruct what we can from partial children
    std::string result = "#ERROR!";
    for (const auto& child : node->partialChildren) {
        result += nodeToString(child.get());
    }
    return result;
}

bool FormulaDisplayConverter::needsParentheses(const ASTNode* parent, const ASTNode* child,
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
