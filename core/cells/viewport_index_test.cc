#include "core/cells/viewport_index.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <vector>

#include "core/cells/id.h"
#include "core/cells/model.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// Test fixture with helper methods
class ViewportIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a simple workbook and sheet for testing
        workbook_ = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
        sheet_ = std::make_unique<Sheet>(generate_id(), "TestSheet");
        sheet_->setWorkbook(workbook_.get());  // Set workbook early so cells get stored properly
    }

    // Add columns with specified widths (default 100px each)
    void addColumns(size_t count, uint32_t width = 100) {
        for (size_t i = 0; i < count; i++) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = static_cast<uint32_t>(colIds_.size());
            col->size = width;
            colIds_.push_back(col->id);
            sheet_->addColumn(std::move(col));
        }
    }

    // Add rows with specified heights (default 24px each)
    void addRows(size_t count, uint32_t height = 24) {
        for (size_t i = 0; i < count; i++) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = static_cast<uint32_t>(rowIds_.size());
            row->size = height;
            rowIds_.push_back(row->id);
            sheet_->addRow(std::move(row));
        }
    }

    // Add a cell at a specific column/row position
    Cell* addCell(size_t col, size_t row, const std::string& value = "") {
        auto cell = std::make_unique<Cell>(generate_id(), colIds_[col], rowIds_[row]);
        cell->value = CellValue(value);
        Cell* ptr = cell.get();
        sheet_->addCell(std::move(cell));
        return ptr;
    }

    // Helper to verify index invariants
    void verifyIndex(const ViewportIndex& index) {
        EXPECT_TRUE(index.verify()) << "ViewportIndex invariants violated";
    }

    std::unique_ptr<Workbook> workbook_;
    std::unique_ptr<Sheet> sheet_;
    std::vector<ID> colIds_;
    std::vector<ID> rowIds_;
};

// ============================================================================
// Basic operations
// ============================================================================

TEST_F(ViewportIndexTest, EmptyIndex) {
    ViewportIndex index;
    EXPECT_TRUE(index.empty());
    EXPECT_EQ(index.columnCount(), 0u);
    EXPECT_EQ(index.rowCount(), 0u);
    EXPECT_EQ(index.cellCount(), 0u);
    EXPECT_EQ(index.totalWidth(), 0u);
    EXPECT_EQ(index.totalHeight(), 0u);
    verifyIndex(index);
}

TEST_F(ViewportIndexTest, BuildFromEmptySheet) {
    ViewportIndex index;
    index.build(*sheet_);

    EXPECT_TRUE(index.empty());
    EXPECT_EQ(index.columnCount(), 0u);
    EXPECT_EQ(index.rowCount(), 0u);
    verifyIndex(index);
}

TEST_F(ViewportIndexTest, BuildFromSheetWithColumnsOnly) {
    addColumns(5, 100);

    ViewportIndex index;
    index.build(*sheet_);

    EXPECT_EQ(index.columnCount(), 5u);
    EXPECT_EQ(index.rowCount(), 0u);
    EXPECT_EQ(index.totalWidth(), 500u);
    EXPECT_EQ(index.totalHeight(), 0u);
    verifyIndex(index);
}

TEST_F(ViewportIndexTest, BuildFromSheetWithRowsOnly) {
    addRows(10, 24);

    ViewportIndex index;
    index.build(*sheet_);

    EXPECT_EQ(index.columnCount(), 0u);
    EXPECT_EQ(index.rowCount(), 10u);
    EXPECT_EQ(index.totalWidth(), 0u);
    EXPECT_EQ(index.totalHeight(), 240u);
    verifyIndex(index);
}

TEST_F(ViewportIndexTest, BuildFromSheetWithCells) {
    addColumns(5, 100);
    addRows(10, 24);

    addCell(0, 0, "A1");
    addCell(1, 0, "B1");
    addCell(0, 1, "A2");
    addCell(4, 9, "E10");

    ViewportIndex index;
    index.build(*sheet_);

    EXPECT_EQ(index.columnCount(), 5u);
    EXPECT_EQ(index.rowCount(), 10u);
    EXPECT_EQ(index.cellCount(), 4u);
    EXPECT_EQ(index.totalWidth(), 500u);
    EXPECT_EQ(index.totalHeight(), 240u);
    verifyIndex(index);
}

// ============================================================================
// Coordinate conversion tests
// ============================================================================

TEST_F(ViewportIndexTest, PixelToColumnBasic) {
    addColumns(3, 100);  // 3 columns, 100px each

    ViewportIndex index;
    index.build(*sheet_);

    // Pixel 0 -> column 0
    auto r0 = index.pixelToColumn(0);
    ASSERT_TRUE(r0.has_value());
    EXPECT_EQ(r0->axisId, colIds_[0]);
    EXPECT_EQ(r0->offsetInAxis, 0u);
    EXPECT_EQ(r0->position, 0u);

    // Pixel 50 -> column 0, offset 50
    auto r50 = index.pixelToColumn(50);
    ASSERT_TRUE(r50.has_value());
    EXPECT_EQ(r50->axisId, colIds_[0]);
    EXPECT_EQ(r50->offsetInAxis, 50u);
    EXPECT_EQ(r50->position, 0u);

    // Pixel 100 -> column 1, offset 0
    auto r100 = index.pixelToColumn(100);
    ASSERT_TRUE(r100.has_value());
    EXPECT_EQ(r100->axisId, colIds_[1]);
    EXPECT_EQ(r100->offsetInAxis, 0u);
    EXPECT_EQ(r100->position, 1u);

    // Pixel 250 -> column 2, offset 50
    auto r250 = index.pixelToColumn(250);
    ASSERT_TRUE(r250.has_value());
    EXPECT_EQ(r250->axisId, colIds_[2]);
    EXPECT_EQ(r250->offsetInAxis, 50u);
    EXPECT_EQ(r250->position, 2u);

    // Pixel 300 -> out of range
    auto r300 = index.pixelToColumn(300);
    EXPECT_FALSE(r300.has_value());
}

TEST_F(ViewportIndexTest, PixelToRowBasic) {
    addRows(4, 25);  // 4 rows, 25px each

    ViewportIndex index;
    index.build(*sheet_);

    // Pixel 0 -> row 0
    auto r0 = index.pixelToRow(0);
    ASSERT_TRUE(r0.has_value());
    EXPECT_EQ(r0->axisId, rowIds_[0]);
    EXPECT_EQ(r0->offsetInAxis, 0u);
    EXPECT_EQ(r0->position, 0u);

    // Pixel 25 -> row 1
    auto r25 = index.pixelToRow(25);
    ASSERT_TRUE(r25.has_value());
    EXPECT_EQ(r25->axisId, rowIds_[1]);
    EXPECT_EQ(r25->offsetInAxis, 0u);
    EXPECT_EQ(r25->position, 1u);

    // Pixel 60 -> row 2, offset 10
    auto r60 = index.pixelToRow(60);
    ASSERT_TRUE(r60.has_value());
    EXPECT_EQ(r60->axisId, rowIds_[2]);
    EXPECT_EQ(r60->offsetInAxis, 10u);
    EXPECT_EQ(r60->position, 2u);

    // Pixel 100 -> out of range
    auto r100 = index.pixelToRow(100);
    EXPECT_FALSE(r100.has_value());
}

TEST_F(ViewportIndexTest, ColumnToPixel) {
    addColumns(4, 50);  // 4 columns, 50px each

    ViewportIndex index;
    index.build(*sheet_);

    EXPECT_EQ(*index.columnToPixel(colIds_[0]), 0u);
    EXPECT_EQ(*index.columnToPixel(colIds_[1]), 50u);
    EXPECT_EQ(*index.columnToPixel(colIds_[2]), 100u);
    EXPECT_EQ(*index.columnToPixel(colIds_[3]), 150u);

    // Non-existent column
    EXPECT_FALSE(index.columnToPixel(generate_id()).has_value());
}

TEST_F(ViewportIndexTest, RowToPixel) {
    addRows(3, 30);  // 3 rows, 30px each

    ViewportIndex index;
    index.build(*sheet_);

    EXPECT_EQ(*index.rowToPixel(rowIds_[0]), 0u);
    EXPECT_EQ(*index.rowToPixel(rowIds_[1]), 30u);
    EXPECT_EQ(*index.rowToPixel(rowIds_[2]), 60u);

    // Non-existent row
    EXPECT_FALSE(index.rowToPixel(generate_id()).has_value());
}

TEST_F(ViewportIndexTest, GetColumnWidthAndRowHeight) {
    addColumns(2, 100);
    addRows(2, 24);

    ViewportIndex index;
    index.build(*sheet_);

    EXPECT_EQ(*index.getColumnWidth(colIds_[0]), 100u);
    EXPECT_EQ(*index.getColumnWidth(colIds_[1]), 100u);
    EXPECT_EQ(*index.getRowHeight(rowIds_[0]), 24u);
    EXPECT_EQ(*index.getRowHeight(rowIds_[1]), 24u);

    EXPECT_FALSE(index.getColumnWidth(generate_id()).has_value());
    EXPECT_FALSE(index.getRowHeight(generate_id()).has_value());
}

TEST_F(ViewportIndexTest, GetColumnAtAndRowAt) {
    addColumns(3, 100);
    addRows(2, 24);

    ViewportIndex index;
    index.build(*sheet_);

    EXPECT_EQ(*index.getColumnAt(0), colIds_[0]);
    EXPECT_EQ(*index.getColumnAt(1), colIds_[1]);
    EXPECT_EQ(*index.getColumnAt(2), colIds_[2]);
    EXPECT_FALSE(index.getColumnAt(3).has_value());

    EXPECT_EQ(*index.getRowAt(0), rowIds_[0]);
    EXPECT_EQ(*index.getRowAt(1), rowIds_[1]);
    EXPECT_FALSE(index.getRowAt(2).has_value());
}

// ============================================================================
// Viewport query tests
// ============================================================================

TEST_F(ViewportIndexTest, QueryEmptyViewport) {
    ViewportIndex index;
    auto results = index.queryViewport(0, 0, 800, 600);
    EXPECT_TRUE(results.empty());
}

TEST_F(ViewportIndexTest, QueryNoColumnsOrRows) {
    addColumns(5, 100);
    addRows(10, 24);
    // No cells added

    ViewportIndex index;
    index.build(*sheet_);

    auto results = index.queryViewport(0, 0, 500, 240);
    EXPECT_TRUE(results.empty());
}

TEST_F(ViewportIndexTest, QuerySingleCell) {
    addColumns(5, 100);
    addRows(10, 24);
    Cell* cell = addCell(2, 3, "C4");

    ViewportIndex index;
    index.build(*sheet_);

    // Query covering the cell (col 2: 200-300px, row 3: 72-96px)
    auto results = index.queryViewport(0, 0, 500, 240);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].cell, cell);
    EXPECT_EQ(results[0].x, 200u);
    EXPECT_EQ(results[0].y, 72u);
    EXPECT_EQ(results[0].width, 100u);
    EXPECT_EQ(results[0].height, 24u);
}

TEST_F(ViewportIndexTest, QueryMultipleCells) {
    addColumns(5, 100);
    addRows(5, 20);

    Cell* c1 = addCell(0, 0, "A1");
    Cell* c2 = addCell(1, 0, "B1");
    Cell* c3 = addCell(0, 1, "A2");
    Cell* c4 = addCell(4, 4, "E5");

    ViewportIndex index;
    index.build(*sheet_);

    // Query covering all cells
    auto results = index.queryViewport(0, 0, 500, 100);
    EXPECT_EQ(results.size(), 4u);

    // Query covering only top-left cells
    results = index.queryViewport(0, 0, 200, 40);
    EXPECT_EQ(results.size(), 3u);  // A1, B1, A2

    // Verify the cells are found
    bool foundC1 = false, foundC2 = false, foundC3 = false;
    for (const auto& entry : results) {
        if (entry.cell == c1)
            foundC1 = true;
        if (entry.cell == c2)
            foundC2 = true;
        if (entry.cell == c3)
            foundC3 = true;
    }
    EXPECT_TRUE(foundC1);
    EXPECT_TRUE(foundC2);
    EXPECT_TRUE(foundC3);

    // Query only the bottom-right cell
    results = index.queryViewport(400, 80, 500, 100);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].cell, c4);
}

TEST_F(ViewportIndexTest, QueryPartialOverlap) {
    addColumns(3, 100);  // 0-300px
    addRows(3, 50);      // 0-150px

    addCell(0, 0);  // 0-100, 0-50
    addCell(1, 1);  // 100-200, 50-100
    addCell(2, 2);  // 200-300, 100-150

    ViewportIndex index;
    index.build(*sheet_);

    // Query that partially overlaps middle cell
    auto results = index.queryViewport(50, 25, 150, 75);
    // Should include cells at (0,0) and (1,1) since their ranges overlap
    EXPECT_EQ(results.size(), 2u);

    // Query that only includes middle cell
    results = index.queryViewport(100, 50, 200, 100);
    EXPECT_EQ(results.size(), 1u);
}

TEST_F(ViewportIndexTest, QueryOutsideGrid) {
    addColumns(3, 100);
    addRows(3, 50);
    addCell(1, 1);

    ViewportIndex index;
    index.build(*sheet_);

    // Query completely outside the grid
    auto results = index.queryViewport(500, 200, 600, 300);
    EXPECT_TRUE(results.empty());
}

TEST_F(ViewportIndexTest, GetVisibleColumnRange) {
    addColumns(5, 100);  // 0-500px total

    ViewportIndex index;
    index.build(*sheet_);

    // Viewport covering all columns
    auto [first, last] = index.getVisibleColumnRange(0, 500);
    EXPECT_EQ(first, 0u);
    EXPECT_EQ(last, 4u);

    // Viewport covering columns 1-3 (100-400px)
    auto [first2, last2] = index.getVisibleColumnRange(100, 400);
    EXPECT_EQ(first2, 1u);
    EXPECT_EQ(last2, 3u);

    // Viewport covering partial columns
    auto [first3, last3] = index.getVisibleColumnRange(50, 250);
    EXPECT_EQ(first3, 0u);
    EXPECT_EQ(last3, 2u);
}

TEST_F(ViewportIndexTest, GetVisibleRowRange) {
    addRows(10, 25);  // 0-250px total

    ViewportIndex index;
    index.build(*sheet_);

    // Viewport covering all rows
    auto [first, last] = index.getVisibleRowRange(0, 250);
    EXPECT_EQ(first, 0u);
    EXPECT_EQ(last, 9u);

    // Viewport covering rows 2-5 (50-150px)
    auto [first2, last2] = index.getVisibleRowRange(50, 150);
    EXPECT_EQ(first2, 2u);
    EXPECT_EQ(last2, 5u);
}

// ============================================================================
// Incremental cell update tests
// ============================================================================

TEST_F(ViewportIndexTest, OnCellAdded) {
    addColumns(3, 100);
    addRows(3, 50);

    ViewportIndex index;
    index.build(*sheet_);
    EXPECT_EQ(index.cellCount(), 0u);

    // Add a cell
    Cell* cell = addCell(1, 1, "B2");
    index.onCellAdded(cell);

    EXPECT_EQ(index.cellCount(), 1u);

    auto results = index.queryViewport(0, 0, 300, 150);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].cell, cell);
    verifyIndex(index);
}

TEST_F(ViewportIndexTest, OnCellRemoved) {
    addColumns(3, 100);
    addRows(3, 50);
    Cell* cell = addCell(1, 1, "B2");

    ViewportIndex index;
    index.build(*sheet_);
    EXPECT_EQ(index.cellCount(), 1u);

    index.onCellRemoved(cell);

    EXPECT_EQ(index.cellCount(), 0u);

    auto results = index.queryViewport(0, 0, 300, 150);
    EXPECT_TRUE(results.empty());
    verifyIndex(index);
}

TEST_F(ViewportIndexTest, OnCellRemovedByIds) {
    addColumns(3, 100);
    addRows(3, 50);
    Cell* cell = addCell(1, 1, "B2");

    // Save IDs before removal
    const ID colId = cell->colId;
    const ID rowId = cell->rowId;

    ViewportIndex index;
    index.build(*sheet_);
    EXPECT_EQ(index.cellCount(), 1u);

    // Use ID-based removal (simulates deletion after cell freed)
    index.onCellRemoved(colId, rowId);

    EXPECT_EQ(index.cellCount(), 0u);

    auto results = index.queryViewport(0, 0, 300, 150);
    EXPECT_TRUE(results.empty());
    verifyIndex(index);
}

TEST_F(ViewportIndexTest, OnCellChanged) {
    addColumns(3, 100);
    addRows(3, 50);
    Cell* cell = addCell(1, 1, "B2");

    ViewportIndex index;
    index.build(*sheet_);

    // Changing cell value should not affect spatial index
    cell->value = CellValue("New Value");
    index.onCellChanged(cell);

    EXPECT_EQ(index.cellCount(), 1u);

    auto results = index.queryViewport(0, 0, 300, 150);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].cell, cell);
    verifyIndex(index);
}

TEST_F(ViewportIndexTest, OnCellAddedNull) {
    ViewportIndex index;
    index.onCellAdded(nullptr);  // Should not crash
    EXPECT_EQ(index.cellCount(), 0u);
}

// ============================================================================
// Incremental axis update tests
// ============================================================================

TEST_F(ViewportIndexTest, OnAxisInsertedColumn) {
    addColumns(2, 100);
    addRows(2, 50);

    ViewportIndex index;
    index.build(*sheet_);
    EXPECT_EQ(index.columnCount(), 2u);
    EXPECT_EQ(index.totalWidth(), 200u);

    // Insert a new column at position 1
    ID newColId = generate_id();
    index.onAxisInserted(newColId, true, 1, 150);

    EXPECT_EQ(index.columnCount(), 3u);
    EXPECT_EQ(index.totalWidth(), 350u);

    // Verify positions
    EXPECT_EQ(*index.getColumnAt(0), colIds_[0]);
    EXPECT_EQ(*index.getColumnAt(1), newColId);
    EXPECT_EQ(*index.getColumnAt(2), colIds_[1]);

    // Verify pixel offsets
    EXPECT_EQ(*index.columnToPixel(colIds_[0]), 0u);
    EXPECT_EQ(*index.columnToPixel(newColId), 100u);
    EXPECT_EQ(*index.columnToPixel(colIds_[1]), 250u);

    verifyIndex(index);
}

TEST_F(ViewportIndexTest, OnAxisInsertedRow) {
    addColumns(2, 100);
    addRows(2, 50);

    ViewportIndex index;
    index.build(*sheet_);
    EXPECT_EQ(index.rowCount(), 2u);
    EXPECT_EQ(index.totalHeight(), 100u);

    // Insert a new row at position 0
    ID newRowId = generate_id();
    index.onAxisInserted(newRowId, false, 0, 30);

    EXPECT_EQ(index.rowCount(), 3u);
    EXPECT_EQ(index.totalHeight(), 130u);

    // Verify positions
    EXPECT_EQ(*index.getRowAt(0), newRowId);
    EXPECT_EQ(*index.getRowAt(1), rowIds_[0]);
    EXPECT_EQ(*index.getRowAt(2), rowIds_[1]);

    verifyIndex(index);
}

TEST_F(ViewportIndexTest, OnAxisDeletedColumn) {
    addColumns(3, 100);
    addRows(2, 50);
    addCell(0, 0);
    addCell(1, 0);  // Cell in column to be deleted
    addCell(2, 0);

    ViewportIndex index;
    index.build(*sheet_);
    EXPECT_EQ(index.columnCount(), 3u);
    EXPECT_EQ(index.cellCount(), 3u);

    // Delete column 1
    index.onAxisDeleted(colIds_[1], true);

    EXPECT_EQ(index.columnCount(), 2u);
    EXPECT_EQ(index.totalWidth(), 200u);
    EXPECT_EQ(index.cellCount(), 2u);  // Cell in deleted column removed

    // Verify remaining columns
    EXPECT_EQ(*index.getColumnAt(0), colIds_[0]);
    EXPECT_EQ(*index.getColumnAt(1), colIds_[2]);

    verifyIndex(index);
}

TEST_F(ViewportIndexTest, OnAxisDeletedRow) {
    addColumns(2, 100);
    addRows(3, 50);
    addCell(0, 0);
    addCell(0, 1);  // Cell in row to be deleted
    addCell(0, 2);

    ViewportIndex index;
    index.build(*sheet_);
    EXPECT_EQ(index.rowCount(), 3u);
    EXPECT_EQ(index.cellCount(), 3u);

    // Delete row 1
    index.onAxisDeleted(rowIds_[1], false);

    EXPECT_EQ(index.rowCount(), 2u);
    EXPECT_EQ(index.totalHeight(), 100u);
    EXPECT_EQ(index.cellCount(), 2u);

    verifyIndex(index);
}

TEST_F(ViewportIndexTest, OnAxisResizedColumn) {
    addColumns(3, 100);
    addRows(2, 50);
    Cell* cell = addCell(2, 0);  // Cell in last column

    ViewportIndex index;
    index.build(*sheet_);

    // Resize column 1 from 100 to 200
    index.onAxisResized(colIds_[1], true, 200);

    EXPECT_EQ(index.totalWidth(), 400u);
    EXPECT_EQ(*index.getColumnWidth(colIds_[1]), 200u);

    // Verify pixel offset of last column shifted
    EXPECT_EQ(*index.columnToPixel(colIds_[2]), 300u);

    // Cell in last column should have updated position
    auto results = index.queryViewport(300, 0, 400, 50);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].cell, cell);
    EXPECT_EQ(results[0].x, 300u);
    EXPECT_EQ(results[0].width, 100u);

    verifyIndex(index);
}

TEST_F(ViewportIndexTest, OnAxisResizedRow) {
    addColumns(2, 100);
    addRows(3, 50);

    ViewportIndex index;
    index.build(*sheet_);

    // Resize row 0 from 50 to 100
    index.onAxisResized(rowIds_[0], false, 100);

    EXPECT_EQ(index.totalHeight(), 200u);
    EXPECT_EQ(*index.getRowHeight(rowIds_[0]), 100u);
    EXPECT_EQ(*index.rowToPixel(rowIds_[1]), 100u);
    EXPECT_EQ(*index.rowToPixel(rowIds_[2]), 150u);

    verifyIndex(index);
}

TEST_F(ViewportIndexTest, OnAxisMovedColumn) {
    addColumns(4, 100);
    addRows(2, 50);

    ViewportIndex index;
    index.build(*sheet_);

    // Move column 0 to position 2
    index.onAxisMoved(colIds_[0], true, 2);

    // New order: col1, col2, col0, col3
    EXPECT_EQ(*index.getColumnAt(0), colIds_[1]);
    EXPECT_EQ(*index.getColumnAt(1), colIds_[2]);
    EXPECT_EQ(*index.getColumnAt(2), colIds_[0]);
    EXPECT_EQ(*index.getColumnAt(3), colIds_[3]);

    verifyIndex(index);
}

TEST_F(ViewportIndexTest, OnAxisMovedRow) {
    addColumns(2, 100);
    addRows(4, 50);

    ViewportIndex index;
    index.build(*sheet_);

    // Move row 3 to position 1
    index.onAxisMoved(rowIds_[3], false, 1);

    // New order: row0, row3, row1, row2
    EXPECT_EQ(*index.getRowAt(0), rowIds_[0]);
    EXPECT_EQ(*index.getRowAt(1), rowIds_[3]);
    EXPECT_EQ(*index.getRowAt(2), rowIds_[1]);
    EXPECT_EQ(*index.getRowAt(3), rowIds_[2]);

    verifyIndex(index);
}

// ============================================================================
// Clear tests
// ============================================================================

TEST_F(ViewportIndexTest, Clear) {
    addColumns(5, 100);
    addRows(10, 24);
    addCell(0, 0);
    addCell(1, 1);

    ViewportIndex index;
    index.build(*sheet_);

    EXPECT_EQ(index.columnCount(), 5u);
    EXPECT_EQ(index.rowCount(), 10u);
    EXPECT_EQ(index.cellCount(), 2u);

    index.clear();

    EXPECT_TRUE(index.empty());
    EXPECT_EQ(index.columnCount(), 0u);
    EXPECT_EQ(index.rowCount(), 0u);
    EXPECT_EQ(index.cellCount(), 0u);
    EXPECT_EQ(index.totalWidth(), 0u);
    EXPECT_EQ(index.totalHeight(), 0u);

    verifyIndex(index);
}

// ============================================================================
// Variable size tests
// ============================================================================

TEST_F(ViewportIndexTest, VariableColumnWidths) {
    // Create columns with different widths
    auto col1 = std::make_unique<Axis>(generate_id(), true);
    col1->position = 0;
    col1->size = 50;
    colIds_.push_back(col1->id);
    sheet_->addColumn(std::move(col1));

    auto col2 = std::make_unique<Axis>(generate_id(), true);
    col2->position = 1;
    col2->size = 150;
    colIds_.push_back(col2->id);
    sheet_->addColumn(std::move(col2));

    auto col3 = std::make_unique<Axis>(generate_id(), true);
    col3->position = 2;
    col3->size = 100;
    colIds_.push_back(col3->id);
    sheet_->addColumn(std::move(col3));

    addRows(1, 24);
    addCell(1, 0);  // Cell in column with width 150

    ViewportIndex index;
    index.build(*sheet_);

    EXPECT_EQ(index.totalWidth(), 300u);

    // Verify pixel offsets
    EXPECT_EQ(*index.columnToPixel(colIds_[0]), 0u);
    EXPECT_EQ(*index.columnToPixel(colIds_[1]), 50u);
    EXPECT_EQ(*index.columnToPixel(colIds_[2]), 200u);

    // Query cell in second column
    auto results = index.queryViewport(50, 0, 200, 24);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].x, 50u);
    EXPECT_EQ(results[0].width, 150u);
}

TEST_F(ViewportIndexTest, VariableRowHeights) {
    addColumns(1, 100);

    // Create rows with different heights
    auto row1 = std::make_unique<Axis>(generate_id(), false);
    row1->position = 0;
    row1->size = 30;
    rowIds_.push_back(row1->id);
    sheet_->addRow(std::move(row1));

    auto row2 = std::make_unique<Axis>(generate_id(), false);
    row2->position = 1;
    row2->size = 50;
    rowIds_.push_back(row2->id);
    sheet_->addRow(std::move(row2));

    auto row3 = std::make_unique<Axis>(generate_id(), false);
    row3->position = 2;
    row3->size = 20;
    rowIds_.push_back(row3->id);
    sheet_->addRow(std::move(row3));

    addCell(0, 1);  // Cell in row with height 50

    ViewportIndex index;
    index.build(*sheet_);

    EXPECT_EQ(index.totalHeight(), 100u);

    // Verify pixel offsets
    EXPECT_EQ(*index.rowToPixel(rowIds_[0]), 0u);
    EXPECT_EQ(*index.rowToPixel(rowIds_[1]), 30u);
    EXPECT_EQ(*index.rowToPixel(rowIds_[2]), 80u);

    // Query cell in second row
    auto results = index.queryViewport(0, 30, 100, 80);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].y, 30u);
    EXPECT_EQ(results[0].height, 50u);
}

// ============================================================================
// Stress tests
// ============================================================================

TEST_F(ViewportIndexTest, ManyColumnsAndRows) {
    addColumns(100, 100);
    addRows(1000, 24);

    // Add cells diagonally
    for (size_t i = 0; i < 100; i++) {
        addCell(i, i * 10);
    }

    ViewportIndex index;
    index.build(*sheet_);

    EXPECT_EQ(index.columnCount(), 100u);
    EXPECT_EQ(index.rowCount(), 1000u);
    EXPECT_EQ(index.cellCount(), 100u);
    EXPECT_EQ(index.totalWidth(), 10000u);
    EXPECT_EQ(index.totalHeight(), 24000u);

    // Query a viewport
    auto results = index.queryViewport(0, 0, 500, 240);
    EXPECT_GE(results.size(), 1u);  // At least the first cell

    verifyIndex(index);
}

TEST_F(ViewportIndexTest, ManyIncrementalUpdates) {
    addColumns(10, 100);
    addRows(10, 24);

    ViewportIndex index;
    index.build(*sheet_);

    // Add and remove many cells
    for (int i = 0; i < 100; i++) {
        Cell* cell = addCell(static_cast<size_t>(i % 10), static_cast<size_t>(i % 10));
        index.onCellAdded(cell);

        if (i % 3 == 0) {
            index.onCellRemoved(cell);
        }
    }

    verifyIndex(index);
}

TEST_F(ViewportIndexTest, ManyResizeOperations) {
    addColumns(10, 100);
    addRows(10, 24);

    ViewportIndex index;
    index.build(*sheet_);

    // Resize columns multiple times
    for (int i = 0; i < 50; i++) {
        size_t colIdx = static_cast<size_t>(i % 10);
        uint32_t newWidth = static_cast<uint32_t>(50 + (i % 200));
        index.onAxisResized(colIds_[colIdx], true, newWidth);
    }

    verifyIndex(index);
}

// ============================================================================
// Performance benchmark tests
// ============================================================================

TEST_F(ViewportIndexTest, BenchmarkFullRebuildVsIncremental) {
    // Create a reasonably large sheet
    const size_t numCols = 100;
    const size_t numRows = 1000;

    addColumns(numCols, 100);
    addRows(numRows, 24);

    // Add some initial cells
    for (size_t i = 0; i < 500; i++) {
        addCell(i % numCols, i % numRows);
    }

    ViewportIndex index;
    index.build(*sheet_);

    // Benchmark full rebuild
    auto startRebuild = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 100; i++) {
        index.build(*sheet_);
    }
    auto endRebuild = std::chrono::high_resolution_clock::now();
    auto rebuildDuration =
        std::chrono::duration_cast<std::chrono::microseconds>(endRebuild - startRebuild).count();

    // Benchmark incremental cell add/remove
    auto startIncremental = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 100; i++) {
        Cell* cell =
            addCell(static_cast<size_t>(i % numCols), static_cast<size_t>((i + 500) % numRows));
        index.onCellAdded(cell);
        index.onCellRemoved(cell);
    }
    auto endIncremental = std::chrono::high_resolution_clock::now();
    auto incrementalDuration =
        std::chrono::duration_cast<std::chrono::microseconds>(endIncremental - startIncremental)
            .count();

    // Benchmark incremental axis resize
    auto startResize = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < 100; i++) {
        index.onAxisResized(colIds_[i % numCols], true, 100 + (i % 50));
    }
    auto endResize = std::chrono::high_resolution_clock::now();
    auto resizeDuration =
        std::chrono::duration_cast<std::chrono::microseconds>(endResize - startResize).count();

    // Log results (viewable in test output)
    std::cout << "\n=== ViewportIndex Performance Benchmark ===" << std::endl;
    std::cout << "Sheet size: " << numCols << " columns x " << numRows << " rows" << std::endl;
    std::cout << "100 full rebuilds: " << rebuildDuration << " µs (" << rebuildDuration / 100
              << " µs/rebuild)" << std::endl;
    std::cout << "100 incremental cell add/remove: " << incrementalDuration << " µs ("
              << incrementalDuration / 100 << " µs/op)" << std::endl;
    std::cout << "100 incremental axis resizes: " << resizeDuration << " µs ("
              << resizeDuration / 100 << " µs/resize)" << std::endl;

    // Incremental should be significantly faster than full rebuild
    // Note: This is a soft assertion - actual speedup depends on implementation
    EXPECT_LT(incrementalDuration, rebuildDuration)
        << "Incremental updates should be faster than full rebuilds";
    EXPECT_LT(resizeDuration, rebuildDuration)
        << "Incremental resizes should be faster than full rebuilds";

    verifyIndex(index);
}

TEST_F(ViewportIndexTest, BenchmarkLargeSheetIncrementalUpdates) {
    // Create a large sheet (simulating 1000x10000)
    const size_t numCols = 1000;
    const size_t numRows = 10000;

    addColumns(numCols, 80);
    addRows(numRows, 20);

    ViewportIndex index;

    // Time initial build
    auto startBuild = std::chrono::high_resolution_clock::now();
    index.build(*sheet_);
    auto endBuild = std::chrono::high_resolution_clock::now();
    auto buildDuration =
        std::chrono::duration_cast<std::chrono::microseconds>(endBuild - startBuild).count();

    std::cout << "\n=== Large Sheet Performance ===" << std::endl;
    std::cout << "Sheet size: " << numCols << " columns x " << numRows << " rows" << std::endl;
    std::cout << "Initial build: " << buildDuration << " µs" << std::endl;

    // Time incremental operations
    auto startOps = std::chrono::high_resolution_clock::now();

    // Add 1000 cells incrementally
    for (size_t i = 0; i < 1000; i++) {
        Cell* cell = addCell(i % numCols, i % numRows);
        index.onCellAdded(cell);
    }

    // Resize 100 columns
    for (size_t i = 0; i < 100; i++) {
        index.onAxisResized(colIds_[i], true, 100);
    }

    // Resize 100 rows
    for (size_t i = 0; i < 100; i++) {
        index.onAxisResized(rowIds_[i], false, 25);
    }

    auto endOps = std::chrono::high_resolution_clock::now();
    auto opsDuration =
        std::chrono::duration_cast<std::chrono::microseconds>(endOps - startOps).count();

    std::cout << "1000 cell adds + 100 col resizes + 100 row resizes: " << opsDuration << " µs"
              << std::endl;

    // The batch of incremental operations should be faster than a full rebuild
    EXPECT_LT(opsDuration, buildDuration)
        << "Batch of incremental ops should be faster than full rebuild";

    EXPECT_EQ(index.cellCount(), 1000u);
    verifyIndex(index);
}

}  // namespace
}  // namespace cells
