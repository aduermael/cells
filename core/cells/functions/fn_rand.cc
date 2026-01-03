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

void registerRandFunctions(FunctionRegistry& registry) {
    // Register as volatile functions - they recalculate on every sheet change
    registry.registerFunction("RAND", fn_RAND, "()", "Returns a random number between 0 and 1",
                              "Math", true);
    registry.registerFunction("RANDBETWEEN", fn_RANDBETWEEN, "(bottom, top)",
                              "Returns a random integer in a range", "Math", true);
}

}  // namespace cells
