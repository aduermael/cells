#include "core/cells/functions/fn_logic.h"

#include "core/cells/formula_ast.h"
#include "core/cells/formula_functions.h"

namespace cells {

// =============================================================================
// Core Logic Functions
// =============================================================================

EvalResult fn_IF(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Evaluate condition
    EvalResult condition = evaluate(args[0], ctx);
    if (condition.isError()) {
        return condition;
    }

    // Convert to boolean
    EvalResult condBool = condition.toBoolean();
    if (condBool.isError()) {
        return condBool;
    }

    if (condBool.getBoolean()) {
        // Return value_if_true
        return evaluate(args[1], ctx);
    }
    // Return value_if_false (or FALSE if not provided)
    if (args.size() == 3) {
        return evaluate(args[2], ctx);
    }
    return EvalResult::Boolean(false);
}

EvalResult fn_AND(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Boolean(true);  // Vacuous truth
    }

    bool foundFalse = false;
    EvalResult firstError = EvalResult::Boolean(true);  // placeholder
    bool hasError = false;
    bool hasLogical = false;

    for (const ASTNode* arg : args) {
        const EvalResult result = evaluate(arg, ctx);
        if (result.isError()) {
            if (!hasError) {
                firstError = result;
                hasError = true;
            }
            continue;
        }

        // Handle ranges by checking all values
        if (result.isRange()) {
            const std::vector<EvalResult> rangeValues = collectRangeValues(result, ctx);
            for (const EvalResult& val : rangeValues) {
                if (val.isError()) {
                    if (!hasError) {
                        firstError = val;
                        hasError = true;
                    }
                    continue;
                }
                if (val.isEmpty() || val.isString()) {
                    continue;  // Skip empty cells and text values
                }
                const EvalResult boolVal = val.toBoolean();
                if (boolVal.isError()) {
                    if (!hasError) {
                        firstError = boolVal;
                        hasError = true;
                    }
                    continue;
                }
                hasLogical = true;
                if (!boolVal.getBoolean()) {
                    foundFalse = true;
                }
            }
        } else {
            // Direct args: skip text and empty (same as range behavior)
            if (result.isEmpty() || result.isString()) {
                continue;
            }
            const EvalResult boolVal = result.toBoolean();
            if (boolVal.isError()) {
                if (!hasError) {
                    firstError = boolVal;
                    hasError = true;
                }
                continue;
            }
            hasLogical = true;
            if (!boolVal.getBoolean()) {
                foundFalse = true;
            }
        }
    }

    // Errors take priority over the boolean result
    if (hasError) {
        return firstError;
    }

    // If no valid logical values were found (all args were text/empty), return #VALUE!
    if (!hasLogical) {
        return EvalResult::Error(CellError::VALUE);
    }

    return EvalResult::Boolean(!foundFalse);
}

EvalResult fn_OR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Boolean(false);  // No true values found
    }

    bool foundTrue = false;
    EvalResult firstError = EvalResult::Boolean(false);  // placeholder
    bool hasError = false;
    bool hasLogical = false;

    for (const ASTNode* arg : args) {
        const EvalResult result = evaluate(arg, ctx);
        if (result.isError()) {
            if (!hasError) {
                firstError = result;
                hasError = true;
            }
            continue;
        }

        // Handle ranges by checking all values
        if (result.isRange()) {
            const std::vector<EvalResult> rangeValues = collectRangeValues(result, ctx);
            for (const EvalResult& val : rangeValues) {
                if (val.isError()) {
                    if (!hasError) {
                        firstError = val;
                        hasError = true;
                    }
                    continue;
                }
                if (val.isEmpty() || val.isString()) {
                    continue;  // Skip empty cells and text values
                }
                const EvalResult boolVal = val.toBoolean();
                if (boolVal.isError()) {
                    if (!hasError) {
                        firstError = boolVal;
                        hasError = true;
                    }
                    continue;
                }
                hasLogical = true;
                if (boolVal.getBoolean()) {
                    foundTrue = true;
                }
            }
        } else {
            // Direct args: skip text and empty (same as range behavior)
            if (result.isEmpty() || result.isString()) {
                continue;
            }
            const EvalResult boolVal = result.toBoolean();
            if (boolVal.isError()) {
                if (!hasError) {
                    firstError = boolVal;
                    hasError = true;
                }
                continue;
            }
            hasLogical = true;
            if (boolVal.getBoolean()) {
                foundTrue = true;
            }
        }
    }

    // Errors take priority over the boolean result
    if (hasError) {
        return firstError;
    }

    // If no valid logical values were found (all args were text/empty), return #VALUE!
    if (!hasLogical) {
        return EvalResult::Error(CellError::VALUE);
    }

    return EvalResult::Boolean(foundTrue);
}

EvalResult fn_NOT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult result = evaluate(args[0], ctx);
    if (result.isError()) {
        return result;
    }

    EvalResult boolVal = result.toBoolean();
    if (boolVal.isError()) {
        return boolVal;
    }

    return EvalResult::Boolean(!boolVal.getBoolean());
}

// =============================================================================
// Error Handling Functions
// =============================================================================

EvalResult fn_IFERROR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult value = evaluate(args[0], ctx);
    if (value.isError()) {
        return evaluate(args[1], ctx);
    }
    return value;
}

EvalResult fn_IFNA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult value = evaluate(args[0], ctx);
    if (value.isError() && value.getError() == CellError::NA) {
        return evaluate(args[1], ctx);
    }
    return value;
}

// =============================================================================
// Type Checking Functions
// =============================================================================

EvalResult fn_EXACT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    EvalResult text1 = evaluateAsString(args[0], ctx);
    if (text1.isError()) {
        return text1;
    }

    EvalResult text2 = evaluateAsString(args[1], ctx);
    if (text2.isError()) {
        return text2;
    }

    return EvalResult::Boolean(text1.getString() == text2.getString());
}

EvalResult fn_ISBLANK(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    const EvalResult result = evaluate(args[0], ctx);
    return EvalResult::Boolean(result.isEmpty());
}

EvalResult fn_ISNUMBER(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    const EvalResult result = evaluate(args[0], ctx);
    return EvalResult::Boolean(result.isNumber());
}

EvalResult fn_ISTEXT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    const EvalResult result = evaluate(args[0], ctx);
    return EvalResult::Boolean(result.isString());
}

EvalResult fn_ISERROR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    const EvalResult result = evaluate(args[0], ctx);
    return EvalResult::Boolean(result.isError());
}

EvalResult fn_ISLOGICAL(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    const EvalResult result = evaluate(args[0], ctx);
    return EvalResult::Boolean(result.isBoolean());
}

EvalResult fn_ISNA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 1) {
        return EvalResult::Error(CellError::VALUE);
    }

    const EvalResult result = evaluate(args[0], ctx);
    return EvalResult::Boolean(result.isError() && result.getError() == CellError::NA);
}

// =============================================================================
// XOR Function
// =============================================================================

EvalResult fn_XOR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }

    int trueCount = 0;
    EvalResult firstError = EvalResult::Boolean(false);  // placeholder
    bool hasError = false;
    bool hasLogical = false;

    for (const ASTNode* arg : args) {
        const EvalResult result = evaluate(arg, ctx);
        if (result.isError()) {
            if (!hasError) {
                firstError = result;
                hasError = true;
            }
            continue;
        }

        if (result.isRange()) {
            const std::vector<EvalResult> rangeValues = collectRangeValues(result, ctx);
            for (const EvalResult& val : rangeValues) {
                if (val.isError()) {
                    if (!hasError) {
                        firstError = val;
                        hasError = true;
                    }
                    continue;
                }
                if (val.isEmpty() || val.isString()) {
                    continue;
                }
                const EvalResult boolVal = val.toBoolean();
                if (boolVal.isError()) {
                    if (!hasError) {
                        firstError = boolVal;
                        hasError = true;
                    }
                    continue;
                }
                hasLogical = true;
                if (boolVal.getBoolean()) {
                    trueCount++;
                }
            }
        } else {
            if (result.isEmpty() || result.isString()) {
                continue;
            }
            const EvalResult boolVal = result.toBoolean();
            if (boolVal.isError()) {
                if (!hasError) {
                    firstError = boolVal;
                    hasError = true;
                }
                continue;
            }
            hasLogical = true;
            if (boolVal.getBoolean()) {
                trueCount++;
            }
        }
    }

    if (hasError) {
        return firstError;
    }

    if (!hasLogical) {
        return EvalResult::Error(CellError::VALUE);
    }

    return EvalResult::Boolean((trueCount % 2) == 1);
}

// =============================================================================
// SWITCH Function
// =============================================================================

EvalResult fn_SWITCH(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    // SWITCH(expression, value1, result1, [value2, result2, ...], [default])
    // Minimum 3 args: expression, value1, result1
    if (args.size() < 3) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Evaluate the expression to match against
    EvalResult expr = evaluate(args[0], ctx);
    if (expr.isError()) {
        return expr;
    }

    // After expression, remaining args are value/result pairs + optional default
    const size_t remaining = args.size() - 1;
    const bool hasDefault = (remaining % 2) == 1;
    const size_t pairCount = remaining / 2;

    // Compare expression against each value
    for (size_t i = 0; i < pairCount; i++) {
        EvalResult caseVal = evaluate(args[1 + i * 2], ctx);
        if (caseVal.isError()) {
            return caseVal;
        }

        // Check equality: same type required, case-insensitive for strings
        bool match = false;
        if (expr.type == caseVal.type) {
            switch (expr.type) {
                case EvalResult::Type::NUMBER:
                    match = expr.numberValue == caseVal.numberValue;
                    break;
                case EvalResult::Type::STRING: {
                    // Case-insensitive comparison (Excel behavior)
                    if (expr.stringValue.size() == caseVal.stringValue.size()) {
                        match = true;
                        for (size_t j = 0; j < expr.stringValue.size(); j++) {
                            if (std::toupper(static_cast<unsigned char>(expr.stringValue[j])) !=
                                std::toupper(static_cast<unsigned char>(caseVal.stringValue[j]))) {
                                match = false;
                                break;
                            }
                        }
                    }
                    break;
                }
                case EvalResult::Type::BOOLEAN:
                    match = expr.boolValue == caseVal.boolValue;
                    break;
                default:
                    break;
            }
        }

        if (match) {
            return evaluate(args[2 + i * 2], ctx);
        }
    }

    // No match found — return default or #N/A
    if (hasDefault) {
        return evaluate(args[args.size() - 1], ctx);
    }
    return EvalResult::Error(CellError::NA);
}

// =============================================================================
// IFS Function
// =============================================================================

EvalResult fn_IFS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    // IFS(condition1, value1, [condition2, value2, ...])
    // Must have even number of args (condition/value pairs), minimum 2
    if (args.size() < 2 || (args.size() % 2) != 0) {
        return EvalResult::Error(CellError::VALUE);
    }

    for (size_t i = 0; i < args.size(); i += 2) {
        EvalResult condition = evaluate(args[i], ctx);
        if (condition.isError()) {
            return condition;
        }

        EvalResult condBool = condition.toBoolean();
        if (condBool.isError()) {
            return condBool;
        }

        if (condBool.getBoolean()) {
            return evaluate(args[i + 1], ctx);
        }
    }

    // No condition was TRUE
    return EvalResult::Error(CellError::NA);
}

// =============================================================================
// NA Function
// =============================================================================

EvalResult fn_NA(const std::vector<const ASTNode*>& args, EvalContext& /*ctx*/) {
    if (!args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    return EvalResult::Error(CellError::NA);
}

// =============================================================================
// LET / LAMBDA
// =============================================================================

// LET(name1, value1, [name2, value2, ...], calculation)
// Binds named variables and evaluates the final expression with those bindings.
EvalResult fn_LET(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    // Must have odd number of args >= 3 (pairs of name/value + final calculation)
    if (args.size() < 3 || args.size() % 2 == 0) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Build a new variable scope inheriting any existing bindings
    std::unordered_map<std::string, EvalResult> scope;
    if (ctx.localVariables) {
        scope = *ctx.localVariables;
    }

    // Set up context with the new scope
    const auto* prevScope = ctx.localVariables;
    ctx.localVariables = &scope;

    // Bind each name/value pair
    const size_t numPairs = (args.size() - 1) / 2;
    for (size_t i = 0; i < numPairs; i++) {
        // Extract variable name from NamedRefNode
        const auto* nameNode = dynamic_cast<const NamedRefNode*>(args[i * 2]);
        if (!nameNode) {
            ctx.localVariables = prevScope;
            return EvalResult::Error(CellError::VALUE);
        }

        // Evaluate the value (can reference previously bound variables)
        EvalResult value = evaluate(args[i * 2 + 1], ctx);
        if (value.isError()) {
            ctx.localVariables = prevScope;
            return value;
        }

        scope[nameNode->name] = std::move(value);
    }

    // Evaluate the calculation expression with all bindings in scope
    EvalResult result = evaluate(args.back(), ctx);

    // Restore previous scope
    ctx.localVariables = prevScope;
    return result;
}

// LAMBDA(param1, [param2, ...], body)(arg1, [arg2, ...])
// Creates and immediately invokes an anonymous function.
// The parser appends invocation args after the definition args, so:
//   args[0..N-1] = parameter names, args[N] = body, args[N+1..end] = invocation args
//   where N = (total_args - 1) / 2
EvalResult fn_LAMBDA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    // Must have odd number of args >= 3 (at least 1 param + body + 1 invocation arg)
    if (args.size() < 3 || args.size() % 2 == 0) {
        return EvalResult::Error(CellError::VALUE);
    }

    const size_t numParams = (args.size() - 1) / 2;
    const size_t bodyIndex = numParams;

    // Build variable scope with param bindings
    std::unordered_map<std::string, EvalResult> scope;
    if (ctx.localVariables) {
        scope = *ctx.localVariables;
    }

    const auto* prevScope = ctx.localVariables;
    ctx.localVariables = &scope;

    // Bind each parameter to its corresponding invocation argument
    for (size_t i = 0; i < numParams; i++) {
        const auto* nameNode = dynamic_cast<const NamedRefNode*>(args[i]);
        if (!nameNode) {
            ctx.localVariables = prevScope;
            return EvalResult::Error(CellError::VALUE);
        }

        // Evaluate the invocation argument
        EvalResult value = evaluate(args[numParams + 1 + i], ctx);
        if (value.isError()) {
            ctx.localVariables = prevScope;
            return value;
        }

        scope[nameNode->name] = std::move(value);
    }

    // Evaluate the body with parameter bindings in scope
    EvalResult result = evaluate(args[bodyIndex], ctx);

    ctx.localVariables = prevScope;
    return result;
}

// =============================================================================
// Boolean Constants
// =============================================================================

EvalResult fn_TRUE(const std::vector<const ASTNode*>& args, EvalContext& /*ctx*/) {
    if (!args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    return EvalResult::Boolean(true);
}

EvalResult fn_FALSE(const std::vector<const ASTNode*>& args, EvalContext& /*ctx*/) {
    if (!args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    return EvalResult::Boolean(false);
}

// =============================================================================
// Registration
// =============================================================================

void registerLogicFunctions(FunctionRegistry& registry) {
    // Core logic functions
    registry.registerFunction("IF", fn_IF, "(condition, value_if_true, [value_if_false])",
                              "Returns one value if condition is true", "Logic");
    registry.registerFunction("AND", fn_AND, "(logical1, [logical2], ...)",
                              "Returns TRUE if all arguments are true", "Logic");
    registry.registerFunction("OR", fn_OR, "(logical1, [logical2], ...)",
                              "Returns TRUE if any argument is true", "Logic");
    registry.registerFunction("NOT", fn_NOT, "(logical)", "Reverses the value of its argument",
                              "Logic");
    registry.registerFunction("XOR", fn_XOR, "(logical1, [logical2], ...)",
                              "Returns TRUE if an odd number of arguments are true", "Logic");
    registry.registerFunction("NA", fn_NA, "()", "Returns the error value #N/A", "Logic");
    registry.registerFunction("SWITCH", fn_SWITCH, "(expression, value1, result1, ..., [default])",
                              "Compares expression against values and returns matching result",
                              "Logic");
    registry.registerFunction("IFS", fn_IFS, "(condition1, value1, condition2, value2, ...)",
                              "Returns value for first TRUE condition", "Logic");
    registry.registerFunction("LET", fn_LET, "(name1, value1, [name2, value2, ...], calculation)",
                              "Defines named variables and returns calculation result", "Logic");
    registry.registerFunction("LAMBDA", fn_LAMBDA, "([param1, param2, ...], body)(args...)",
                              "Creates and immediately invokes anonymous function", "Logic");

    // Error handling
    registry.registerFunction("IFERROR", fn_IFERROR, "(value, value_if_error)",
                              "Returns value_if_error if value is an error", "Logic");
    registry.registerFunction("IFNA", fn_IFNA, "(value, value_if_na)",
                              "Returns value_if_na if value is #N/A", "Logic");

    // Type checking
    registry.registerFunction("EXACT", fn_EXACT, "(text1, text2)",
                              "Checks if two strings are identical", "Logic");
    registry.registerFunction("ISBLANK", fn_ISBLANK, "(value)", "Returns TRUE if cell is empty",
                              "Logic");
    registry.registerFunction("ISNUMBER", fn_ISNUMBER, "(value)",
                              "Returns TRUE if value is a number", "Logic");
    registry.registerFunction("ISTEXT", fn_ISTEXT, "(value)", "Returns TRUE if value is text",
                              "Logic");
    registry.registerFunction("ISERROR", fn_ISERROR, "(value)",
                              "Returns TRUE if value is any error", "Logic");
    registry.registerFunction("ISLOGICAL", fn_ISLOGICAL, "(value)",
                              "Returns TRUE if value is a boolean", "Logic");
    registry.registerFunction("ISNA", fn_ISNA, "(value)", "Returns TRUE if value is #N/A", "Logic");

    // Boolean constants
    registry.registerFunction("TRUE", fn_TRUE, "()", "Returns the logical value TRUE", "Logic");
    registry.registerFunction("FALSE", fn_FALSE, "()", "Returns the logical value FALSE", "Logic");
}

}  // namespace cells
