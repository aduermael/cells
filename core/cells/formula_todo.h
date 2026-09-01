// Mog-derived formula corpus: load cases and drive the shipped evaluator.

#ifndef CELLS_FORMULA_TODO_H_
#define CELLS_FORMULA_TODO_H_

#include <cstdint>

#include <string>
#include <vector>

namespace cells {

struct FormulaCase {
    std::string id;
    std::string formula;
    std::string cells;  // A1=n:1;B1=s:hi
    std::string expected;
};

enum class CaseStatus : std::uint8_t { kPass, kRemaining, kFail };

struct CaseOutcome {
    FormulaCase c;
    CaseStatus status = CaseStatus::kRemaining;
    std::string got;
    std::string function;
};

struct CorpusReport {
    std::vector<CaseOutcome> outcomes;
    int pass = 0;
    int remaining = 0;
    int fail = 0;
    int functions_total = 0;
    int functions_supported = 0;
    int functions_not_impl = 0;
    int functions_wrong = 0;
    bool sum_passed = false;
    std::string error;
};

std::string formula_todo_cases_path();
std::vector<FormulaCase> load_formula_todo_cases(const std::string& path);
CorpusReport run_formula_todo_corpus(const std::string& cases_path = {});
const char* case_status_name(CaseStatus s);

}  // namespace cells

#endif  // CELLS_FORMULA_TODO_H_
