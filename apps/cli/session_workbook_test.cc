#include "session_workbook.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include "core/cells/xlsx_reader.h"

#include "gtest/gtest.h"

namespace cells::cli {
namespace {

std::string temp_path(const std::string& name) {
    static int n = 0;
    return (std::filesystem::temp_directory_path() / ("session_wb_" + std::to_string(++n) + "_" + name))
        .string();
}

TEST(SessionWorkbookTest, LoadEmptyExecTwoOpsExport) {
    SessionWorkbook session;
    std::string err;
    ASSERT_TRUE(session.load("", err)) << err;
    ASSERT_NE(session.workbook(), nullptr);

    ScriptResult first = session.exec("setCell(\"A1\", 10)");
    ASSERT_TRUE(first.success) << first.error;

    ScriptResult second = session.exec("setCell(\"B1\", 20)");
    ASSERT_TRUE(second.success) << second.error;

    const std::string out = temp_path("two_ops.xlsx");
    ASSERT_TRUE(session.export_to(out, "xlsx", err)) << err;

    auto read = readXLSX(out);
    ASSERT_TRUE(read.ok()) << (read.error ? read.error->toString() : "");
    ASSERT_NE(read.workbook, nullptr);
    Sheet* sheet = read.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    bool saw_a = false;
    bool saw_b = false;
    for (const auto& cellId : sheet->getCellIds()) {
        Cell* cell = read.workbook->getCell(cellId);
        if (cell == nullptr) {
            continue;
        }
        const Axis* col = read.workbook->getColumn(cell->colId);
        const Axis* row = read.workbook->getRow(cell->rowId);
        if (col == nullptr || row == nullptr) {
            continue;
        }
        if (col->position == 0 && row->position == 0) {
            EXPECT_DOUBLE_EQ(cell->value.asNumber(), 10.0);
            saw_a = true;
        }
        if (col->position == 1 && row->position == 0) {
            EXPECT_DOUBLE_EQ(cell->value.asNumber(), 20.0);
            saw_b = true;
        }
    }
    EXPECT_TRUE(saw_a) << "exported workbook missing A1=10 from first exec";
    EXPECT_TRUE(saw_b) << "exported workbook missing B1=20 from second exec";
    std::remove(out.c_str());
}

}  // namespace
}  // namespace cells::cli
