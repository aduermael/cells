#include "core/cells/functions/fn_conditional.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_functions.h"

namespace cells {
namespace {

std::string toLowerAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool hasWildcardChars(const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '~' && i + 1 < s.size()) {
            ++i;
            continue;
        }
        if (s[i] == '*' || s[i] == '?') {
            return true;
        }
    }
    return false;
}

enum class Cmp : std::uint8_t { EQ, NE, GT, GTE, LT, LTE };

struct Criteria {
    Cmp cmp{Cmp::EQ};
    EvalResult rhs;
    bool wildcard{false};
    std::string textLower;
};

bool parseAsNumber(const std::string& s, double* out) {
    if (s.empty()) {
        return false;
    }
    char* end = nullptr;
    const double n = std::strtod(s.c_str(), &end);
    if (end == nullptr || end == s.c_str()) {
        return false;
    }
    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (*end != '\0') {
        return false;
    }
    *out = n;
    return true;
}

Criteria parseCriteria(const EvalResult& raw) {
    Criteria c;
    if (raw.isError() || raw.isNumber() || raw.isBoolean() || raw.isEmpty()) {
        c.rhs = raw;
        return c;
    }
    const EvalResult asStr = raw.toString();
    if (asStr.isError()) {
        c.rhs = asStr;
        return c;
    }
    const std::string& s = asStr.getString();
    size_t skip = 0;
    if (s.size() >= 2 && s[0] == '<' && s[1] == '>') {
        c.cmp = Cmp::NE;
        skip = 2;
    } else if (s.size() >= 2 && s[0] == '>' && s[1] == '=') {
        c.cmp = Cmp::GTE;
        skip = 2;
    } else if (s.size() >= 2 && s[0] == '<' && s[1] == '=') {
        c.cmp = Cmp::LTE;
        skip = 2;
    } else if (!s.empty() && s[0] == '>') {
        c.cmp = Cmp::GT;
        skip = 1;
    } else if (!s.empty() && s[0] == '<') {
        c.cmp = Cmp::LT;
        skip = 1;
    } else if (!s.empty() && s[0] == '=') {
        c.cmp = Cmp::EQ;
        skip = 1;
    }
    const std::string rest = s.substr(skip);
    double n = 0.0;
    if (parseAsNumber(rest, &n)) {
        c.rhs = EvalResult::Number(n);
        return c;
    }
    if (rest.empty()) {
        c.rhs = EvalResult::Empty();
        return c;
    }
    c.rhs = EvalResult::String(rest);
    c.textLower = toLowerAscii(rest);
    if ((c.cmp == Cmp::EQ || c.cmp == Cmp::NE) && hasWildcardChars(rest)) {
        c.wildcard = true;
    }
    return c;
}

int cmpNumbers(double a, double b) {
    if (a < b) {
        return -1;
    }
    if (a > b) {
        return 1;
    }
    return 0;
}

bool equalValues(const EvalResult& cell, const Criteria& c) {
    if (c.rhs.isEmpty()) {
        return cell.isEmpty();
    }
    if (c.rhs.isBoolean()) {
        return cell.isBoolean() && cell.getBoolean() == c.rhs.getBoolean();
    }
    if (c.rhs.isNumber()) {
        if (cell.isNumber()) {
            return cell.getNumber() == c.rhs.getNumber();
        }
        if (cell.isString()) {
            const EvalResult n = cell.toNumber();
            return n.isNumber() && n.getNumber() == c.rhs.getNumber();
        }
        return false;
    }
    if (!c.rhs.isString()) {
        return false;
    }
    if (cell.isEmpty()) {
        return false;
    }
    if (c.wildcard) {
        if (!cell.isString() && !cell.isNumber() && !cell.isBoolean()) {
            return false;
        }
        // Wildcards apply to text representations; numbers don't match "*" in Excel.
        if (!cell.isString()) {
            return false;
        }
        return excelWildcardMatch(cell.getString(), c.rhs.getString());
    }
    if (cell.isNumber()) {
        double n = 0.0;
        if (parseAsNumber(c.textLower, &n)) {
            return cell.getNumber() == n;
        }
        return false;
    }
    if (cell.isString()) {
        return toLowerAscii(cell.getString()) == c.textLower;
    }
    if (cell.isBoolean()) {
        return toLowerAscii(cell.toString().getString()) == c.textLower;
    }
    return false;
}

bool compareOp(const EvalResult& cell, const Criteria& c) {
    if (c.rhs.isNumber()) {
        if (!cell.isNumber()) {
            return false;
        }
        const int cmp = cmpNumbers(cell.getNumber(), c.rhs.getNumber());
        switch (c.cmp) {
            case Cmp::GT:
                return cmp > 0;
            case Cmp::GTE:
                return cmp >= 0;
            case Cmp::LT:
                return cmp < 0;
            case Cmp::LTE:
                return cmp <= 0;
            default:
                return false;
        }
    }
    if (c.rhs.isString() && cell.isString()) {
        const int cmp = toLowerAscii(cell.getString()).compare(c.textLower);
        switch (c.cmp) {
            case Cmp::GT:
                return cmp > 0;
            case Cmp::GTE:
                return cmp >= 0;
            case Cmp::LT:
                return cmp < 0;
            case Cmp::LTE:
                return cmp <= 0;
            default:
                return false;
        }
    }
    return false;
}

bool matchesCriteria(const EvalResult& cell, const Criteria& c) {
    if (c.rhs.isError()) {
        return false;
    }
    switch (c.cmp) {
        case Cmp::EQ:
            return equalValues(cell, c);
        case Cmp::NE:
            return !equalValues(cell, c);
        case Cmp::GT:
        case Cmp::GTE:
        case Cmp::LT:
        case Cmp::LTE:
            return compareOp(cell, c);
    }
    return false;
}

// Flatten one argument to a list of cell-like values (range/array/scalar).
std::pair<std::vector<EvalResult>, EvalResult> flattenArg(const ASTNode* arg, EvalContext& ctx) {
    const EvalResult r = evaluate(arg, ctx);
    if (r.isError()) {
        return {{}, r};
    }
    if (r.isRange()) {
        return {collectRangeValues(r, ctx), EvalResult::Empty()};
    }
    if (r.isArray()) {
        std::vector<EvalResult> out;
        for (const auto& row : r.getArray()) {
            for (const auto& v : row) {
                if (v.isError()) {
                    return {{}, v};
                }
                out.push_back(v);
            }
        }
        return {std::move(out), EvalResult::Empty()};
    }
    return {{r}, EvalResult::Empty()};
}

enum class AggKind : std::uint8_t { Sum, Count, Average, Min, Max };

EvalResult aggregateIfs(const std::vector<const ASTNode*>& args, EvalContext& ctx, AggKind kind,
                        bool valueFirst, size_t minArgs) {
    if (args.size() < minArgs || (args.size() % 2) != (valueFirst ? 1 : 0)) {
        return EvalResult::Error(CellError::VALUE);
    }

    const ASTNode* valueArg = nullptr;
    size_t pairStart = 0;
    if (valueFirst) {
        valueArg = args[0];
        pairStart = 1;
        if ((args.size() - 1) % 2 != 0 || args.size() < 3) {
            return EvalResult::Error(CellError::VALUE);
        }
    } else {
        // COUNTIFS: pairs only
        if (args.size() % 2 != 0) {
            return EvalResult::Error(CellError::VALUE);
        }
    }

    std::vector<EvalResult> values;
    if (valueArg != nullptr) {
        auto [vals, err] = flattenArg(valueArg, ctx);
        if (err.isError()) {
            return err;
        }
        values = std::move(vals);
    }

    struct Pair {
        std::vector<EvalResult> cells;
        Criteria criteria;
    };
    std::vector<Pair> pairs;
    for (size_t i = pairStart; i < args.size(); i += 2) {
        auto [cells, err] = flattenArg(args[i], ctx);
        if (err.isError()) {
            return err;
        }
        const EvalResult critRaw = evaluate(args[i + 1], ctx);
        if (critRaw.isError()) {
            return critRaw;
        }
        pairs.push_back(Pair{std::move(cells), parseCriteria(critRaw)});
    }

    size_t n = 0;
    if (!values.empty()) {
        n = values.size();
    } else if (!pairs.empty()) {
        n = pairs[0].cells.size();
    }
    for (const auto& p : pairs) {
        if (p.cells.size() != n && n != 0) {
            return EvalResult::Error(CellError::VALUE);
        }
        if (n == 0) {
            n = p.cells.size();
        }
    }
    if (valueArg != nullptr && values.size() != n) {
        return EvalResult::Error(CellError::VALUE);
    }

    double sum = 0.0;
    size_t count = 0;
    double minV = 0.0;
    double maxV = 0.0;
    bool haveMinMax = false;

    for (size_t i = 0; i < n; ++i) {
        bool ok = true;
        for (const auto& p : pairs) {
            if (!matchesCriteria(p.cells[i], p.criteria)) {
                ok = false;
                break;
            }
        }
        if (!ok) {
            continue;
        }
        if (kind == AggKind::Count) {
            ++count;
            continue;
        }
        const EvalResult& v = values[i];
        if (v.isError()) {
            return v;
        }
        if (!v.isNumber()) {
            continue;
        }
        const double num = v.getNumber();
        switch (kind) {
            case AggKind::Sum:
            case AggKind::Average:
                sum += num;
                ++count;
                break;
            case AggKind::Min:
                if (!haveMinMax || num < minV) {
                    minV = num;
                }
                haveMinMax = true;
                ++count;
                break;
            case AggKind::Max:
                if (!haveMinMax || num > maxV) {
                    maxV = num;
                }
                haveMinMax = true;
                ++count;
                break;
            case AggKind::Count:
                break;
        }
    }

    switch (kind) {
        case AggKind::Sum:
            return EvalResult::Number(excelNormalize(sum));
        case AggKind::Count:
            return EvalResult::Number(static_cast<double>(count));
        case AggKind::Average:
            if (count == 0) {
                return EvalResult::Error(CellError::DIV);
            }
            return EvalResult::Number(excelNormalize(sum / static_cast<double>(count)));
        case AggKind::Min:
            if (!haveMinMax) {
                return EvalResult::Number(0.0);
            }
            return EvalResult::Number(excelNormalize(minV));
        case AggKind::Max:
            if (!haveMinMax) {
                return EvalResult::Number(0.0);
            }
            return EvalResult::Number(excelNormalize(maxV));
    }
    return EvalResult::Error(CellError::VALUE);
}

}  // namespace

EvalResult fn_SUMIF(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    // SUMIF(range, criteria, [sum_range]) → value-first when sum_range present
    if (args.size() == 2) {
        std::vector<const ASTNode*> rewritten = {args[0], args[0], args[1]};
        return aggregateIfs(rewritten, ctx, AggKind::Sum, true, 3);
    }
    std::vector<const ASTNode*> rewritten = {args[2], args[0], args[1]};
    return aggregateIfs(rewritten, ctx, AggKind::Sum, true, 3);
}

EvalResult fn_SUMIFS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return aggregateIfs(args, ctx, AggKind::Sum, true, 3);
}

EvalResult fn_COUNTIF(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }
    return aggregateIfs(args, ctx, AggKind::Count, false, 2);
}

EvalResult fn_COUNTIFS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return aggregateIfs(args, ctx, AggKind::Count, false, 2);
}

EvalResult fn_AVERAGEIF(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() < 2 || args.size() > 3) {
        return EvalResult::Error(CellError::VALUE);
    }
    if (args.size() == 2) {
        std::vector<const ASTNode*> rewritten = {args[0], args[0], args[1]};
        return aggregateIfs(rewritten, ctx, AggKind::Average, true, 3);
    }
    std::vector<const ASTNode*> rewritten = {args[2], args[0], args[1]};
    return aggregateIfs(rewritten, ctx, AggKind::Average, true, 3);
}

EvalResult fn_AVERAGEIFS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return aggregateIfs(args, ctx, AggKind::Average, true, 3);
}

EvalResult fn_MINIFS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return aggregateIfs(args, ctx, AggKind::Min, true, 3);
}

EvalResult fn_MAXIFS(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return aggregateIfs(args, ctx, AggKind::Max, true, 3);
}

EvalResult fn_SUMPRODUCT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    std::vector<std::vector<EvalResult>> arrays;
    arrays.reserve(args.size());
    size_t n = 0;
    for (const ASTNode* arg : args) {
        auto [vals, err] = flattenArg(arg, ctx);
        if (err.isError()) {
            return err;
        }
        if (arrays.empty()) {
            n = vals.size();
        } else if (vals.size() != n) {
            return EvalResult::Error(CellError::VALUE);
        }
        arrays.push_back(std::move(vals));
    }

    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double prod = 1.0;
        for (const auto& arr : arrays) {
            const EvalResult& v = arr[i];
            if (v.isError()) {
                return v;
            }
            if (v.isNumber()) {
                prod *= v.getNumber();
            } else if (v.isBoolean()) {
                prod *= v.getBoolean() ? 1.0 : 0.0;
            } else {
                prod = 0.0;
                break;
            }
        }
        sum += prod;
    }
    return EvalResult::Number(excelNormalize(sum));
}

void registerConditionalFunctions(FunctionRegistry& registry) {
    registry.registerFunction("SUMIF", fn_SUMIF, "(range, criteria, [sum_range])",
                              "Sums cells that meet a criterion", "Math");
    registry.registerFunction("SUMIFS", fn_SUMIFS, "(sum_range, criteria_range1, criteria1, ...)",
                              "Sums cells that meet all criteria", "Math");
    registry.registerFunction("COUNTIF", fn_COUNTIF, "(range, criteria)",
                              "Counts cells that meet a criterion", "Math");
    registry.registerFunction("COUNTIFS", fn_COUNTIFS, "(criteria_range1, criteria1, ...)",
                              "Counts cells that meet all criteria", "Math");
    registry.registerFunction("AVERAGEIF", fn_AVERAGEIF, "(range, criteria, [average_range])",
                              "Averages cells that meet a criterion", "Math");
    registry.registerFunction("AVERAGEIFS", fn_AVERAGEIFS,
                              "(average_range, criteria_range1, criteria1, ...)",
                              "Averages cells that meet all criteria", "Math");
    registry.registerFunction("MINIFS", fn_MINIFS, "(min_range, criteria_range1, criteria1, ...)",
                              "Minimum of cells that meet all criteria", "Math");
    registry.registerFunction("MAXIFS", fn_MAXIFS, "(max_range, criteria_range1, criteria1, ...)",
                              "Maximum of cells that meet all criteria", "Math");
    registry.registerFunction("SUMPRODUCT", fn_SUMPRODUCT, "(array1, [array2], ...)",
                              "Sum of products of corresponding arrays", "Math");
}

}  // namespace cells
