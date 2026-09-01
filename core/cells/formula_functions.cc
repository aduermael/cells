#include "core/cells/formula_functions.h"

#include <cctype>
#include <cstdlib>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_eval.h"
#include "core/cells/functions/fn_array.h"
#include "core/cells/functions/fn_conditional.h"
#include "core/cells/functions/fn_datetime.h"
#include "core/cells/functions/fn_engineering.h"
#include "core/cells/functions/fn_financial.h"
#include "core/cells/functions/fn_logic.h"
#include "core/cells/functions/fn_lookup.h"
#include "core/cells/functions/fn_math.h"
#include "core/cells/functions/fn_rand.h"
#include "core/cells/functions/fn_stats.h"
#include "core/cells/functions/fn_text.h"
#include "core/cells/model.h"

namespace cells {

// =============================================================================
// FunctionRegistry Implementation
// =============================================================================

FunctionRegistry& FunctionRegistry::instance() {
    static FunctionRegistry registry;
    return registry;
}

FunctionRegistry::FunctionRegistry() {
    // Initialize all built-in functions
    initializeBuiltinFunctions(*this);
}

std::string FunctionRegistry::toUpper(const std::string& s) {
    std::string result = s;
    for (char& c : result) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return result;
}

void FunctionRegistry::registerFunction(const std::string& name, FormulaFunction fn,
                                        bool isVolatile) {
    const std::string upperName = toUpper(name);
    functions_[upperName] = std::move(fn);
    if (isVolatile) {
        volatileFunctions_.insert(upperName);
    }
    // Create minimal info for functions registered without metadata
    if (functionInfo_.find(upperName) == functionInfo_.end()) {
        functionInfo_[upperName] = FunctionInfo{upperName, "()", "", "Other"};
    }
}

void FunctionRegistry::registerFunction(const std::string& name, FormulaFunction fn,
                                        const std::string& signature,
                                        const std::string& description, const std::string& category,
                                        bool isVolatile) {
    const std::string upperName = toUpper(name);
    functions_[upperName] = std::move(fn);
    functionInfo_[upperName] = FunctionInfo{upperName, signature, description, category};
    if (isVolatile) {
        volatileFunctions_.insert(upperName);
    }
}

void FunctionRegistry::registerAlias(const std::string& alias, const std::string& canonical) {
    const std::string upperAlias = toUpper(alias);
    const std::string upperCanon = toUpper(canonical);
    auto fnIt = functions_.find(upperCanon);
    if (fnIt == functions_.end()) {
        return;
    }
    functions_[upperAlias] = fnIt->second;
    auto infoIt = functionInfo_.find(upperCanon);
    if (infoIt != functionInfo_.end()) {
        FunctionInfo info = infoIt->second;
        info.name = upperAlias;
        functionInfo_[upperAlias] = std::move(info);
    }
    if (volatileFunctions_.count(upperCanon) > 0) {
        volatileFunctions_.insert(upperAlias);
    }
}

EvalResult FunctionRegistry::call(const std::string& name, const std::vector<const ASTNode*>& args,
                                  EvalContext& ctx) const {
    const std::string upperName = toUpper(name);
    auto it = functions_.find(upperName);
    if (it == functions_.end()) {
        return EvalResult::Error(CellError::NAME);
    }
    return it->second(args, ctx);
}

bool FunctionRegistry::exists(const std::string& name) const {
    return functions_.count(toUpper(name)) > 0;
}

bool FunctionRegistry::isVolatile(const std::string& name) const {
    return volatileFunctions_.count(toUpper(name)) > 0;
}

std::vector<std::string> FunctionRegistry::getFunctionNames() const {
    std::vector<std::string> names;
    names.reserve(functions_.size());
    for (const auto& [name, fn] : functions_) {
        (void)fn;  // Suppress unused warning
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<FunctionInfo> FunctionRegistry::getFunctionList() const {
    std::vector<FunctionInfo> list;
    list.reserve(functionInfo_.size());
    for (const auto& [name, info] : functionInfo_) {
        (void)name;  // Suppress unused warning
        list.push_back(info);
    }
    // Sort by name for consistent ordering
    std::sort(list.begin(), list.end(),
              [](const FunctionInfo& a, const FunctionInfo& b) { return a.name < b.name; });
    return list;
}

// =============================================================================
// Helper Functions
// =============================================================================

std::vector<EvalResult> expandArguments(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    std::vector<EvalResult> results;

    for (const ASTNode* arg : args) {
        const EvalResult result = evaluate(arg, ctx);

        if (result.isRange()) {
            // Expand range into individual cell values
            // Use the new overload that handles cross-sheet references
            std::vector<EvalResult> rangeValues = collectRangeValues(result, ctx);
            results.insert(results.end(), rangeValues.begin(), rangeValues.end());
        } else {
            results.push_back(result);
        }
    }

    return results;
}

std::pair<std::vector<double>, EvalResult> collectNumericValues(
    const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    std::vector<double> values;
    const std::vector<EvalResult> expanded = expandArguments(args, ctx);

    for (const EvalResult& val : expanded) {
        // Propagate errors
        if (val.isError()) {
            return {{}, val};
        }

        // Skip empty cells
        if (val.isEmpty()) {
            continue;
        }

        // Skip non-numeric values in ranges (Excel behavior)
        // But for direct arguments, try to coerce
        if (val.isString()) {
            // Try to parse string as number
            const EvalResult num = val.toNumber();
            if (num.isError()) {
                // In ranges, non-numeric strings are silently skipped
                // For direct arguments, this would be an error
                // Since we can't distinguish here, we'll skip (consistent with Excel)
                continue;
            }
            values.push_back(num.getNumber());
        } else if (val.isNumber()) {
            values.push_back(val.getNumber());
        } else if (val.isBoolean()) {
            // Booleans in ranges are typically skipped, but direct TRUE/FALSE count
            // For simplicity, include them (1 for TRUE, 0 for FALSE)
            values.push_back(val.getBoolean() ? 1.0 : 0.0);
        }
    }

    return {values, EvalResult::Empty()};
}

std::pair<std::vector<std::pair<double, double>>, EvalResult> collectPairedNumericValues(
    const ASTNode* arrayX, const ASTNode* arrayY, EvalContext& ctx) {
    const std::vector<EvalResult> xs = expandArguments({arrayX}, ctx);
    const std::vector<EvalResult> ys = expandArguments({arrayY}, ctx);

    for (const EvalResult& v : xs) {
        if (v.isError()) {
            return {{}, v};
        }
    }
    for (const EvalResult& v : ys) {
        if (v.isError()) {
            return {{}, v};
        }
    }
    if (xs.size() != ys.size()) {
        return {{}, EvalResult::Error(CellError::NA)};
    }

    std::vector<std::pair<double, double>> pairs;
    pairs.reserve(xs.size());
    for (size_t i = 0; i < xs.size(); ++i) {
        const EvalResult& x = xs[i];
        const EvalResult& y = ys[i];
        if (x.isEmpty() || y.isEmpty() || x.isString() || y.isString()) {
            continue;
        }
        double xv = 0.0;
        double yv = 0.0;
        if (x.isNumber()) {
            xv = x.getNumber();
        } else if (x.isBoolean()) {
            xv = x.getBoolean() ? 1.0 : 0.0;
        } else {
            continue;
        }
        if (y.isNumber()) {
            yv = y.getNumber();
        } else if (y.isBoolean()) {
            yv = y.getBoolean() ? 1.0 : 0.0;
        } else {
            continue;
        }
        pairs.emplace_back(xv, yv);
    }
    return {std::move(pairs), EvalResult::Empty()};
}

EvalResult evaluateAsNumber(const ASTNode* arg, EvalContext& ctx) {
    EvalResult result = evaluate(arg, ctx);
    if (result.isError()) {
        return result;
    }
    return result.toNumber();
}

EvalResult evaluateAsString(const ASTNode* arg, EvalContext& ctx) {
    EvalResult result = evaluate(arg, ctx);
    if (result.isError()) {
        return result;
    }
    return result.toString();
}

EvalResult evaluateAsBoolean(const ASTNode* arg, EvalContext& ctx) {
    EvalResult result = evaluate(arg, ctx);
    if (result.isError()) {
        return result;
    }
    return result.toBoolean();
}

namespace {

std::string asciiLowerCopy(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool wildcardMatchAt(const std::string& text, const std::string& pattern, size_t ti, size_t pi) {
    if (pi == pattern.size()) {
        return ti == text.size();
    }
    if (pattern[pi] == '~' && pi + 1 < pattern.size()) {
        if (ti >= text.size() || text[ti] != pattern[pi + 1]) {
            return false;
        }
        return wildcardMatchAt(text, pattern, ti + 1, pi + 2);
    }
    if (pattern[pi] == '*') {
        if (wildcardMatchAt(text, pattern, ti, pi + 1)) {
            return true;
        }
        if (ti < text.size() && wildcardMatchAt(text, pattern, ti + 1, pi)) {
            return true;
        }
        return false;
    }
    if (pattern[pi] == '?') {
        if (ti >= text.size()) {
            return false;
        }
        return wildcardMatchAt(text, pattern, ti + 1, pi + 1);
    }
    if (ti >= text.size() || text[ti] != pattern[pi]) {
        return false;
    }
    return wildcardMatchAt(text, pattern, ti + 1, pi + 1);
}

}  // namespace

bool excelWildcardMatch(const std::string& text, const std::string& pattern) {
    return wildcardMatchAt(asciiLowerCopy(text), asciiLowerCopy(pattern), 0, 0);
}

namespace {

EvalResult cellValueToGridCell(const Cell* cell) {
    if (!cell) {
        return EvalResult::Empty();
    }
    const CellValue& val = cell->value;
    switch (val.type) {
        case CellValueType::NUMBER:
            return EvalResult::Number(val.asNumber());
        case CellValueType::STRING:
            if (val.raw.empty()) {
                return EvalResult::Empty();
            }
            return EvalResult::String(val.asString());
        case CellValueType::BOOLEAN:
            return EvalResult::Boolean(val.asBoolean());
        case CellValueType::ERROR:
            return EvalResult::Error(val.error);
        default:
            return EvalResult::Empty();
    }
}

}  // namespace

std::pair<std::vector<std::vector<EvalResult>>, EvalResult> collectAs2D(const EvalResult& value,
                                                                        EvalContext& ctx) {
    if (value.isError()) {
        return {{}, value};
    }
    if (value.isArray()) {
        return {value.getArray(), EvalResult::Empty()};
    }
    if (!value.isRange()) {
        return {{{value}}, EvalResult::Empty()};
    }

    Sheet* sheet = value.targetSheet != nullptr ? value.targetSheet : ctx.sheet;
    if (sheet == nullptr) {
        return {{}, EvalResult::Error(CellError::VALUE)};
    }

    const RangeBounds& bounds = value.getRangeBounds();
    if (bounds.type != RangeType::CELL_RANGE) {
        return {{}, EvalResult::Error(CellError::VALUE)};
    }

    const Axis* startCol = sheet->getColumn(bounds.startColId);
    const Axis* endCol = sheet->getColumn(bounds.endColId);
    if (startCol == nullptr || endCol == nullptr) {
        return {{}, EvalResult::Error(CellError::REF)};
    }

    const uint32_t startColPos =
        startCol->position <= endCol->position ? startCol->position : endCol->position;
    const uint32_t cols = static_cast<uint32_t>(
        std::abs(static_cast<int>(endCol->position) - static_cast<int>(startCol->position)) + 1);
    const uint32_t rows = static_cast<uint32_t>(bounds.endRowPos - bounds.startRowPos + 1);

    std::vector<std::vector<EvalResult>> result;
    result.reserve(rows);
    for (uint32_t r = 0; r < rows; ++r) {
        std::vector<EvalResult> row;
        row.reserve(cols);
        for (uint32_t c = 0; c < cols; ++c) {
            const Axis* targetCol = sheet->getColumnByPosition(startColPos + c);
            const Axis* targetRow = sheet->getRowByPosition(bounds.startRowPos + r);
            if (targetCol == nullptr || targetRow == nullptr) {
                row.push_back(EvalResult::Error(CellError::REF));
                continue;
            }
            row.push_back(cellValueToGridCell(sheet->getCellAt(targetCol->id, targetRow->id)));
        }
        result.push_back(std::move(row));
    }
    return {std::move(result), EvalResult::Empty()};
}

std::pair<std::vector<std::vector<EvalResult>>, EvalResult> evaluateAs2D(const ASTNode* arg,
                                                                         EvalContext& ctx) {
    return collectAs2D(evaluate(arg, ctx), ctx);
}

// =============================================================================
// Register Built-in Functions
// =============================================================================

void initializeBuiltinFunctions(FunctionRegistry& registry) {
    // Register functions from individual modules
    registerArrayFunctions(registry);
    registerMathFunctions(registry);
    registerConditionalFunctions(registry);
    registerLogicFunctions(registry);
    registerTextFunctions(registry);
    registerDateTimeFunctions(registry);
    registerRandFunctions(registry);
    registerStatsFunctions(registry);
    registerLookupFunctions(registry);
    registerEngineeringFunctions(registry);
    registerFinancialFunctions(registry);
}

}  // namespace cells
