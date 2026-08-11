#include "apps/ios/cells_xlsx.h"

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace {

std::string testFilePath(const std::string& filename) {
    return "testdata/xlsx/" + filename;
}

std::string tempOutPath(const std::string& name) {
    // TEST_TMPDIR is set by Bazel test; fall back for local runs.
    const char* tmp = std::getenv("TEST_TMPDIR");
    std::string dir = (tmp != nullptr && tmp[0] != '\0') ? tmp : "/tmp";
    return dir + "/cells_xlsx_" + name;
}

std::vector<char> readFileBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

class WorkbookGuard {
public:
    explicit WorkbookGuard(CellsXlsxWorkbook* wb) : wb_(wb) {}
    ~WorkbookGuard() { cells_xlsx_close(wb_); }
    CellsXlsxWorkbook* get() const { return wb_; }
    CellsXlsxWorkbook* release() {
        CellsXlsxWorkbook* p = wb_;
        wb_ = nullptr;
        return p;
    }

private:
    CellsXlsxWorkbook* wb_;
};

}  // namespace

TEST(CellsXlsxApiTest, OpenFixtureReadsSheetsAndCells) {
    WorkbookGuard guard(cells_xlsx_open(testFilePath("simple.xlsx").c_str()));
    ASSERT_NE(guard.get(), nullptr) << cells_xlsx_last_error();

    EXPECT_EQ(cells_xlsx_sheet_count(guard.get()), 1);

    char name[64] = {};
    ASSERT_GE(cells_xlsx_sheet_name(guard.get(), 0, name, sizeof(name)), 0);
    EXPECT_STREQ(name, "Sheet1");

    // Header row in fixture: Name, Age, Score
    EXPECT_EQ(cells_xlsx_get_type(guard.get(), 0, 0, 0), CELLS_XLSX_VALUE_STRING);
    const char* a1 = cells_xlsx_get_string(guard.get(), 0, 0, 0);
    ASSERT_NE(a1, nullptr);
    EXPECT_STREQ(a1, "Name");

    EXPECT_EQ(cells_xlsx_get_type(guard.get(), 0, 0, 1), CELLS_XLSX_VALUE_STRING);
    const char* a2 = cells_xlsx_get_string(guard.get(), 0, 0, 1);
    ASSERT_NE(a2, nullptr);
    EXPECT_STREQ(a2, "Alice");

    // Age for Alice is numeric
    EXPECT_EQ(cells_xlsx_get_type(guard.get(), 0, 1, 1), CELLS_XLSX_VALUE_NUMBER);
    EXPECT_DOUBLE_EQ(cells_xlsx_get_number(guard.get(), 0, 1, 1), 25.0);
}

TEST(CellsXlsxApiTest, OpenFromMemory) {
    const std::string path = testFilePath("simple.xlsx");
    std::vector<char> bytes = readFileBytes(path);
    ASSERT_FALSE(bytes.empty());

    WorkbookGuard guard(cells_xlsx_open_bytes(bytes.data(), bytes.size()));
    ASSERT_NE(guard.get(), nullptr) << cells_xlsx_last_error();
    EXPECT_EQ(cells_xlsx_sheet_count(guard.get()), 1);

    const char* a1 = cells_xlsx_get_string(guard.get(), 0, 0, 0);
    ASSERT_NE(a1, nullptr);
    EXPECT_STREQ(a1, "Name");
}

TEST(CellsXlsxApiTest, WriteAndRereadRoundTrip) {
    WorkbookGuard src(cells_xlsx_open(testFilePath("simple.xlsx").c_str()));
    ASSERT_NE(src.get(), nullptr) << cells_xlsx_last_error();

    const std::string out = tempOutPath("roundtrip.xlsx");
    ASSERT_EQ(cells_xlsx_write(src.get(), out.c_str()), 0) << cells_xlsx_last_error();

    WorkbookGuard reread(cells_xlsx_open(out.c_str()));
    ASSERT_NE(reread.get(), nullptr) << cells_xlsx_last_error();
    EXPECT_EQ(cells_xlsx_sheet_count(reread.get()), 1);

    const char* a1 = cells_xlsx_get_string(reread.get(), 0, 0, 0);
    ASSERT_NE(a1, nullptr);
    EXPECT_STREQ(a1, "Name");
    const char* a2 = cells_xlsx_get_string(reread.get(), 0, 0, 1);
    ASSERT_NE(a2, nullptr);
    EXPECT_STREQ(a2, "Alice");
    EXPECT_DOUBLE_EQ(cells_xlsx_get_number(reread.get(), 0, 1, 1), 25.0);

    std::remove(out.c_str());
}

TEST(CellsXlsxApiTest, CreateSetWriteReread) {
    WorkbookGuard wb(cells_xlsx_create());
    ASSERT_NE(wb.get(), nullptr) << cells_xlsx_last_error();
    EXPECT_EQ(cells_xlsx_sheet_count(wb.get()), 1);

    ASSERT_EQ(cells_xlsx_set_string(wb.get(), 0, 0, 0, "hello"), 0);
    ASSERT_EQ(cells_xlsx_set_number(wb.get(), 0, 1, 0, 42.5), 0);
    ASSERT_EQ(cells_xlsx_set_bool(wb.get(), 0, 2, 0, 1), 0);

    const std::string out = tempOutPath("created.xlsx");
    ASSERT_EQ(cells_xlsx_write(wb.get(), out.c_str()), 0) << cells_xlsx_last_error();

    WorkbookGuard reread(cells_xlsx_open(out.c_str()));
    ASSERT_NE(reread.get(), nullptr) << cells_xlsx_last_error();
    EXPECT_STREQ(cells_xlsx_get_string(reread.get(), 0, 0, 0), "hello");
    EXPECT_DOUBLE_EQ(cells_xlsx_get_number(reread.get(), 0, 1, 0), 42.5);
    EXPECT_EQ(cells_xlsx_get_type(reread.get(), 0, 2, 0), CELLS_XLSX_VALUE_BOOL);
    EXPECT_EQ(cells_xlsx_get_bool(reread.get(), 0, 2, 0), 1);

    std::remove(out.c_str());
}

TEST(CellsXlsxApiTest, MissingFileReturnsError) {
    CellsXlsxWorkbook* wb = cells_xlsx_open(testFilePath("does_not_exist.xlsx").c_str());
    EXPECT_EQ(wb, nullptr);
    const char* err = cells_xlsx_last_error();
    ASSERT_NE(err, nullptr);
    EXPECT_NE(err[0], '\0');
}

TEST(CellsXlsxApiTest, NullPathReturnsError) {
    EXPECT_EQ(cells_xlsx_open(nullptr), nullptr);
    EXPECT_NE(cells_xlsx_last_error()[0], '\0');

    WorkbookGuard wb(cells_xlsx_create());
    ASSERT_NE(wb.get(), nullptr);
    EXPECT_EQ(cells_xlsx_write(wb.get(), nullptr), -1);
    EXPECT_NE(cells_xlsx_last_error()[0], '\0');
}

TEST(CellsXlsxApiTest, InvalidHandleDoesNotCrash) {
    EXPECT_EQ(cells_xlsx_sheet_count(nullptr), -1);
    EXPECT_EQ(cells_xlsx_write(nullptr, "/tmp/nope.xlsx"), -1);
    EXPECT_EQ(cells_xlsx_get_type(nullptr, 0, 0, 0), CELLS_XLSX_VALUE_OTHER);
    cells_xlsx_close(nullptr);  // no-op
}
