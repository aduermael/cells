// Tests for large file loading, viewport indexing, and viewport queries
// Phase 5: Fix XLSX Stress Test Loading
//
// Timing bounds are intentionally generous so shared CI runners under load
// do not flake; they still fail on multi-minute regressions. Functional
// checks (counts, viewport hits) are the primary correctness signal.

#include <chrono>
#include <iostream>
#include <string>

#include "core/cells/viewport_index.h"
#include "core/cells/xlsx_reader.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// Helper to get test file path
std::string testFilePath(const std::string& filename) {
    return "testdata/xlsx/" + filename;
}

// Load once per process: stress_test.xlsx is large; reloading it four times
// multiplies wall time and amplifies CI contention flakes.
class LargeFileTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        auto start = std::chrono::steady_clock::now();
        result_ = readXLSX(testFilePath("stress_test.xlsx"));
        load_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    }

    static void TearDownTestSuite() { result_ = XLSXReadResult{}; }

    void SetUp() override {
        ASSERT_TRUE(result_.ok()) << (result_.error ? result_.error->toString() : "unknown error");
        ASSERT_NE(result_.workbook, nullptr);
        sheet_ = result_.workbook->getSheetByIndex(0);
        ASSERT_NE(sheet_, nullptr);
    }

    static XLSXReadResult result_;
    static int64_t load_ms_;
    Sheet* sheet_ = nullptr;
};

XLSXReadResult LargeFileTest::result_{};
int64_t LargeFileTest::load_ms_ = 0;

// ============================================================================
// Task 5a: Large file statistics test
// ============================================================================

TEST_F(LargeFileTest, StressTestLoadStatistics) {
    std::cout << "\n=== stress_test.xlsx Statistics ===\n";
    std::cout << "Load time: " << load_ms_ << " ms\n";
    std::cout << "Columns: " << sheet_->columnCount() << "\n";
    std::cout << "Rows: " << sheet_->rowCount() << "\n";
    std::cout << "Cells: " << sheet_->cellCount() << "\n";

    size_t formulaCount = 0;
    for (const ID& cellId : sheet_->getCellIds()) {
        const Cell* cell = result_.workbook->getCell(cellId);
        if (cell && cell->isFormula()) {
            formulaCount++;
        }
    }
    std::cout << "Formulas: " << formulaCount << "\n";
    std::cout << "Values: " << sheet_->cellCount() - formulaCount << "\n";

    // Verify expected dimensions (65536 rows x 8 columns based on CLI output)
    EXPECT_EQ(sheet_->columnCount(), 8u) << "Expected 8 columns";
    EXPECT_EQ(sheet_->rowCount(), 65536u) << "Expected 65536 rows";

    // Verify cell count is reasonable (should be ~524288 cells for 65536x8)
    EXPECT_GT(sheet_->cellCount(), 500000u) << "Expected >500k cells";

    // Catastrophic-regression bound (local ~3s; CI under load can be slower)
    EXPECT_LT(load_ms_, 60000) << "Load time should be <60 seconds";
}

// ============================================================================
// Task 5b: ViewportIndex indexing test
// ============================================================================

TEST_F(LargeFileTest, ViewportIndexIndexesAllCells) {
    auto buildStart = std::chrono::steady_clock::now();

    ViewportIndex vi;
    vi.build(*sheet_);

    auto buildTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - buildStart)
                         .count();

    std::cout << "\n=== ViewportIndex Build Statistics ===\n";
    std::cout << "Build time: " << buildTime << " ms\n";
    std::cout << "Indexed cells: " << vi.cellCount() << "\n";

    EXPECT_EQ(vi.cellCount(), sheet_->cellCount()) << "ViewportIndex should index all cells";

    // Catastrophic-regression bound (local ~0.5s)
    EXPECT_LT(buildTime, 30000) << "Build time should be <30 seconds";
}

// ============================================================================
// Task 5c: Viewport query test for bottom rows
// ============================================================================

TEST_F(LargeFileTest, ViewportQueryBottomRows) {
    ViewportIndex vi;
    vi.build(*sheet_);

    std::cout << "\n=== Viewport Query Tests ===\n";
    std::cout << "Total width: " << vi.totalWidth() << " pixels\n";
    std::cout << "Total height: " << vi.totalHeight() << " pixels\n";

    // Test 1: Query top-left viewport using actual pixel coordinates
    {
        const uint32_t rowsToQuery = 50;
        const uint32_t avgRowHeight = vi.totalHeight() / vi.rowCount();
        const uint32_t y2 = std::min(vi.totalHeight(), rowsToQuery * avgRowHeight);

        auto start = std::chrono::steady_clock::now();
        auto entries = vi.queryViewport(0, 0, vi.totalWidth(), y2);
        auto end = std::chrono::steady_clock::now();
        auto queryTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "Top-left viewport (0,0)-(" << vi.totalWidth() << "," << y2
                  << "): " << entries.size() << " cells, " << queryTime.count() << " us\n";

        EXPECT_GT(entries.size(), 0u) << "Should find cells in top-left viewport";
        EXPECT_LE(entries.size(), 800u) << "Should not exceed reasonable cell count";
        EXPECT_LT(queryTime.count(), 10000) << "Query should be <10ms";
    }

    // Test 2: Query bottom portion of the spreadsheet (small viewport)
    {
        const uint32_t totalH = vi.totalHeight();
        const uint32_t avgRowHeight = totalH / vi.rowCount();
        const uint32_t viewportHeight = 50 * avgRowHeight;
        const uint32_t y1 = totalH > viewportHeight ? totalH - viewportHeight : 0;

        auto start = std::chrono::steady_clock::now();
        auto entries = vi.queryViewport(0, y1, vi.totalWidth(), totalH);
        auto end = std::chrono::steady_clock::now();
        auto queryTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "Bottom viewport (0," << y1 << ")-(" << vi.totalWidth() << "," << totalH
                  << "): " << entries.size() << " cells, " << queryTime.count() << " us\n";

        EXPECT_GT(entries.size(), 0u) << "Should find cells at bottom of spreadsheet";
        // Allow up to 500ms for this query (future optimization opportunity)
        EXPECT_LT(queryTime.count(), 500000) << "Query should be <500ms";
    }

    // Test 3: Query last row specifically using pixel coordinates
    {
        const uint32_t totalH = vi.totalHeight();
        const uint32_t y1 = totalH > 24 ? totalH - 24 : 0;

        auto entries = vi.queryViewport(0, y1, vi.totalWidth(), totalH);
        std::cout << "Last row region (" << y1 << "-" << totalH << "): " << entries.size()
                  << " cells\n";

        EXPECT_GT(entries.size(), 0u) << "Should find cells in last row";
        EXPECT_LE(entries.size(), 16u) << "Should not exceed reasonable count for last row(s)";
    }

    // Test 4: Query middle viewport (~50 rows)
    {
        const uint32_t totalH = vi.totalHeight();
        const uint32_t avgRowHeight = totalH / vi.rowCount();
        const uint32_t viewportHeight = 50 * avgRowHeight;
        const uint32_t midY = totalH / 2;
        const uint32_t y1 = midY - viewportHeight / 2;
        const uint32_t y2 = midY + viewportHeight / 2;

        auto start = std::chrono::steady_clock::now();
        auto entries = vi.queryViewport(0, y1, vi.totalWidth(), y2);
        auto end = std::chrono::steady_clock::now();
        auto queryTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "Middle viewport (0," << y1 << ")-(" << vi.totalWidth() << "," << y2
                  << "): " << entries.size() << " cells, " << queryTime.count() << " us\n";

        EXPECT_GT(entries.size(), 0u) << "Should find cells in middle viewport";
        EXPECT_LT(queryTime.count(), 500000) << "Query should be <500ms";
    }
}

// ============================================================================
// Memory efficiency test
// ============================================================================

TEST_F(LargeFileTest, CellAccessByPosition) {
    std::cout << "\n=== Cell Access Tests ===\n";

    Axis* col0 = sheet_->getColumnByPosition(0);
    Axis* row0 = sheet_->getRowByPosition(0);
    ASSERT_NE(col0, nullptr) << "Column 0 should exist";
    ASSERT_NE(row0, nullptr) << "Row 0 should exist";

    Cell* cellA1 = sheet_->getCellAt(col0->id, row0->id);
    EXPECT_NE(cellA1, nullptr) << "Cell A1 should exist";
    if (cellA1) {
        std::cout << "Cell A1 value type: " << static_cast<int>(cellA1->value.type) << "\n";
    }

    std::cout << "\n=== Row value mapping (column A) ===\n";
    std::cout << "First rows:\n";
    for (uint32_t r = 0; r <= 5; r++) {
        Axis* row = sheet_->getRowByPosition(r);
        if (!row) {
            continue;
        }
        Cell* cell = sheet_->getCellAt(col0->id, row->id);
        if (cell) {
            std::cout << "  Position " << r << " (header " << (r + 1) << "): raw=\""
                      << cell->value.raw << "\"\n";
        }
    }

    std::cout << "Rows around 120-130:\n";
    for (uint32_t r = 120; r <= 130; r++) {
        Axis* row = sheet_->getRowByPosition(r);
        if (!row) {
            continue;
        }
        Cell* cell = sheet_->getCellAt(col0->id, row->id);
        if (cell) {
            std::cout << "  Position " << r << " (header " << (r + 1) << "): raw=\""
                      << cell->value.raw << "\"\n";
        }
    }

    // Access last row cells
    Axis* lastRow = sheet_->getRowByPosition(65535);
    ASSERT_NE(lastRow, nullptr) << "Row 65535 should exist";

    int lastRowCellCount = 0;
    for (uint32_t c = 0; c < 8; c++) {
        Axis* col = sheet_->getColumnByPosition(c);
        if (col) {
            Cell* cell = sheet_->getCellAt(col->id, lastRow->id);
            if (cell) {
                lastRowCellCount++;
            }
        }
    }
    std::cout << "Cells in row 65535: " << lastRowCellCount << "\n";
    EXPECT_EQ(lastRowCellCount, 8) << "All 8 columns should have cells in last row";

    // Access a cell in the middle (row 30000)
    Axis* midRow = sheet_->getRowByPosition(30000);
    ASSERT_NE(midRow, nullptr) << "Row 30000 should exist";

    Cell* midCell = sheet_->getCellAt(col0->id, midRow->id);
    EXPECT_NE(midCell, nullptr) << "Cell at (0, 30000) should exist";
}

}  // namespace
}  // namespace cells
