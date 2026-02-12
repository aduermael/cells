// =============================================================================
// Math Functions
// =============================================================================
//
// Basic mathematical and aggregate formula functions.
// Includes SUM, AVERAGE, MIN, MAX, ABS, SQRT, ROUND, etc.
//
// Categories:
// - Aggregate: SUM, AVERAGE, COUNT, COUNTA, MIN, MAX
// - Basic math: ABS, SQRT, POWER, MOD, INT
// - Rounding: ROUND, FLOOR, CEILING
//
// Dependencies: formula_eval.h
// Used by: FunctionRegistry initialization
//
// =============================================================================

#ifndef CELLS_FUNCTIONS_FN_MATH_H_
#define CELLS_FUNCTIONS_FN_MATH_H_

#include <vector>

#include "core/cells/formula_eval.h"

namespace cells {

// Forward declarations
struct ASTNode;
class FunctionRegistry;

// =============================================================================
// Aggregate Functions
// =============================================================================

// SUM(value1, [value2], ...) - Adds all numbers in the argument list
EvalResult fn_SUM(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// AVERAGE(value1, [value2], ...) - Returns arithmetic mean of numbers
EvalResult fn_AVERAGE(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// COUNT(value1, [value2], ...) - Counts numbers only
EvalResult fn_COUNT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// COUNTA(value1, [value2], ...) - Counts non-empty values
EvalResult fn_COUNTA(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// MIN(value1, [value2], ...) - Returns smallest number
EvalResult fn_MIN(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// MAX(value1, [value2], ...) - Returns largest number
EvalResult fn_MAX(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// =============================================================================
// Basic Math Functions
// =============================================================================

// ABS(number) - Returns absolute value
EvalResult fn_ABS(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// SQRT(number) - Returns square root
EvalResult fn_SQRT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// POWER(number, power) - Returns number raised to power
EvalResult fn_POWER(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ROUND(number, [num_digits]) - Rounds to specified number of digits
EvalResult fn_ROUND(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ROUNDUP(number, [num_digits]) - Rounds away from zero
EvalResult fn_ROUNDUP(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ROUNDDOWN(number, [num_digits]) - Rounds toward zero
EvalResult fn_ROUNDDOWN(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// FLOOR(number) - Rounds down toward negative infinity
EvalResult fn_FLOOR(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// CEILING(number) - Rounds up toward positive infinity
EvalResult fn_CEILING(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// CEILING_MATH(number, [significance], [mode]) - Rounds up to nearest multiple
EvalResult fn_CEILING_MATH(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// FLOOR_MATH(number, [significance], [mode]) - Rounds down to nearest multiple
EvalResult fn_FLOOR_MATH(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// MOD(number, divisor) - Returns remainder after division
EvalResult fn_MOD(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// INT(number) - Truncates to integer (rounds down toward negative infinity)
EvalResult fn_INT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// SIGN(number) - Returns -1, 0, or 1
EvalResult fn_SIGN(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// EXP(number) - Returns e raised to the power of number
EvalResult fn_EXP(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// LN(number) - Returns the natural logarithm
EvalResult fn_LN(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// TRUNC(number, [num_digits]) - Truncates toward zero
EvalResult fn_TRUNC(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// FACT(number) - Returns the factorial
EvalResult fn_FACT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// QUOTIENT(numerator, denominator) - Returns integer portion of division
EvalResult fn_QUOTIENT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// LOG10(number) - Returns the base-10 logarithm
EvalResult fn_LOG10(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// LOG(number, [base]) - Returns the logarithm to specified base (default 10)
EvalResult fn_LOG(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// =============================================================================
// Trigonometric Functions
// =============================================================================

// PI() - Returns the value of Pi
EvalResult fn_PI(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// SIN(number) - Returns the sine of an angle in radians
EvalResult fn_SIN(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// COS(number) - Returns the cosine of an angle in radians
EvalResult fn_COS(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// TAN(number) - Returns the tangent of an angle in radians
EvalResult fn_TAN(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ASIN(number) - Returns the arcsine
EvalResult fn_ASIN(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ACOS(number) - Returns the arccosine
EvalResult fn_ACOS(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ATAN(number) - Returns the arctangent
EvalResult fn_ATAN(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ATAN2(x_num, y_num) - Returns the arctangent of x and y coordinates
EvalResult fn_ATAN2(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// CSC(number) - Returns the cosecant
EvalResult fn_CSC(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// SEC(number) - Returns the secant
EvalResult fn_SEC(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// COT(number) - Returns the cotangent
EvalResult fn_COT(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// =============================================================================
// Hyperbolic Functions
// =============================================================================

// SINH(number) - Returns the hyperbolic sine
EvalResult fn_SINH(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// COSH(number) - Returns the hyperbolic cosine
EvalResult fn_COSH(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// TANH(number) - Returns the hyperbolic tangent
EvalResult fn_TANH(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ASINH(number) - Returns the inverse hyperbolic sine
EvalResult fn_ASINH(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ACOSH(number) - Returns the inverse hyperbolic cosine
EvalResult fn_ACOSH(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// ATANH(number) - Returns the inverse hyperbolic tangent
EvalResult fn_ATANH(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// =============================================================================
// Angle Conversion Functions
// =============================================================================

// RADIANS(angle) - Converts degrees to radians
EvalResult fn_RADIANS(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// DEGREES(angle) - Converts radians to degrees
EvalResult fn_DEGREES(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// =============================================================================
// Registration
// =============================================================================

// Register math functions with the registry
void registerMathFunctions(FunctionRegistry& registry);

}  // namespace cells

#endif  // CELLS_FUNCTIONS_FN_MATH_H_
