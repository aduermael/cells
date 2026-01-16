#include "core/cells/formula_functions.h"

#include <cctype>

#include <algorithm>
#include <string>
#include <vector>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_eval.h"
#include "core/cells/functions/fn_array.h"
#include "core/cells/functions/fn_datetime.h"
#include "core/cells/functions/fn_logic.h"
#include "core/cells/functions/fn_lookup.h"
#include "core/cells/functions/fn_math.h"
#include "core/cells/functions/fn_rand.h"
#include "core/cells/functions/fn_stats.h"
#include "core/cells/functions/fn_text.h"

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

// =============================================================================
// Register Built-in Functions
// =============================================================================

void initializeBuiltinFunctions(FunctionRegistry& registry) {
    // Register functions from individual modules
    registerArrayFunctions(registry);
    registerMathFunctions(registry);
    registerLogicFunctions(registry);
    registerTextFunctions(registry);
    registerDateTimeFunctions(registry);
    registerRandFunctions(registry);
    registerStatsFunctions(registry);
    registerLookupFunctions(registry);
}

}  // namespace cells
