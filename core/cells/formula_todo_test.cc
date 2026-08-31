// Mog-derived formula TODO suite.
// Drives the shipped parse → resolve → evaluateCell (recalc) path.
// Default target stays green: supported cases must pass; unimplemented are remaining.
// formula_todo_strict_test (manual) fails remaining cases as a live TODO list.

#include "core/cells/formula_eval.h"
#include "core/cells/formula_functions.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "gtest/gtest.h"

namespace cells {
namespace {

struct FormulaCase {
    std::string id;
    std::string formula;
    std::string cells;  // A1=n:1;B1=s:hi
    std::string expected;
};

std::string find_cases_path() {
    const char* srcdir = std::getenv("TEST_SRCDIR");
    std::vector<std::string> candidates;
    if (srcdir != nullptr) {
        candidates.push_back(std::string(srcdir) + "/_main/testdata/formulas/mog_cases.tsv");
        candidates.push_back(std::string(srcdir) + "/testdata/formulas/mog_cases.tsv");
    }
    if (const char* runfiles = std::getenv("RUNFILES_DIR")) {
        candidates.push_back(std::string(runfiles) + "/_main/testdata/formulas/mog_cases.tsv");
    }
    candidates.push_back("testdata/formulas/mog_cases.tsv");
    candidates.push_back("core/testdata/formulas/mog_cases.tsv");
    for (const auto& p : candidates) {
        std::ifstream in(p);
        if (in) {
            return p;
        }
    }
    return "testdata/formulas/mog_cases.tsv";
}

std::vector<FormulaCase> load_cases() {
    std::vector<FormulaCase> out;
    std::ifstream in(find_cases_path());
    if (!in) {
        return out;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::vector<std::string> cols;
        std::string cur;
        for (char c : line) {
            if (c == '\t') {
                cols.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        cols.push_back(cur);
        if (cols.size() < 4) {
            continue;
        }
        FormulaCase c;
        c.id = cols[0];
        c.formula = cols[1];
        c.cells = cols[2];
        c.expected = cols[3];
        out.push_back(std::move(c));
    }
    return out;
}

bool parse_a1(const std::string& ref, uint32_t& col, uint32_t& row) {
    size_t i = 0;
    uint32_t c = 0;
    while (i < ref.size() && std::isalpha(static_cast<unsigned char>(ref[i]))) {
        c = c * 26 + static_cast<uint32_t>(std::toupper(static_cast<unsigned char>(ref[i])) - 'A' + 1);
        ++i;
    }
    if (c == 0 || i >= ref.size()) {
        return false;
    }
    uint32_t r = 0;
    while (i < ref.size() && std::isdigit(static_cast<unsigned char>(ref[i]))) {
        r = r * 10 + static_cast<uint32_t>(ref[i] - '0');
        ++i;
    }
    if (r == 0) {
        return false;
    }
    col = c - 1;
    row = r - 1;
    return true;
}

struct Harness {
    std::unique_ptr<Workbook> workbook;
    Sheet* sheet = nullptr;
    ID colIds[26]{};
    ID rowIds[100]{};

    Harness() {
        workbook = std::make_unique<Workbook>(generate_id(), "Todo");
        workbook->addSheet(std::make_unique<Sheet>(generate_id(), "Sheet1"));
        sheet = workbook->getSheetByIndex(0);
        for (uint32_t i = 0; i < 26; i++) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = i;
            col->name = Sheet::positionToColumnName(i);
            colIds[i] = col->id;
            sheet->addColumn(std::move(col));
        }
        for (uint32_t i = 0; i < 100; i++) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = i;
            rowIds[i] = row->id;
            sheet->addRow(std::move(row));
        }
    }

    Cell* cell_at(uint32_t col, uint32_t row) {
        return sheet->getOrCreateCellAt(colIds[col], rowIds[row]);
    }

    void apply_cells(const std::string& spec) {
        if (spec.empty()) {
            return;
        }
        std::stringstream ss(spec);
        std::string part;
        while (std::getline(ss, part, ';')) {
            auto eq = part.find('=');
            if (eq == std::string::npos) {
                continue;
            }
            std::string a1 = part.substr(0, eq);
            std::string val = part.substr(eq + 1);
            uint32_t col = 0;
            uint32_t row = 0;
            if (!parse_a1(a1, col, row) || col >= 26 || row >= 100) {
                continue;
            }
            Cell* cell = cell_at(col, row);
            if (val.rfind("n:", 0) == 0) {
                cell->value = CellValue(std::stod(val.substr(2)));
            } else if (val.rfind("s:", 0) == 0) {
                cell->value = CellValue(val.substr(2));
            } else if (val.rfind("b:", 0) == 0) {
                cell->value = CellValue(val.substr(2) == "true");
            }
        }
    }

    EvalResult eval_formula(const std::string& formula) {
        std::string text = formula;
        if (!text.empty() && text[0] == '=') {
            text = text.substr(1);
        }
        Cell* cell = cell_at(25, 99);  // Z100
        FormulaParser parser(text);
        auto ast = parser.parse();
        if (!ast || parser.hasErrors()) {
            return EvalResult::Error(CellError::VALUE);
        }
        FormulaResolver resolver(*workbook, *sheet, workbook->getNamedRanges());
        RequiredEntities required = resolver.getRequiredEntities(ast.get());
        for (const auto& pending : required.columns) {
            if (sheet->getColumn(pending.id) == nullptr && pending.position < 26) {
                (void)pending;
            }
        }
        resolver.resolve(ast.get());
        auto set = sheet->setCellFormula(cell->id, text, ast.release());
        if (!set.success) {
            return EvalResult::Error(CellError::VALUE);
        }
        return evaluateCell(sheet, cell);
    }
};

std::string format_result(const EvalResult& r) {
    switch (r.type) {
        case EvalResult::Type::NUMBER: {
            std::ostringstream o;
            o << "n:" << r.numberValue;
            return o.str();
        }
        case EvalResult::Type::STRING:
            return "s:" + r.stringValue;
        case EvalResult::Type::BOOLEAN:
            return r.boolValue ? "b:true" : "b:false";
        case EvalResult::Type::ERROR:
            switch (r.error) {
                case CellError::NAME:
                    return "e:NAME";
                case CellError::VALUE:
                    return "e:VALUE";
                case CellError::DIV:
                    return "e:DIV";
                case CellError::NUM:
                    return "e:NUM";
                case CellError::NA:
                    return "e:NA";
                case CellError::REF:
                    return "e:REF";
                default:
                    return "e:OTHER";
            }
        case EvalResult::Type::EMPTY:
            return "empty";
        default:
            return "other";
    }
}

bool matches_expected(const EvalResult& r, const std::string& expected) {
    if (expected == "implemented") {
        return !(r.type == EvalResult::Type::ERROR && r.error == CellError::NAME);
    }
    if (expected.rfind("n:", 0) == 0) {
        if (r.type != EvalResult::Type::NUMBER) {
            return false;
        }
        const double want = std::stod(expected.substr(2));
        return std::fabs(r.numberValue - want) < 1e-9;
    }
    return format_result(r) == expected;
}

enum class CaseStatus { kPass, kRemaining, kFail };

CaseStatus classify(const FormulaCase& c, const EvalResult& r) {
    if (matches_expected(r, c.expected)) {
        return CaseStatus::kPass;
    }
    if (r.type == EvalResult::Type::ERROR && r.error == CellError::NAME) {
        return CaseStatus::kRemaining;
    }
    if (c.expected == "implemented") {
        return CaseStatus::kRemaining;
    }
    return CaseStatus::kFail;
}

const char* status_name(CaseStatus s) {
    switch (s) {
        case CaseStatus::kPass:
            return "PASS";
        case CaseStatus::kRemaining:
            return "REMAINING";
        case CaseStatus::kFail:
            return "FAIL";
    }
    return "?";
}

TEST(FormulaTodo, CorpusDrivesShippedEvaluator) {
    auto cases = load_cases();
    ASSERT_FALSE(cases.empty()) << "mog_cases.tsv not found in runfiles";

    int pass = 0;
    int remaining = 0;
    int fail = 0;
    bool sum_passed = false;

    for (const auto& c : cases) {
        Harness harness;
        harness.apply_cells(c.cells);
        EvalResult r = harness.eval_formula(c.formula);
        CaseStatus st = classify(c, r);
        std::cout << status_name(st) << "\t" << c.id << "\t" << c.formula << "\t got="
                  << format_result(r) << " want=" << c.expected << "\n";
        if (st == CaseStatus::kPass) {
            ++pass;
            if (c.id.find("SUM.") == 0) {
                sum_passed = true;
            }
        } else if (st == CaseStatus::kRemaining) {
            ++remaining;
        } else {
            ++fail;
        }
#ifdef CELLS_FORMULA_TODO_STRICT
        EXPECT_EQ(st, CaseStatus::kPass) << c.id << " " << c.formula
                                         << " got=" << format_result(r);
#endif
    }

    std::cout << "formula-todo summary pass=" << pass << " remaining=" << remaining
              << " fail=" << fail << " total=" << cases.size() << "\n";

    EXPECT_TRUE(sum_passed) << "seed SUM case must pass via shipped evaluateCell";
    EXPECT_GT(remaining, 0) << "suite should list unimplemented mog functions as remaining";
#ifndef CELLS_FORMULA_TODO_STRICT
    // Default :check path: remaining is the TODO list, not a red build.
    EXPECT_EQ(fail, 0) << "implemented formulas that disagree with expected values";
#endif
}

TEST(FormulaTodo, RegistryExistsForSum) {
    EXPECT_TRUE(FunctionRegistry::instance().exists("SUM"));
}

}  // namespace
}  // namespace cells
