// =============================================================================
// Formula Function Registry
// =============================================================================
//
// Central registry for all built-in formula functions (SUM, IF, VLOOKUP, etc.).
// Functions are registered at startup and called during formula evaluation.
//
// Key responsibilities:
// - Register built-in functions with implementation, signature, and metadata
// - Provide case-insensitive function lookup
// - Track volatile functions (NOW, RAND, TODAY) for recalculation
// - Supply function list for autocomplete UI
//
// Function implementation:
// - Arguments are passed as AST nodes for lazy evaluation (IF, AND, OR)
// - Helper functions for argument expansion and type coercion
// - Functions implemented in core/cells/functions/fn_*.cc
//
// Function categories: Math, Statistical, Logical, Text, Lookup, Date/Time
//
// Dependencies: formula_eval.h
// Used by: formula_eval.cc (function calls), bindings.cc (autocomplete)
//
// =============================================================================

#ifndef CELLS_FORMULA_FUNCTIONS_H_
#define CELLS_FORMULA_FUNCTIONS_H_

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/cells/formula_eval.h"

namespace cells {

// Forward declaration
struct ASTNode;

// =============================================================================
// Function metadata for autocomplete
// =============================================================================

// Information about a formula function for autocomplete display
struct FunctionInfo {
    std::string name;         // Function name, e.g., "SUM"
    std::string signature;    // Arguments, e.g., "(number1, [number2], ...)"
    std::string description;  // Brief description, e.g., "Adds all numbers in a range"
    std::string category;     // Category, e.g., "Math", "Logic", "Text"
};

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

    // Register a function with metadata for autocomplete
    void registerFunction(const std::string& name, FormulaFunction fn, const std::string& signature,
                          const std::string& description, const std::string& category,
                          bool isVolatile = false);

    // Register an alternate spelling for an already-registered function.
    // Copies implementation, metadata, and volatility from `canonical`.
    void registerAlias(const std::string& alias, const std::string& canonical);

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

    // Get list of all functions with metadata for autocomplete
    [[nodiscard]] std::vector<FunctionInfo> getFunctionList() const;

    FunctionRegistry(const FunctionRegistry&) = delete;
    FunctionRegistry& operator=(const FunctionRegistry&) = delete;

private:
    FunctionRegistry();
    ~FunctionRegistry() = default;

    // Convert name to uppercase for case-insensitive lookup
    static std::string toUpper(const std::string& s);

    std::unordered_map<std::string, FormulaFunction> functions_;
    std::unordered_map<std::string, FunctionInfo> functionInfo_;
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

// Collect paired numeric values from two array arguments (same expanded length).
// Skips pairs where either side is empty or non-numeric. Errors propagate.
// Length mismatch returns #N/A.
std::pair<std::vector<std::pair<double, double>>, EvalResult> collectPairedNumericValues(
    const ASTNode* arrayX, const ASTNode* arrayY, EvalContext& ctx);

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
