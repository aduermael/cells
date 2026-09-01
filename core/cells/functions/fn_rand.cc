#include "core/cells/functions/fn_rand.h"

#include <cmath>

#include <random>

#include "core/cells/formula_ast.h"
#include "core/cells/formula_functions.h"

namespace cells {

namespace {

// Thread-local random number generator for thread safety
std::mt19937& getRandomGenerator() {
    thread_local std::mt19937 gen(std::random_device{}());
    return gen;
}

}  // namespace

EvalResult fn_RAND(const std::vector<const ASTNode*>& args, EvalContext& /*ctx*/) {
    // RAND takes no arguments
    if (!args.empty()) {
        return EvalResult::Error(CellError::VALUE);
    }

    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return EvalResult::Number(dist(getRandomGenerator()));
}

EvalResult fn_RANDBETWEEN(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    // RANDBETWEEN requires exactly 2 arguments
    if (args.size() != 2) {
        return EvalResult::Error(CellError::VALUE);
    }

    // Evaluate arguments
    EvalResult bottomResult = evaluateAsNumber(args[0], ctx);
    if (bottomResult.isError()) {
        return bottomResult;
    }

    EvalResult topResult = evaluateAsNumber(args[1], ctx);
    if (topResult.isError()) {
        return topResult;
    }

    // Get integer bounds (Excel truncates towards zero)
    const double bottomRaw = bottomResult.getNumber();
    const double topRaw = topResult.getNumber();

    // Excel's RANDBETWEEN rounds bottom up and top down (ceiling/floor)
    // This ensures the range is valid integers
    const auto bottom = static_cast<int64_t>(std::ceil(bottomRaw));
    const auto top = static_cast<int64_t>(std::floor(topRaw));

    // Check for invalid range
    if (bottom > top) {
        return EvalResult::Error(CellError::NUM);
    }

    // Generate random integer in range [bottom, top]
    std::uniform_int_distribution<int64_t> dist(bottom, top);
    return EvalResult::Number(static_cast<double>(dist(getRandomGenerator())));
}

EvalResult fn_RANDARRAY(const std::vector<const ASTNode*>& args, EvalContext& ctx) {
    if (args.size() > 5) {
        return EvalResult::Error(CellError::VALUE);
    }
    int rows = 1;
    int cols = 1;
    double minV = 0.0;
    double maxV = 1.0;
    bool integer = false;
    if (!args.empty()) {
        const EvalResult r = evaluateAsNumber(args[0], ctx);
        if (r.isError()) {
            return r;
        }
        rows = static_cast<int>(r.getNumber());
    }
    if (args.size() >= 2) {
        const EvalResult c = evaluateAsNumber(args[1], ctx);
        if (c.isError()) {
            return c;
        }
        cols = static_cast<int>(c.getNumber());
    }
    if (args.size() >= 3) {
        const EvalResult m = evaluateAsNumber(args[2], ctx);
        if (m.isError()) {
            return m;
        }
        minV = m.getNumber();
    }
    if (args.size() >= 4) {
        const EvalResult m = evaluateAsNumber(args[3], ctx);
        if (m.isError()) {
            return m;
        }
        maxV = m.getNumber();
    }
    if (args.size() >= 5) {
        const EvalResult i = evaluateAsBoolean(args[4], ctx);
        if (i.isError()) {
            return i;
        }
        integer = i.getBoolean();
    }
    if (rows < 1 || cols < 1 || minV > maxV) {
        return EvalResult::Error(CellError::VALUE);
    }

    std::vector<std::vector<EvalResult>> out;
    out.reserve(static_cast<size_t>(rows));
    auto& gen = getRandomGenerator();
    if (integer) {
        const auto lo = static_cast<int64_t>(std::ceil(minV));
        const auto hi = static_cast<int64_t>(std::floor(maxV));
        if (lo > hi) {
            return EvalResult::Error(CellError::VALUE);
        }
        std::uniform_int_distribution<int64_t> dist(lo, hi);
        for (int r = 0; r < rows; ++r) {
            std::vector<EvalResult> row;
            row.reserve(static_cast<size_t>(cols));
            for (int c = 0; c < cols; ++c) {
                row.push_back(EvalResult::Number(static_cast<double>(dist(gen))));
            }
            out.push_back(std::move(row));
        }
    } else {
        std::uniform_real_distribution<double> dist(minV, maxV);
        for (int r = 0; r < rows; ++r) {
            std::vector<EvalResult> row;
            row.reserve(static_cast<size_t>(cols));
            for (int c = 0; c < cols; ++c) {
                row.push_back(EvalResult::Number(dist(gen)));
            }
            out.push_back(std::move(row));
        }
    }
    return EvalResult::Array(std::move(out));
}

void registerRandFunctions(FunctionRegistry& registry) {
    // Register as volatile functions - they recalculate on every sheet change
    registry.registerFunction("RAND", fn_RAND, "()", "Returns a random number between 0 and 1",
                              "Math", true);
    registry.registerFunction("RANDBETWEEN", fn_RANDBETWEEN, "(bottom, top)",
                              "Returns a random integer in a range", "Math", true);
    registry.registerFunction("RANDARRAY", fn_RANDARRAY,
                              "([rows], [cols], [min], [max], [integer])",
                              "Returns an array of random numbers", "Array", true);
}

}  // namespace cells
