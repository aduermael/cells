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

enum class DbAgg : std::uint8_t {
    Sum,
    Count,
    CountA,
    Average,
    Max,
    Min,
    Get,
    Product,
    Stdev,
    StdevP,
    Var,
    VarP
};

size_t gridColsForDb(const std::vector<std::vector<EvalResult>>& grid) {
    size_t cols = 0;
    for (const auto& row : grid) {
        cols = std::max(cols, row.size());
    }
    return cols;
}

EvalResult gridAtDb(const std::vector<std::vector<EvalResult>>& grid, size_t r, size_t c) {
    if (r >= grid.size() || c >= grid[r].size()) {
        return EvalResult::Empty();
    }
    return grid[r][c];
}

bool cellIsBlankForDb(const EvalResult& v) {
    return v.isEmpty() || (v.isString() && v.getString().empty());
}

std::string headerKey(const EvalResult& v) {
    const EvalResult s = v.toString();
    if (s.isError()) {
        return {};
    }
    return toLowerAscii(s.getString());
}

int resolveDbField(const EvalResult& field, const std::vector<std::string>& headers) {
    if (field.isNumber() || field.isBoolean()) {
        const EvalResult n = field.toNumber();
        if (n.isError()) {
            return -1;
        }
        const int idx = static_cast<int>(n.getNumber());
        if (idx < 1 || idx > static_cast<int>(headers.size())) {
            return -1;
        }
        return idx - 1;
    }
    const EvalResult s = field.toString();
    if (s.isError()) {
        return -1;
    }
    const std::string want = toLowerAscii(s.getString());
    for (size_t i = 0; i < headers.size(); ++i) {
        if (headers[i] == want) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool databaseRowMatches(const std::vector<EvalResult>& row, const std::vector<int>& critToDb,
                        const std::vector<EvalResult>& critRow) {
    for (size_t c = 0; c < critToDb.size(); ++c) {
        if (c >= critRow.size() || cellIsBlankForDb(critRow[c])) {
            continue;
        }
        const int dbCol = critToDb[c];
        if (dbCol < 0) {
            continue;
        }
        const EvalResult cell = static_cast<size_t>(dbCol) < row.size()
                                    ? row[static_cast<size_t>(dbCol)]
                                    : EvalResult::Empty();
        if (!matchesCriteria(cell, parseCriteria(critRow[c]))) {
            return false;
        }
    }
    return true;
}

EvalResult databaseAggregate(const std::vector<const ASTNode*>& args, EvalContext& ctx,
                             DbAgg kind) {
    const bool fieldOptional = kind == DbAgg::Count || kind == DbAgg::CountA;
    if (args.size() < 2 || args.size() > 3 || (args.size() == 2 && !fieldOptional)) {
        return EvalResult::Error(CellError::VALUE);
    }
    auto [db, dbErr] = evaluateAs2D(args[0], ctx);
    if (dbErr.isError()) {
        return dbErr;
    }
    if (db.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }
    const ASTNode* criteriaArg = args.size() == 3 ? args[2] : args[1];
    auto [crit, critErr] = evaluateAs2D(criteriaArg, ctx);
    if (critErr.isError()) {
        return critErr;
    }
    if (crit.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }

    const size_t dbCols = gridColsForDb(db);
    std::vector<std::string> headers;
    headers.reserve(dbCols);
    for (size_t c = 0; c < dbCols; ++c) {
        headers.push_back(headerKey(gridAtDb(db, 0, c)));
    }

    const bool haveField = args.size() == 3;
    int fieldCol = -1;
    if (haveField) {
        EvalResult field = evaluate(args[1], ctx);
        if (field.isError()) {
            return field;
        }
        fieldCol = resolveDbField(field, headers);
        if (fieldCol < 0) {
            return EvalResult::Error(CellError::VALUE);
        }
    }

    const size_t critCols = gridColsForDb(crit);
    std::vector<int> critToDb(critCols, -1);
    for (size_t c = 0; c < critCols; ++c) {
        const std::string key = headerKey(gridAtDb(crit, 0, c));
        for (size_t d = 0; d < headers.size(); ++d) {
            if (headers[d] == key) {
                critToDb[c] = static_cast<int>(d);
                break;
            }
        }
    }

    std::vector<EvalResult> matched;
    for (size_t r = 1; r < db.size(); ++r) {
        bool any = crit.size() <= 1;
        for (size_t cr = 1; cr < crit.size(); ++cr) {
            if (databaseRowMatches(db[r], critToDb, crit[cr])) {
                any = true;
                break;
            }
        }
        if (!any) {
            continue;
        }
        if (!haveField) {
            matched.push_back(EvalResult::Number(1.0));
            continue;
        }
        matched.push_back(gridAtDb(db, r, static_cast<size_t>(fieldCol)));
    }

    if (kind == DbAgg::Get) {
        if (matched.empty()) {
            return EvalResult::Error(CellError::NA);
        }
        if (matched.size() > 1) {
            return EvalResult::Error(CellError::NUM);
        }
        return matched[0];
    }

    double sum = 0.0;
    double sumSq = 0.0;
    double minV = 0.0;
    double maxV = 0.0;
    double product = 1.0;
    size_t n = 0;
    size_t countA = 0;
    bool haveMinMax = false;
    for (const EvalResult& v : matched) {
        if (kind == DbAgg::CountA) {
            if (!v.isEmpty()) {
                ++countA;
            }
            continue;
        }
        if (kind == DbAgg::Count) {
            if (v.isNumber()) {
                ++n;
            }
            continue;
        }
        if (v.isError()) {
            return v;
        }
        if (!v.isNumber()) {
            continue;
        }
        const double num = v.getNumber();
        ++n;
        sum += num;
        sumSq += num * num;
        product *= num;
        if (!haveMinMax) {
            minV = num;
            maxV = num;
            haveMinMax = true;
        } else {
            minV = std::min(minV, num);
            maxV = std::max(maxV, num);
        }
    }

    switch (kind) {
        case DbAgg::Count:
            return EvalResult::Number(static_cast<double>(n));
        case DbAgg::CountA:
            return EvalResult::Number(static_cast<double>(countA));
        case DbAgg::Sum:
            return EvalResult::Number(excelNormalize(sum));
        case DbAgg::Product:
            return EvalResult::Number(n == 0 ? 0.0 : excelNormalize(product));
        case DbAgg::Average:
            if (n == 0) {
                return EvalResult::Error(CellError::DIV);
            }
            return EvalResult::Number(excelNormalize(sum / static_cast<double>(n)));
        case DbAgg::Max:
            return EvalResult::Number(haveMinMax ? excelNormalize(maxV) : 0.0);
        case DbAgg::Min:
            return EvalResult::Number(haveMinMax ? excelNormalize(minV) : 0.0);
        case DbAgg::Stdev:
        case DbAgg::Var: {
            if (n < 2) {
                return EvalResult::Error(CellError::DIV);
            }
            const double mean = sum / static_cast<double>(n);
            const double ss = sumSq - static_cast<double>(n) * mean * mean;
            const double var = ss / static_cast<double>(n - 1);
            if (kind == DbAgg::Var) {
                return EvalResult::Number(excelNormalize(var));
            }
            return EvalResult::Number(excelNormalize(std::sqrt(std::max(0.0, var))));
        }
        case DbAgg::StdevP:
        case DbAgg::VarP: {
            if (n < 1) {
                return EvalResult::Error(CellError::DIV);
            }
            const double mean = sum / static_cast<double>(n);
            const double ss = sumSq - static_cast<double>(n) * mean * mean;
            const double var = ss / static_cast<double>(n);
            if (kind == DbAgg::VarP) {
                return EvalResult::Number(excelNormalize(var));
            }
            return EvalResult::Number(excelNormalize(std::sqrt(std::max(0.0, var))));
        }
        case DbAgg::Get:
            break;
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
        const std::vector<const ASTNode*> rewritten = {args[0], args[0], args[1]};
        return aggregateIfs(rewritten, ctx, AggKind::Sum, true, 3);
    }
    const std::vector<const ASTNode*> rewritten = {args[2], args[0], args[1]};
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
        const std::vector<const ASTNode*> rewritten = {args[0], args[0], args[1]};
        return aggregateIfs(rewritten, ctx, AggKind::Average, true, 3);
    }
    const std::vector<const ASTNode*> rewritten = {args[2], args[0], args[1]};
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

EvalResult fn_DSUM(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return databaseAggregate(args, ctx, DbAgg::Sum);
}
EvalResult fn_DCOUNT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return databaseAggregate(args, ctx, DbAgg::Count);
}
EvalResult fn_DCOUNTA(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return databaseAggregate(args, ctx, DbAgg::CountA);
}
EvalResult fn_DAVERAGE(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return databaseAggregate(args, ctx, DbAgg::Average);
}
EvalResult fn_DMAX(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return databaseAggregate(args, ctx, DbAgg::Max);
}
EvalResult fn_DMIN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return databaseAggregate(args, ctx, DbAgg::Min);
}
EvalResult fn_DGET(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return databaseAggregate(args, ctx, DbAgg::Get);
}
EvalResult fn_DPRODUCT(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return databaseAggregate(args, ctx, DbAgg::Product);
}
EvalResult fn_DSTDEV(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return databaseAggregate(args, ctx, DbAgg::Stdev);
}
EvalResult fn_DSTDEVP(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return databaseAggregate(args, ctx, DbAgg::StdevP);
}
EvalResult fn_DVAR(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return databaseAggregate(args, ctx, DbAgg::Var);
}
EvalResult fn_DVARP(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    return databaseAggregate(args, ctx, DbAgg::VarP);
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
    registry.registerFunction("DSUM", fn_DSUM, "(database, field, criteria)",
                              "Sums matching database records", "Database");
    registry.registerFunction("DCOUNT", fn_DCOUNT, "(database, [field], criteria)",
                              "Counts numeric matching database records", "Database");
    registry.registerFunction("DCOUNTA", fn_DCOUNTA, "(database, [field], criteria)",
                              "Counts nonblank matching database records", "Database");
    registry.registerFunction("DAVERAGE", fn_DAVERAGE, "(database, field, criteria)",
                              "Averages matching database records", "Database");
    registry.registerFunction("DMAX", fn_DMAX, "(database, field, criteria)",
                              "Maximum of matching database records", "Database");
    registry.registerFunction("DMIN", fn_DMIN, "(database, field, criteria)",
                              "Minimum of matching database records", "Database");
    registry.registerFunction("DGET", fn_DGET, "(database, field, criteria)",
                              "Extracts a single matching database value", "Database");
    registry.registerFunction("DPRODUCT", fn_DPRODUCT, "(database, field, criteria)",
                              "Product of matching database records", "Database");
    registry.registerFunction("DSTDEV", fn_DSTDEV, "(database, field, criteria)",
                              "Sample standard deviation of matching records", "Database");
    registry.registerFunction("DSTDEVP", fn_DSTDEVP, "(database, field, criteria)",
                              "Population standard deviation of matching records", "Database");
    registry.registerFunction("DVAR", fn_DVAR, "(database, field, criteria)",
                              "Sample variance of matching records", "Database");
    registry.registerFunction("DVARP", fn_DVARP, "(database, field, criteria)",
                              "Population variance of matching records", "Database");
}

}  // namespace cells
