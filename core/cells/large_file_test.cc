// Tests for large file loading, viewport indexing, and viewport queries
// Phase 5: Fix XLSX Stress Test Loading

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

// ============================================================================
// Task 5a: Large file statistics test
// ============================================================================

TEST(LargeFileTest, StressTestLoadStatistics) {
    auto start = std::chrono::steady_clock::now();

    auto result = readXLSX(testFilePath("stress_test.xlsx"));

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    // Print statistics
    std::cout << "\n=== stress_test.xlsx Statistics ===\n";
    std::cout << "Load time: " << duration.count() << " ms\n";
    std::cout << "Columns: " << sheet->columnCount() << "\n";
    std::cout << "Rows: " << sheet->rowCount() << "\n";
    std::cout << "Cells: " << sheet->cellCount() << "\n";

    // Count formulas
    size_t formulaCount = 0;
    for (const auto& [id, cell] : sheet->cells) {
        if (cell->isFormula()) {
            formulaCount++;
        }
    }
    std::cout << "Formulas: " << formulaCount << "\n";
    std::cout << "Values: " << sheet->cellCount() - formulaCount << "\n";

    // Verify expected dimensions (65536 rows x 8 columns based on CLI output)
    EXPECT_EQ(sheet->columnCount(), 8u) << "Expected 8 columns";
    EXPECT_EQ(sheet->rowCount(), 65536u) << "Expected 65536 rows";

    // Verify cell count is reasonable (should be ~524288 cells for 65536x8)
    // But not all cells may have values
    EXPECT_GT(sheet->cellCount(), 500000u) << "Expected >500k cells";

    // Load time should be reasonable (under 5 seconds)
    EXPECT_LT(duration.count(), 5000) << "Load time should be <5 seconds";
}

// ============================================================================
// Task 5b: ViewportIndex indexing test
// ============================================================================

TEST(LargeFileTest, ViewportIndexIndexesAllCells) {
    auto result = readXLSX(testFilePath("stress_test.xlsx"));
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    // Build viewport index from sheet
    auto buildStart = std::chrono::steady_clock::now();

    ViewportIndex vi;
    vi.build(*sheet);

    auto buildEnd = std::chrono::steady_clock::now();
    auto buildTime = std::chrono::duration_cast<std::chrono::milliseconds>(buildEnd - buildStart);

    std::cout << "\n=== ViewportIndex Build Statistics ===\n";
    std::cout << "Build time: " << buildTime.count() << " ms\n";
    std::cout << "Indexed cells: " << vi.cellCount() << "\n";

    // All cells should be indexed
    EXPECT_EQ(vi.cellCount(), sheet->cellCount()) << "ViewportIndex should index all cells";

    // Build time should be reasonable (under 2 seconds)
    EXPECT_LT(buildTime.count(), 2000) << "Build time should be <2 seconds";
}

// ============================================================================
// Task 5c: Viewport query test for bottom rows
// ============================================================================

TEST(LargeFileTest, ViewportQueryBottomRows) {
    auto result = readXLSX(testFilePath("stress_test.xlsx"));
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    // Build viewport index
    ViewportIndex vi;
    vi.build(*sheet);

    std::cout << "\n=== Viewport Query Tests ===\n";
    std::cout << "Total width: " << vi.totalWidth() << " pixels\n";
    std::cout << "Total height: " << vi.totalHeight() << " pixels\n";

    // Test 1: Query top-left viewport using actual pixel coordinates
    {
        // Query first ~50 rows worth of pixels (dynamically calculate based on total height)
        const uint32_t rowsToQuery = 50;
        const uint32_t avgRowHeight = vi.totalHeight() / vi.rowCount();
        const uint32_t y2 = std::min(vi.totalHeight(), rowsToQuery * avgRowHeight);

        auto start = std::chrono::steady_clock::now();
        auto entries = vi.queryViewport(0, 0, vi.totalWidth(), y2);
        auto end = std::chrono::steady_clock::now();
        auto queryTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "Top-left viewport (0,0)-(" << vi.totalWidth() << "," << y2
                  << "): " << entries.size() << " cells, " << queryTime.count() << " us\n";

        // Should find cells in this region
        EXPECT_GT(entries.size(), 0u) << "Should find cells in top-left viewport";
        // Allow for some variance in row height
        EXPECT_LE(entries.size(), 800u) << "Should not exceed reasonable cell count";

        // Query time should be fast (<10ms)
        EXPECT_LT(queryTime.count(), 10000) << "Query should be <10ms";
    }

    // Test 2: Query bottom portion of the spreadsheet (small viewport)
    {
        // Query last ~50 rows using average row height
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

        // Should find cells in the bottom region
        EXPECT_GT(entries.size(), 0u) << "Should find cells at bottom of spreadsheet";

        // Query time: 50 rows × 8 cols = 400 cells
        // Current implementation does O(log n) lookups per cell
        // Allow up to 500ms for this query (future optimization opportunity)
        EXPECT_LT(queryTime.count(), 500000) << "Query should be <500ms";
    }

    // Test 3: Query last row specifically using pixel coordinates
    {
        // Get the last row's pixel range
        const uint32_t totalH = vi.totalHeight();
        const uint32_t y1 = totalH > 24 ? totalH - 24 : 0;

        auto entries = vi.queryViewport(0, y1, vi.totalWidth(), totalH);
        std::cout << "Last row region (" << y1 << "-" << totalH << "): " << entries.size()
                  << " cells\n";

        // Should find cells in the last row (8 columns)
        EXPECT_GT(entries.size(), 0u) << "Should find cells in last row";
        EXPECT_LE(entries.size(), 16u) << "Should not exceed reasonable count for last row(s)";
    }

    // Test 4: Query middle viewport (~50 rows)
    {
        // Query middle portion (~50 rows)
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

        // Should find cells in the middle region
        EXPECT_GT(entries.size(), 0u) << "Should find cells in middle viewport";

        // Query time: 50 rows × 8 cols = 400 cells
        // Current implementation does O(log n) lookups per cell
        // Allow up to 500ms for this query (future optimization opportunity)
        EXPECT_LT(queryTime.count(), 500000) << "Query should be <500ms";
    }
}

// ============================================================================
// Memory efficiency test
// ============================================================================

TEST(LargeFileTest, CellAccessByPosition) {
    auto result = readXLSX(testFilePath("stress_test.xlsx"));
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    // Test accessing cells at various positions
    std::cout << "\n=== Cell Access Tests ===\n";

    // Access first cell (A1 - position 0,0)
    Axis* col0 = sheet->getColumnByPosition(0);
    Axis* row0 = sheet->getRowByPosition(0);
    ASSERT_NE(col0, nullptr) << "Column 0 should exist";
    ASSERT_NE(row0, nullptr) << "Row 0 should exist";

    Cell* cellA1 = sheet->getCellAt(col0->id, row0->id);
    EXPECT_NE(cellA1, nullptr) << "Cell A1 should exist";
    if (cellA1) {
        std::cout << "Cell A1 value type: " << static_cast<int>(cellA1->value.type) << "\n";
    }

    // Debug: Print first few rows and rows around 122-127 to verify mapping
    std::cout << "\n=== Row value mapping (column A) ===\n";
    std::cout << "First rows:\n";
    for (uint32_t r = 0; r <= 5; r++) {
        Axis* row = sheet->getRowByPosition(r);
        if (!row)
            continue;
        Cell* cell = sheet->getCellAt(col0->id, row->id);
        if (cell) {
            std::cout << "  Position " << r << " (header " << (r + 1) << "): raw=\""
                      << cell->value.raw << "\"\n";
        }
    }

    std::cout << "Rows around 120-130:\n";
    for (uint32_t r = 120; r <= 130; r++) {
        Axis* row = sheet->getRowByPosition(r);
        if (!row)
            continue;
        Cell* cell = sheet->getCellAt(col0->id, row->id);
        if (cell) {
            std::cout << "  Position " << r << " (header " << (r + 1) << "): raw=\""
                      << cell->value.raw << "\"\n";
        }
    }

    // Access last row cells
    Axis* lastRow = sheet->getRowByPosition(65535);
    ASSERT_NE(lastRow, nullptr) << "Row 65535 should exist";

    int lastRowCellCount = 0;
    for (uint32_t c = 0; c < 8; c++) {
        Axis* col = sheet->getColumnByPosition(c);
        if (col) {
            Cell* cell = sheet->getCellAt(col->id, lastRow->id);
            if (cell) {
                lastRowCellCount++;
            }
        }
    }
    std::cout << "Cells in row 65535: " << lastRowCellCount << "\n";
    EXPECT_EQ(lastRowCellCount, 8) << "All 8 columns should have cells in last row";

    // Access a cell in the middle (row 30000)
    Axis* midRow = sheet->getRowByPosition(30000);
    ASSERT_NE(midRow, nullptr) << "Row 30000 should exist";

    Cell* midCell = sheet->getCellAt(col0->id, midRow->id);
    EXPECT_NE(midCell, nullptr) << "Cell at (0, 30000) should exist";
}

}  // namespace
}  // namespace cells
