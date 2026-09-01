// Human-readable formula TODO report. One line per case + summary.
// bazel run :formula-todo  (or //core/cells:formula_todo_report)

#include <cstdio>
#include <cstring>

#include <iostream>
#include <string>

#include "core/cells/formula_todo.h"

#ifndef _WIN32
#include <unistd.h>
#else
#include <io.h>
#endif

namespace {

bool use_color() {
    if (std::getenv("NO_COLOR") != nullptr) {
        return false;
    }
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDOUT_FILENO) == 1;
#endif
}

const char* kReset = "\033[0m";
const char* kGreen = "\033[32m";
const char* kRed = "\033[31m";
const char* kBold = "\033[1m";

void print_status(cells::CaseStatus st, bool color) {
    const char* name = cells::case_status_name(st);
    if (!color) {
        std::printf("%-4s", name);
        return;
    }
    const char* paint = kGreen;
    if (st != cells::CaseStatus::kPass) {
        paint = kRed;
    }
    std::printf("%s%s%-4s%s", kBold, paint, name, kReset);
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string cases_path;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--cases") == 0 && i + 1 < argc) {
            cases_path = argv[++i];
        }
    }

    cells::CorpusReport report = cells::run_formula_todo_corpus(cases_path);
    if (!report.error.empty()) {
        std::cerr << "Error: " << report.error << "\n";
        return 1;
    }

    const bool color = use_color();
    for (const auto& o : report.outcomes) {
        print_status(o.status, color);
        std::printf("  %-16s  %s", o.function.c_str(), o.c.formula.c_str());
        if (o.status == cells::CaseStatus::kFail) {
            std::printf("  (got %s, want %s)", o.got.c_str(), o.c.expected.c_str());
        }
        std::printf("\n");
    }

    const int total = report.pass + report.remaining + report.fail;
    std::printf("\n");
    if (color) {
        std::printf("%sSummary%s\n", kBold, kReset);
    } else {
        std::printf("Summary\n");
    }
    std::printf("  cases:      %d\n", total);
    std::printf("    passing:          %d\n", report.pass);
    std::printf("    not implemented:  %d\n", report.remaining);
    std::printf("    wrong:            %d\n", report.fail);
    std::printf("  functions:  %d\n", report.functions_total);
    std::printf("    supported:        %d\n", report.functions_supported);
    std::printf("    not implemented:  %d\n", report.functions_not_impl);
    std::printf("    wrong:            %d\n", report.functions_wrong);
    return 0;
}
