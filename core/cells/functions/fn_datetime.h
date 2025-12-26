#ifndef CELLS_FUNCTIONS_FN_DATETIME_H_
#define CELLS_FUNCTIONS_FN_DATETIME_H_

#include <vector>

#include "core/cells/formula_eval.h"

namespace cells {

// Forward declarations
struct ASTNode;
class FunctionRegistry;

// =============================================================================
// Volatile Date/Time Functions
// =============================================================================

// NOW() - Returns current date and time as Excel serial date (volatile)
EvalResult fn_NOW(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// TODAY() - Returns current date as Excel serial date (volatile)
EvalResult fn_TODAY(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// =============================================================================
// Date Construction Functions
// =============================================================================

// DATE(year, month, day) - Constructs a date
EvalResult fn_DATE(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// TIME(hour, minute, second) - Constructs a time
EvalResult fn_TIME(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// DATEVALUE(date_text) - Converts date string to serial date
EvalResult fn_DATEVALUE(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// TIMEVALUE(time_text) - Converts time string to serial time
EvalResult fn_TIMEVALUE(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// =============================================================================
// Date Extraction Functions
// =============================================================================

// YEAR(serial_number) - Extracts year
EvalResult fn_YEAR(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// MONTH(serial_number) - Extracts month (1-12)
EvalResult fn_MONTH(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// DAY(serial_number) - Extracts day of month
EvalResult fn_DAY(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// HOUR(serial_number) - Extracts hour (0-23)
EvalResult fn_HOUR(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// MINUTE(serial_number) - Extracts minute (0-59)
EvalResult fn_MINUTE(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// SECOND(serial_number) - Extracts second (0-59)
EvalResult fn_SECOND(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// WEEKDAY(serial_number, [return_type]) - Returns day of week
EvalResult fn_WEEKDAY(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// =============================================================================
// Registration
// =============================================================================

// Register date/time functions with the registry
void registerDateTimeFunctions(FunctionRegistry& registry);

}  // namespace cells

#endif  // CELLS_FUNCTIONS_FN_DATETIME_H_
