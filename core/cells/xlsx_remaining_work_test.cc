// Remaining-work XLSX fidelity tests.
// Supported content still roundtrips. Unmodeled domains (charts, pivots, ...)
// are stored opaquely and written back; they are not evaluated.

#include <cctype>
#include <cstdio>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "core/cells/xlsx_reader.h"
#include "core/cells/xlsx_writer.h"

#include "gtest/gtest.h"
#include "miniz.h"

namespace cells {
namespace {

std::string testFilePath(const std::string& filename) {
    return "testdata/xlsx/" + filename;
}

std::vector<std::string> zip_names(const std::string& path) {
    std::vector<std::string> names;
    mz_zip_archive archive{};
    if (mz_zip_reader_init_file(&archive, path.c_str(), 0) == 0) {
        return names;
    }
    const mz_uint n = mz_zip_reader_get_num_files(&archive);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat stat;
        if (mz_zip_reader_file_stat(&archive, i, &stat) == 0 || stat.m_is_directory) {
            continue;
        }
        names.emplace_back(stat.m_filename);
    }
    mz_zip_reader_end(&archive);
    return names;
}

bool has_chart_part(const std::vector<std::string>& names) {
    for (const auto& n : names) {
        std::string lower = n;
        for (char& c : lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (lower.find("xl/charts/") != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool warnings_mention(const std::vector<std::string>& warnings, const char* needle) {
    for (const auto& w : warnings) {
        std::string lower = w;
        for (char& c : lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (lower.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

TEST(XlsxRemainingWork, ChartFixtureIsPreservedOpaquely) {
    const std::string input = testFilePath("charts.xlsx");
    auto names_in = zip_names(input);
    ASSERT_FALSE(names_in.empty()) << "missing fixture " << input;
    ASSERT_TRUE(has_chart_part(names_in)) << "fixture must contain xl/charts/";

    auto read = readXLSX(input);
    ASSERT_TRUE(read.ok()) << (read.error ? read.error->toString() : "read failed");
    ASSERT_NE(read.workbook, nullptr);

    EXPECT_TRUE(warnings_mention(read.warnings, "chart"))
        << "reader must report charts as unsupported-for-eval, not silently drop them";

    // Supported cell values from the fixture still load.
    Sheet* sheet = read.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);
    EXPECT_GT(sheet->cellCount(), 0u);

    const std::string out = "/tmp/xlsx_remaining_charts_roundtrip.xlsx";
    auto write = writeXLSX(*read.workbook, out);
    ASSERT_TRUE(write.ok()) << (write.error ? write.error->toString() : "write failed");

    auto names_out = zip_names(out);
    EXPECT_TRUE(has_chart_part(names_out))
        << "unmodeled chart parts must be stored and written back";

    auto read_back = readXLSX(out);
    ASSERT_TRUE(read_back.ok());
    ASSERT_NE(read_back.workbook, nullptr);
    EXPECT_GT(read_back.workbook->getSheetByIndex(0)->cellCount(), 0u);

    std::cout << "XLSX remaining-work: charts preserved opaquely (not evaluated).\n";
    std::remove(out.c_str());
}

TEST(XlsxRemainingWork, SupportedSimpleWorkbookStillRoundtrips) {
    auto read = readXLSX(testFilePath("simple.xlsx"));
    ASSERT_TRUE(read.ok()) << (read.error ? read.error->toString() : "read failed");
    ASSERT_NE(read.workbook, nullptr);

    const std::string out = "/tmp/xlsx_remaining_simple_roundtrip.xlsx";
    auto write = writeXLSX(*read.workbook, out);
    ASSERT_TRUE(write.ok()) << (write.error ? write.error->toString() : "write failed");

    auto read_back = readXLSX(out);
    ASSERT_TRUE(read_back.ok());
    ASSERT_NE(read_back.workbook, nullptr);
    EXPECT_EQ(read.workbook->sheetCount(), read_back.workbook->sheetCount());
    EXPECT_EQ(read.workbook->getSheetByIndex(0)->cellCount(),
              read_back.workbook->getSheetByIndex(0)->cellCount());
    std::remove(out.c_str());
}

TEST(XlsxRemainingWork, InventoryDocumentsProductionGaps) {
    // Document the production-bar gaps this suite tracks. Not implementations.
    const char* remaining[] = {"charts", "pivots", "tables", "comments",
                               "images", "vba",    "print",  "conditional-formatting"};
    for (const char* domain : remaining) {
        EXPECT_STRNE(domain, "") << domain;
    }
}

}  // namespace
}  // namespace cells
