#include "core/cells/formula_resolver.h"

#include <algorithm>
#include <sstream>

namespace cells {

// ===========================================================================
// FormulaResolver
// ===========================================================================

FormulaResolver::FormulaResolver(Workbook& workbook, Sheet& sheet, NamedRangeRegistry* namedRanges)
    : _workbook(workbook), _sheet(sheet), _namedRanges(namedRanges) {}

ResolveResult FormulaResolver::resolve(ASTNode* ast) {
    if (ast == nullptr) {
        return ResolveResult::error("Null AST");
    }
    return resolveNode(ast);
}

std::vector<ReferenceInfo> FormulaResolver::extractReferences(const ASTNode* ast) const {
    std::vector<ReferenceInfo> refs;
    if (ast != nullptr) {
        extractReferencesFromNode(ast, refs);
    }
    return refs;
}

bool FormulaResolver::containsVolatileFunction(const ASTNode* ast) {
    if (ast == nullptr) {
        return false;
    }

    switch (ast->type) {
        case ASTNodeType::FUNCTION_CALL: {
            auto* func = static_cast<const FunctionCallNode*>(ast);
            if (func->isVolatile || FunctionCallNode::isVolatileFunction(func->name)) {
                return true;
            }
            for (const auto& arg : func->args) {
                if (containsVolatileFunction(arg.get())) {
                    return true;
                }
            }
            return false;
        }
        case ASTNodeType::BINARY_OP: {
            auto* binOp = static_cast<const BinaryOpNode*>(ast);
            return containsVolatileFunction(binOp->left.get()) ||
                   containsVolatileFunction(binOp->right.get());
        }
        case ASTNodeType::UNARY_OP: {
            auto* unaryOp = static_cast<const UnaryOpNode*>(ast);
            return containsVolatileFunction(unaryOp->operand.get());
        }
        default:
            return false;
    }
}

ResolveResult FormulaResolver::resolveNode(ASTNode* node) {
    if (node == nullptr) {
        return ResolveResult::ok();
    }

    switch (node->type) {
        case ASTNodeType::NUMBER_LITERAL:
        case ASTNodeType::STRING_LITERAL:
        case ASTNodeType::BOOLEAN_LITERAL:
            return ResolveResult::ok();

        case ASTNodeType::CELL_REF:
            return resolveCellRef(static_cast<CellRefNode*>(node));

        case ASTNodeType::RANGE_REF:
            return resolveRangeRef(static_cast<RangeRefNode*>(node));

        case ASTNodeType::COLUMN_REF:
            return resolveColumnRef(static_cast<ColumnRefNode*>(node));

        case ASTNodeType::ROW_REF:
            return resolveRowRef(static_cast<RowRefNode*>(node));

        case ASTNodeType::COLUMN_RANGE_REF:
            return resolveColumnRangeRef(static_cast<ColumnRangeRefNode*>(node));

        case ASTNodeType::ROW_RANGE_REF:
            return resolveRowRangeRef(static_cast<RowRangeRefNode*>(node));

        case ASTNodeType::NAMED_REF:
            return resolveNamedRef(static_cast<NamedRefNode*>(node));

        case ASTNodeType::BINARY_OP:
            return resolveBinaryOp(static_cast<BinaryOpNode*>(node));

        case ASTNodeType::UNARY_OP:
            return resolveUnaryOp(static_cast<UnaryOpNode*>(node));

        case ASTNodeType::FUNCTION_CALL:
            return resolveFunctionCall(static_cast<FunctionCallNode*>(node));

        case ASTNodeType::ERROR_NODE:
            // Error nodes are OK - they just represent parse errors
            return ResolveResult::ok();
    }

    return ResolveResult::error("Unknown AST node type", node->position);
}

ResolveResult FormulaResolver::resolveCellRef(CellRefNode* node) {
    // Get the target sheet
    Sheet* targetSheet = getTargetSheet(node->sheetName);
    if (targetSheet == nullptr) {
        return ResolveResult::error("Sheet not found: " + node->sheetName, node->position);
    }

    // Convert column name to position (0-indexed)
    const int32_t colPos = Sheet::columnNameToPosition(node->column);
    if (colPos < 0) {
        return ResolveResult::error("Invalid column: " + node->column, node->position);
    }

    // Row is 1-indexed in the AST, convert to 0-indexed position
    if (node->row < 1) {
        return ResolveResult::error("Invalid row: " + std::to_string(node->row), node->position);
    }
    const auto rowPos = static_cast<uint32_t>(node->row - 1);

    // Get or create the column and row axes
    const Axis* col = targetSheet->getOrCreateColumnByPosition(static_cast<uint32_t>(colPos));
    const Axis* row = targetSheet->getOrCreateRowByPosition(rowPos);

    // Get or create the cell at this position
    const Cell* cell = targetSheet->getOrCreateCellAt(col->id, row->id);

    // Store the cell ID
    node->cellId = cell->id.toString();

    return ResolveResult::ok();
}

ResolveResult FormulaResolver::resolveRangeRef(RangeRefNode* node) {
    // Resolve both corner cells
    auto result = resolveCellRef(node->topLeft.get());
    if (!result.success) {
        return result;
    }

    return resolveCellRef(node->bottomRight.get());
}

ResolveResult FormulaResolver::resolveColumnRef(ColumnRefNode* node) {
    // Get the target sheet
    Sheet* targetSheet = getTargetSheet(node->sheetName);
    if (targetSheet == nullptr) {
        return ResolveResult::error("Sheet not found: " + node->sheetName, node->position);
    }

    // Convert column name to position (0-indexed)
    const int32_t colPos = Sheet::columnNameToPosition(node->column);
    if (colPos < 0) {
        return ResolveResult::error("Invalid column: " + node->column, node->position);
    }

    // Get or create the column axis
    const Axis* col = targetSheet->getOrCreateColumnByPosition(static_cast<uint32_t>(colPos));

    // Store the column ID
    node->columnId = col->id.toString();

    return ResolveResult::ok();
}

ResolveResult FormulaResolver::resolveRowRef(RowRefNode* node) {
    // Get the target sheet
    Sheet* targetSheet = getTargetSheet(node->sheetName);
    if (targetSheet == nullptr) {
        return ResolveResult::error("Sheet not found: " + node->sheetName, node->position);
    }

    // Row is 1-indexed in the AST, convert to 0-indexed position
    if (node->row < 1) {
        return ResolveResult::error("Invalid row: " + std::to_string(node->row), node->position);
    }
    const auto rowPos = static_cast<uint32_t>(node->row - 1);

    // Get or create the row axis
    const Axis* row = targetSheet->getOrCreateRowByPosition(rowPos);

    // Store the row ID
    node->rowId = row->id.toString();

    return ResolveResult::ok();
}

ResolveResult FormulaResolver::resolveColumnRangeRef(ColumnRangeRefNode* node) {
    // Get the target sheet
    Sheet* targetSheet = getTargetSheet(node->sheetName);
    if (targetSheet == nullptr) {
        return ResolveResult::error("Sheet not found: " + node->sheetName, node->position);
    }

    // Convert column names to positions
    const int32_t startColPos = Sheet::columnNameToPosition(node->startColumn);
    const int32_t endColPos = Sheet::columnNameToPosition(node->endColumn);

    if (startColPos < 0) {
        return ResolveResult::error("Invalid column: " + node->startColumn, node->position);
    }
    if (endColPos < 0) {
        return ResolveResult::error("Invalid column: " + node->endColumn, node->position);
    }

    // Get or create the column axes
    const Axis* startCol =
        targetSheet->getOrCreateColumnByPosition(static_cast<uint32_t>(startColPos));
    const Axis* endCol = targetSheet->getOrCreateColumnByPosition(static_cast<uint32_t>(endColPos));

    // Store the column IDs
    node->startColumnId = startCol->id.toString();
    node->endColumnId = endCol->id.toString();

    return ResolveResult::ok();
}

ResolveResult FormulaResolver::resolveRowRangeRef(RowRangeRefNode* node) {
    // Get the target sheet
    Sheet* targetSheet = getTargetSheet(node->sheetName);
    if (targetSheet == nullptr) {
        return ResolveResult::error("Sheet not found: " + node->sheetName, node->position);
    }

    // Rows are 1-indexed in the AST
    if (node->startRow < 1 || node->endRow < 1) {
        return ResolveResult::error("Invalid row number", node->position);
    }
    const auto startRowPos = static_cast<uint32_t>(node->startRow - 1);
    const auto endRowPos = static_cast<uint32_t>(node->endRow - 1);

    // Get or create the row axes
    const Axis* startRow = targetSheet->getOrCreateRowByPosition(startRowPos);
    const Axis* endRow = targetSheet->getOrCreateRowByPosition(endRowPos);

    // Store the row IDs
    node->startRowId = startRow->id.toString();
    node->endRowId = endRow->id.toString();

    return ResolveResult::ok();
}

ResolveResult FormulaResolver::resolveNamedRef(NamedRefNode* node) {
    if (_namedRanges == nullptr) {
        return ResolveResult::error("Named range not found: " + node->name, node->position);
    }

    // Resolve the named range using the registry
    const NamedRange* nr = _namedRanges->resolve(node->name, _sheet.id);
    if (nr == nullptr) {
        return ResolveResult::error("Named range not found: " + node->name, node->position);
    }

    // Update scope based on what was found
    node->scope = (nr->scope == NamedRangeScope::WORKBOOK) ? ASTNamedRangeScope::WORKBOOK
                                                           : ASTNamedRangeScope::SHEET;

    return ResolveResult::ok();
}

ResolveResult FormulaResolver::resolveBinaryOp(BinaryOpNode* node) {
    auto result = resolveNode(node->left.get());
    if (!result.success) {
        return result;
    }
    return resolveNode(node->right.get());
}

ResolveResult FormulaResolver::resolveUnaryOp(UnaryOpNode* node) {
    return resolveNode(node->operand.get());
}

ResolveResult FormulaResolver::resolveFunctionCall(FunctionCallNode* node) {
    // Mark if this is a volatile function
    node->isVolatile = FunctionCallNode::isVolatileFunction(node->name);

    // Resolve all arguments
    for (auto& arg : node->args) {
        auto result = resolveNode(arg.get());
        if (!result.success) {
            return result;
        }
    }
    return ResolveResult::ok();
}

Sheet* FormulaResolver::getTargetSheet(const std::string& sheetName) {
    if (sheetName.empty()) {
        return &_sheet;
    }

    // Look up sheet by name in the workbook
    for (const auto& sheet : _workbook.sheets) {
        if (sheet->name == sheetName) {
            return sheet.get();
        }
    }

    return nullptr;
}

void FormulaResolver::extractReferencesFromNode(const ASTNode* node,
                                                std::vector<ReferenceInfo>& refs) const {
    if (node == nullptr) {
        return;
    }

    switch (node->type) {
        case ASTNodeType::CELL_REF: {
            auto* cellRef = static_cast<const CellRefNode*>(node);
            ReferenceInfo info;
            info.type = ReferenceInfo::Type::CELL;
            info.sourcePosition = cellRef->position;
            info.cellId = ID(cellRef->cellId);
            refs.push_back(info);
            break;
        }
        case ASTNodeType::RANGE_REF: {
            auto* rangeRef = static_cast<const RangeRefNode*>(node);
            ReferenceInfo info;
            info.type = ReferenceInfo::Type::RANGE;
            info.sourcePosition = rangeRef->position;
            info.topLeftCellId = ID(rangeRef->topLeft->cellId);
            info.bottomRightCellId = ID(rangeRef->bottomRight->cellId);
            refs.push_back(info);
            break;
        }
        case ASTNodeType::COLUMN_REF: {
            auto* colRef = static_cast<const ColumnRefNode*>(node);
            ReferenceInfo info;
            info.type = ReferenceInfo::Type::COLUMN;
            info.sourcePosition = colRef->position;
            info.axisId = ID(colRef->columnId);
            refs.push_back(info);
            break;
        }
        case ASTNodeType::ROW_REF: {
            auto* rowRef = static_cast<const RowRefNode*>(node);
            ReferenceInfo info;
            info.type = ReferenceInfo::Type::ROW;
            info.sourcePosition = rowRef->position;
            info.axisId = ID(rowRef->rowId);
            refs.push_back(info);
            break;
        }
        case ASTNodeType::COLUMN_RANGE_REF: {
            auto* colRangeRef = static_cast<const ColumnRangeRefNode*>(node);
            ReferenceInfo info;
            info.type = ReferenceInfo::Type::COLUMN_RANGE;
            info.sourcePosition = colRangeRef->position;
            info.startAxisId = ID(colRangeRef->startColumnId);
            info.endAxisId = ID(colRangeRef->endColumnId);
            refs.push_back(info);
            break;
        }
        case ASTNodeType::ROW_RANGE_REF: {
            auto* rowRangeRef = static_cast<const RowRangeRefNode*>(node);
            ReferenceInfo info;
            info.type = ReferenceInfo::Type::ROW_RANGE;
            info.sourcePosition = rowRangeRef->position;
            info.startAxisId = ID(rowRangeRef->startRowId);
            info.endAxisId = ID(rowRangeRef->endRowId);
            refs.push_back(info);
            break;
        }
        case ASTNodeType::NAMED_REF: {
            auto* namedRef = static_cast<const NamedRefNode*>(node);
            ReferenceInfo info;
            info.type = ReferenceInfo::Type::NAMED;
            info.sourcePosition = namedRef->position;
            info.namedRangeName = namedRef->name;
            refs.push_back(info);
            break;
        }
        case ASTNodeType::BINARY_OP: {
            auto* binOp = static_cast<const BinaryOpNode*>(node);
            extractReferencesFromNode(binOp->left.get(), refs);
            extractReferencesFromNode(binOp->right.get(), refs);
            break;
        }
        case ASTNodeType::UNARY_OP: {
            auto* unaryOp = static_cast<const UnaryOpNode*>(node);
            extractReferencesFromNode(unaryOp->operand.get(), refs);
            break;
        }
        case ASTNodeType::FUNCTION_CALL: {
            auto* funcCall = static_cast<const FunctionCallNode*>(node);
            for (const auto& arg : funcCall->args) {
                extractReferencesFromNode(arg.get(), refs);
            }
            break;
        }
        case ASTNodeType::ERROR_NODE: {
            auto* errorNode = static_cast<const ErrorNode*>(node);
            for (const auto& child : errorNode->partialChildren) {
                extractReferencesFromNode(child.get(), refs);
            }
            break;
        }
        default:
            break;
    }
}

// ===========================================================================
// FormulaDisplayConverter
// ===========================================================================

FormulaDisplayConverter::FormulaDisplayConverter(const Sheet& sheet) : _sheet(sheet) {}

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
    std::string result;

    // Add sheet prefix if present
    if (!node->sheetName.empty()) {
        result += node->sheetName + "!";
    }

    // If we have a resolved cellId, look up the current position
    if (!node->cellId.empty()) {
        const ID cellIdObj(node->cellId);
        // Find the cell to get its column and row
        for (const auto& [id, cell] : _sheet.cells) {
            if (id == cellIdObj) {
                // Get column position
                const Axis* col = _sheet.columns.count(cell->colId) != 0u
                                      ? _sheet.columns.at(cell->colId).get()
                                      : nullptr;
                const Axis* row = _sheet.rows.count(cell->rowId) != 0u
                                      ? _sheet.rows.at(cell->rowId).get()
                                      : nullptr;

                if (col != nullptr && row != nullptr) {
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
                break;
            }
        }
    }

    // Fall back to original column/row
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

std::string FormulaDisplayConverter::rangeRefToString(const RangeRefNode* node) const {
    return cellRefToString(node->topLeft.get()) + ":" + cellRefToString(node->bottomRight.get());
}

std::string FormulaDisplayConverter::columnRefToString(const ColumnRefNode* node) const {
    std::string result;

    if (!node->sheetName.empty()) {
        result += node->sheetName + "!";
    }

    // If we have a resolved columnId, look up the current position
    if (!node->columnId.empty()) {
        const ID colIdObj(node->columnId);
        for (const auto& [id, col] : _sheet.columns) {
            if (id == colIdObj) {
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
    }

    // Fall back to original
    if (node->absolute) {
        result += "$";
    }
    result += node->column + ":" + node->column;

    return result;
}

std::string FormulaDisplayConverter::rowRefToString(const RowRefNode* node) const {
    std::string result;

    if (!node->sheetName.empty()) {
        result += node->sheetName + "!";
    }

    // If we have a resolved rowId, look up the current position
    if (!node->rowId.empty()) {
        const ID rowIdObj(node->rowId);
        for (const auto& [id, row] : _sheet.rows) {
            if (id == rowIdObj) {
                if (node->absolute) {
                    result += "$";
                }
                const std::string rowNum =
                    std::to_string(row->position + 1);  // Convert to 1-indexed
                result += rowNum;
                result += ":";
                result += rowNum;
                return result;
            }
        }
    }

    // Fall back to original
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

    if (!node->sheetName.empty()) {
        result += node->sheetName + "!";
    }

    std::string startCol = node->startColumn;
    std::string endCol = node->endColumn;

    // If resolved, look up current positions
    if (!node->startColumnId.empty()) {
        const ID startColIdObj(node->startColumnId);
        for (const auto& [id, col] : _sheet.columns) {
            if (id == startColIdObj) {
                startCol = Sheet::positionToColumnName(col->position);
                break;
            }
        }
    }
    if (!node->endColumnId.empty()) {
        const ID endColIdObj(node->endColumnId);
        for (const auto& [id, col] : _sheet.columns) {
            if (id == endColIdObj) {
                endCol = Sheet::positionToColumnName(col->position);
                break;
            }
        }
    }

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

    if (!node->sheetName.empty()) {
        result += node->sheetName + "!";
    }

    int startRow = node->startRow;
    int endRow = node->endRow;

    // If resolved, look up current positions
    if (!node->startRowId.empty()) {
        const ID startRowIdObj(node->startRowId);
        for (const auto& [id, row] : _sheet.rows) {
            if (id == startRowIdObj) {
                startRow = static_cast<int>(row->position + 1);  // Convert to 1-indexed
                break;
            }
        }
    }
    if (!node->endRowId.empty()) {
        const ID endRowIdObj(node->endRowId);
        for (const auto& [id, row] : _sheet.rows) {
            if (id == endRowIdObj) {
                endRow = static_cast<int>(row->position + 1);  // Convert to 1-indexed
                break;
            }
        }
    }

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
    // For error nodes, try to reconstruct what we can from partial children
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
