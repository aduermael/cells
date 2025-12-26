#ifndef CELLS_FORMULA_FUNCTIONS_H_
#define CELLS_FORMULA_FUNCTIONS_H_

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/cells/formula_eval.h"

namespace cells {

// Forward declaration
struct ASTNode;

// Function signature for formula functions
// Arguments are passed as AST nodes to allow lazy evaluation (for IF, AND, OR, etc.)
using FormulaFunction =
    std::function<EvalResult(const std::vector<const ASTNode*>& args, EvalContext& ctx)>;

// Function registry - singleton that holds all registered formula functions
class FunctionRegistry {
public:
    // Get the singleton instance
    static FunctionRegistry& instance();

    // Register a function with the given name
    // isVolatile: if true, cells using this function will recalculate on any sheet change
    void registerFunction(const std::string& name, FormulaFunction fn, bool isVolatile = false);

    // Call a function by name with the given arguments
    // Returns NAME error if function doesn't exist
    EvalResult call(const std::string& name, const std::vector<const ASTNode*>& args,
                    EvalContext& ctx) const;

    // Check if a function exists
    [[nodiscard]] bool exists(const std::string& name) const;

    // Check if a function is volatile (NOW, TODAY, RAND, etc.)
    [[nodiscard]] bool isVolatile(const std::string& name) const;

    // Get list of all registered function names
    [[nodiscard]] std::vector<std::string> getFunctionNames() const;

    FunctionRegistry(const FunctionRegistry&) = delete;
    FunctionRegistry& operator=(const FunctionRegistry&) = delete;

private:
    FunctionRegistry();
    ~FunctionRegistry() = default;

    // Convert name to uppercase for case-insensitive lookup
    static std::string toUpper(const std::string& s);

    std::unordered_map<std::string, FormulaFunction> functions_;
    std::unordered_set<std::string> volatileFunctions_;
};

// =============================================================================
// Helper utilities for implementing functions
// =============================================================================

// Expand arguments - evaluates each argument, expanding ranges into their values
// Useful for functions like SUM that accept ranges
std::vector<EvalResult> expandArguments(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// Collect numeric values from expanded arguments
// Skips empty cells, errors are propagated
// Returns pair<values, optional_error> - if error is set, values should be ignored
std::pair<std::vector<double>, EvalResult> collectNumericValues(
    const std::vector<const ASTNode*>& args, EvalContext& ctx);

// Evaluate a single argument and coerce to number
// Returns the numeric result or an error
EvalResult evaluateAsNumber(const ASTNode* arg, EvalContext& ctx);

// Evaluate a single argument and coerce to string
EvalResult evaluateAsString(const ASTNode* arg, EvalContext& ctx);

// Evaluate a single argument and coerce to boolean
EvalResult evaluateAsBoolean(const ASTNode* arg, EvalContext& ctx);

// Initialize all built-in functions (called automatically by FunctionRegistry)
void initializeBuiltinFunctions(FunctionRegistry& registry);

}  // namespace cells

#endif  // CELLS_FORMULA_FUNCTIONS_H_
