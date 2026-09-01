// =============================================================================
// Date/Time Functions
// =============================================================================
//
// Date and time formula functions using Excel serial date format.
// Serial dates: days since 1899-12-30. Time: fractional day (0.5 = noon).
//
// Categories:
// - Volatile: NOW, TODAY (recalculate on every change)
// - Construction: DATE, TIME, DATEVALUE, TIMEVALUE
// - Extraction: YEAR, MONTH, DAY, HOUR, MINUTE, SECOND, WEEKDAY
//
// Serial date format:
// - 1 = 1900-01-01
// - 44197 = 2021-01-01
// - Fractional part is time (0.5 = 12:00 PM)
//
// Dependencies: formula_eval.h
// Used by: FunctionRegistry initialization
//
// =============================================================================

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

// EOMONTH(start_date, months) - Returns last day of month N months from start_date
EvalResult fn_EOMONTH(const std::vector<const ASTNode*>& args, EvalContext& ctx);

EvalResult fn_EDATE(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_DAYS(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_DATEDIF(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_WEEKNUM(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_NETWORKDAYS(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_WORKDAY(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_DAYS360(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_YEARFRAC(const std::vector<const ASTNode*>& args, EvalContext& ctx);
EvalResult fn_ISOWEEKNUM(const std::vector<const ASTNode*>& args, EvalContext& ctx);

// =============================================================================
// Registration
// =============================================================================

// Register date/time functions with the registry
void registerDateTimeFunctions(FunctionRegistry& registry);

}  // namespace cells

#endif  // CELLS_FUNCTIONS_FN_DATETIME_H_
