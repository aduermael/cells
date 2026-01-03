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

    for (const ASTNode* arg : args) {
        EvalResult result = evaluate(arg, ctx);
        if (result.isError()) {
            return result;
        }

        // Handle ranges by checking all values
        if (result.isRange()) {
            const std::vector<EvalResult> rangeValues =
                collectRangeValues(result.getRangeBounds(), ctx);
            for (const EvalResult& val : rangeValues) {
                if (val.isError()) {
                    return val;
                }
                if (val.isEmpty()) {
                    continue;  // Skip empty cells
                }
                EvalResult boolVal = val.toBoolean();
                if (boolVal.isError()) {
                    return boolVal;
                }
                if (!boolVal.getBoolean()) {
                    return EvalResult::Boolean(false);
                }
            }
        } else {
            EvalResult boolVal = result.toBoolean();
            if (boolVal.isError()) {
                return boolVal;
            }
            if (!boolVal.getBoolean()) {
                return EvalResult::Boolean(false);
            }
        }
    }

    return EvalResult::Boolean(true);
}

EvalResult fn_OR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Boolean(false);  // No true values found
    }

    for (const ASTNode* arg : args) {
        EvalResult result = evaluate(arg, ctx);
        if (result.isError()) {
            return result;
        }

        // Handle ranges by checking all values
        if (result.isRange()) {
            const std::vector<EvalResult> rangeValues =
                collectRangeValues(result.getRangeBounds(), ctx);
            for (const EvalResult& val : rangeValues) {
                if (val.isError()) {
                    return val;
                }
                if (val.isEmpty()) {
                    continue;  // Skip empty cells
                }
                EvalResult boolVal = val.toBoolean();
                if (boolVal.isError()) {
                    return boolVal;
                }
                if (boolVal.getBoolean()) {
                    return EvalResult::Boolean(true);
                }
            }
        } else {
            EvalResult boolVal = result.toBoolean();
            if (boolVal.isError()) {
                return boolVal;
            }
            if (boolVal.getBoolean()) {
                return EvalResult::Boolean(true);
            }
        }
    }

    return EvalResult::Boolean(false);
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
