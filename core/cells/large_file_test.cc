// Tests for large file loading, quadtree indexing, and viewport queries
// Phase 5: Fix XLSX Stress Test Loading

#include <chrono>
#include <iostream>
#include <string>

#include "core/cells/quadtree.h"
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
// Task 5b: Quadtree indexing test
// ============================================================================

TEST(LargeFileTest, QuadtreeIndexesAllCells) {
    auto result = readXLSX(testFilePath("stress_test.xlsx"));
    ASSERT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);

    // Build quadtree from sheet
    auto buildStart = std::chrono::steady_clock::now();

    Quadtree qt;
    qt.build(*sheet);

    auto buildEnd = std::chrono::steady_clock::now();
    auto buildTime = std::chrono::duration_cast<std::chrono::milliseconds>(buildEnd - buildStart);

    std::cout << "\n=== Quadtree Build Statistics ===\n";
    std::cout << "Build time: " << buildTime.count() << " ms\n";
    std::cout << "Indexed cells: " << qt.count() << "\n";

    // All cells should be indexed
    EXPECT_EQ(qt.count(), sheet->cellCount()) << "Quadtree should index all cells";

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

    // Build quadtree
    Quadtree qt;
    qt.build(*sheet);

    std::cout << "\n=== Viewport Query Tests ===\n";

    // Test 1: Query top-left viewport (0,0 to 10,50)
    {
        auto start = std::chrono::steady_clock::now();
        auto entries = qt.query(0, 0, 10, 50);
        auto end = std::chrono::steady_clock::now();
        auto queryTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "Top-left viewport (0,0)-(10,50): " << entries.size() << " cells, "
                  << queryTime.count() << " us\n";

        // Should find cells in this region (all 8 columns x 50 rows)
        // Since only columns 0-7 have data, expect up to 8*50 = 400 cells
        EXPECT_GT(entries.size(), 0u) << "Should find cells in top-left viewport";
        EXPECT_LE(entries.size(), 400u) << "Should not exceed 400 cells in 8x50 region";

        // Query time should be fast (<10ms)
        EXPECT_LT(queryTime.count(), 10000) << "Query should be <10ms";
    }

    // Test 2: Query bottom rows viewport (0, 65400 to 10, 65536)
    {
        auto start = std::chrono::steady_clock::now();
        auto entries = qt.query(0, 65400, 10, 65536);
        auto end = std::chrono::steady_clock::now();
        auto queryTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "Bottom viewport (0,65400)-(10,65536): " << entries.size() << " cells, "
                  << queryTime.count() << " us\n";

        // Should find cells in the bottom region
        EXPECT_GT(entries.size(), 0u) << "Should find cells at bottom of spreadsheet";

        // Query time should be fast (<10ms)
        EXPECT_LT(queryTime.count(), 10000) << "Query should be <10ms";

        // Verify cells are actually in the expected range
        for (const auto& entry : entries) {
            EXPECT_GE(entry.y, 65400u) << "Cell row should be >= 65400";
            EXPECT_LT(entry.y, 65536u) << "Cell row should be < 65536";
            EXPECT_LT(entry.x, 10u) << "Cell column should be < 10";
        }
    }

    // Test 3: Query last row specifically (0, 65535 to 8, 65536)
    {
        auto entries = qt.query(0, 65535, 8, 65536);
        std::cout << "Last row (65535): " << entries.size() << " cells\n";

        // All 8 columns in the last row should have cells
        EXPECT_EQ(entries.size(), 8u) << "Should find 8 cells in last row";

        // All cells should be in row 65535
        for (const auto& entry : entries) {
            EXPECT_EQ(entry.y, 65535u) << "All cells should be in row 65535";
        }
    }

    // Test 4: Query middle viewport (0, 30000 to 8, 30050)
    {
        auto start = std::chrono::steady_clock::now();
        auto entries = qt.query(0, 30000, 8, 30050);
        auto end = std::chrono::steady_clock::now();
        auto queryTime = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        std::cout << "Middle viewport (0,30000)-(8,30050): " << entries.size() << " cells, "
                  << queryTime.count() << " us\n";

        // Should find ~400 cells (8 columns x 50 rows)
        EXPECT_GT(entries.size(), 0u) << "Should find cells in middle viewport";

        // Query time should be fast
        EXPECT_LT(queryTime.count(), 10000) << "Query should be <10ms";
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
