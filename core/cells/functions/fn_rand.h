// =============================================================================
// Random Number Functions
// =============================================================================
//
// Random number generation formula functions.
// These are volatile functions that recalculate on every sheet change.
//
// Functions:
// - RAND(): Random float between 0 and 1 (exclusive)
// - RANDBETWEEN(bottom, top): Random integer in range (inclusive)
//
// Note: These functions use a thread-local PRNG for reproducibility
// within a single recalculation cycle.
//
// Dependencies: formula_eval.h
// Used by: FunctionRegistry initialization
//
// =============================================================================

#ifndef CELLS_FUNCTIONS_FN_RAND_H_
#define CELLS_FUNCTIONS_FN_RAND_H_

#include "core/cells/formula_eval.h"

namespace cells {

// RAND() - Returns a random number between 0 and 1 (exclusive)
// This is a volatile function - recalculates on every sheet change
EvalResult fn_RAND(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// RANDBETWEEN(bottom, top) - Returns a random integer between bottom and top (inclusive)
// This is a volatile function - recalculates on every sheet change
EvalResult fn_RANDBETWEEN(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// Forward declaration
class FunctionRegistry;

// Register RAND functions with the registry
void registerRandFunctions(FunctionRegistry& registry);

}  // namespace cells

#endif  // CELLS_FUNCTIONS_FN_RAND_H_
