// Default-green gtest: corpus drives the shipped evaluator; remaining is OK.

#include "core/cells/formula_functions.h"
#include "core/cells/formula_todo.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

TEST(FormulaTodo, CorpusDrivesShippedEvaluator) {
    CorpusReport report = run_formula_todo_corpus();
    ASSERT_TRUE(report.error.empty()) << report.error;
    ASSERT_FALSE(report.outcomes.empty());
    EXPECT_TRUE(report.sum_passed) << "seed SUM case must pass via shipped evaluateCell";
    EXPECT_GT(report.remaining, 0) << "suite should list unimplemented mog functions as remaining";
    EXPECT_EQ(report.fail, 0) << "implemented formulas that disagree with expected values";
}

TEST(FormulaTodo, RegistryExistsForSum) {
    EXPECT_TRUE(FunctionRegistry::instance().exists("SUM"));
}

#ifdef CELLS_FORMULA_TODO_STRICT
TEST(FormulaTodoStrict, RemainingCountIsVisible) {
    CorpusReport report = run_formula_todo_corpus();
    ASSERT_TRUE(report.error.empty()) << report.error;
    // Strict target exists for automation that wants a non-zero exit when work
    // remains. Prefer `bazel run :formula-todo` for the human report.
    EXPECT_EQ(report.remaining, 0) << report.remaining << " functions/cases still remaining";
    EXPECT_EQ(report.fail, 0);
}
#endif

}  // namespace
}  // namespace cells
