#include "core/cells/quadtree.h"

#include <memory>
#include <vector>

#include "core/cells/id.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// Test fixture with helper methods
class QuadtreeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a simple sheet for testing
        sheet_ = std::make_unique<Sheet>(generate_id(), "TestSheet");

        // Add columns (0-9)
        for (uint32_t i = 0; i < 10; i++) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = i;
            col->size = DEFAULT_COLUMN_WIDTH;
            colIds_.push_back(col->id);
            sheet_->addColumn(std::move(col));
        }

        // Add rows (0-99)
        for (uint32_t i = 0; i < 100; i++) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = i;
            row->size = DEFAULT_ROW_HEIGHT;
            rowIds_.push_back(row->id);
            sheet_->addRow(std::move(row));
        }
    }

    Cell* addCell(uint32_t col, uint32_t row, const std::string& value = "") {
        auto cell = std::make_unique<Cell>(generate_id(), colIds_[col], rowIds_[row]);
        cell->value = CellValue(value);
        Cell* ptr = cell.get();
        sheet_->addCell(std::move(cell));
        return ptr;
    }

    std::unique_ptr<Sheet> sheet_;
    std::vector<ID> colIds_;
    std::vector<ID> rowIds_;
};

// Rect tests

TEST(RectTest, Contains) {
    Rect r(10, 20, 50, 60);

    // Inside
    EXPECT_TRUE(r.contains(10, 20));  // top-left corner
    EXPECT_TRUE(r.contains(30, 40));  // center
    EXPECT_TRUE(r.contains(49, 59));  // just inside

    // Outside
    EXPECT_FALSE(r.contains(9, 20));   // left of rect
    EXPECT_FALSE(r.contains(50, 20));  // right edge (exclusive)
    EXPECT_FALSE(r.contains(10, 60));  // bottom edge (exclusive)
    EXPECT_FALSE(r.contains(5, 5));    // totally outside
}

TEST(RectTest, Intersects) {
    Rect r(10, 20, 50, 60);

    // Intersecting rects
    EXPECT_TRUE(r.intersects(Rect(0, 0, 20, 30)));      // overlaps top-left
    EXPECT_TRUE(r.intersects(Rect(40, 50, 100, 100)));  // overlaps bottom-right
    EXPECT_TRUE(r.intersects(Rect(20, 30, 30, 40)));    // inside
    EXPECT_TRUE(r.intersects(Rect(0, 0, 100, 100)));    // contains

    // Non-intersecting rects
    EXPECT_FALSE(r.intersects(Rect(0, 0, 10, 20)));        // touches corner (exclusive)
    EXPECT_FALSE(r.intersects(Rect(50, 60, 100, 100)));    // adjacent
    EXPECT_FALSE(r.intersects(Rect(100, 100, 200, 200)));  // far away
}

TEST(RectTest, WidthHeight) {
    Rect r(10, 20, 50, 80);
    EXPECT_EQ(r.width(), 40u);
    EXPECT_EQ(r.height(), 60u);
}

// Quadtree insert/count tests

TEST_F(QuadtreeTest, EmptyQuadtree) {
    Quadtree qt;
    EXPECT_EQ(qt.count(), 0u);
    EXPECT_TRUE(qt.all().empty());
}

TEST_F(QuadtreeTest, InsertSingleCell) {
    Quadtree qt;
    Cell* cell = addCell(0, 0, "Hello");

    EXPECT_TRUE(qt.insert(cell, *sheet_));
    EXPECT_EQ(qt.count(), 1u);
}

TEST_F(QuadtreeTest, InsertMultipleCells) {
    Quadtree qt;

    Cell* cell1 = addCell(0, 0, "A1");
    Cell* cell2 = addCell(1, 0, "B1");
    Cell* cell3 = addCell(0, 1, "A2");

    qt.insert(cell1, *sheet_);
    qt.insert(cell2, *sheet_);
    qt.insert(cell3, *sheet_);

    EXPECT_EQ(qt.count(), 3u);
}

TEST_F(QuadtreeTest, InsertWithExplicitPosition) {
    Quadtree qt;
    Cell* cell = addCell(0, 0, "Test");

    EXPECT_TRUE(qt.insert(cell, 5, 10));
    EXPECT_EQ(qt.count(), 1u);
}

TEST_F(QuadtreeTest, InsertOutOfBounds) {
    Quadtree qt(Rect(0, 0, 100, 100));
    Cell* cell = addCell(0, 0, "Test");

    EXPECT_FALSE(qt.insert(cell, 100, 100));  // Out of bounds (exclusive)
    EXPECT_FALSE(qt.insert(cell, 200, 50));   // Out of bounds
    EXPECT_EQ(qt.count(), 0u);
}

// Quadtree remove tests

TEST_F(QuadtreeTest, RemoveCell) {
    Quadtree qt;
    Cell* cell = addCell(0, 0, "Hello");

    qt.insert(cell, 5, 10);
    EXPECT_EQ(qt.count(), 1u);

    EXPECT_TRUE(qt.remove(cell, 5, 10));
    EXPECT_EQ(qt.count(), 0u);
}

TEST_F(QuadtreeTest, RemoveNonexistent) {
    Quadtree qt;
    Cell* cell = addCell(0, 0, "Hello");

    EXPECT_FALSE(qt.remove(cell, 5, 10));  // Never inserted
}

TEST_F(QuadtreeTest, RemoveWithSheet) {
    Quadtree qt;
    Cell* cell = addCell(3, 7, "Test");

    qt.insert(cell, *sheet_);
    EXPECT_EQ(qt.count(), 1u);

    EXPECT_TRUE(qt.remove(cell, *sheet_));
    EXPECT_EQ(qt.count(), 0u);
}

// Quadtree query tests

TEST_F(QuadtreeTest, QueryEmptyQuadtree) {
    Quadtree qt;
    auto results = qt.query(0, 0, 100, 100);
    EXPECT_TRUE(results.empty());
}

TEST_F(QuadtreeTest, QuerySingleCell) {
    Quadtree qt;
    Cell* cell = addCell(0, 0, "Hello");
    qt.insert(cell, 5, 10);

    // Query containing the cell
    auto results = qt.query(0, 0, 20, 20);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].cell, cell);
    EXPECT_EQ(results[0].x, 5u);
    EXPECT_EQ(results[0].y, 10u);

    // Query not containing the cell
    results = qt.query(50, 50, 100, 100);
    EXPECT_TRUE(results.empty());
}

TEST_F(QuadtreeTest, QueryViewport) {
    Quadtree qt;

    // Add cells at various positions
    std::vector<Cell*> cells;
    for (uint32_t col = 0; col < 10; col++) {
        for (uint32_t row = 0; row < 10; row++) {
            Cell* cell = addCell(col, row, "");
            cells.push_back(cell);
            qt.insert(cell, col, row);
        }
    }

    EXPECT_EQ(qt.count(), 100u);

    // Query a viewport (2x2 region)
    auto results = qt.query(2, 3, 4, 5);
    EXPECT_EQ(results.size(), 4u);  // Cells at (2,3), (2,4), (3,3), (3,4)

    // Verify positions
    for (const auto& entry : results) {
        EXPECT_GE(entry.x, 2u);
        EXPECT_LT(entry.x, 4u);
        EXPECT_GE(entry.y, 3u);
        EXPECT_LT(entry.y, 5u);
    }
}

TEST_F(QuadtreeTest, QueryLargeViewport) {
    Quadtree qt;

    // Add cells scattered across the grid
    Cell* c1 = addCell(0, 0, "");
    Cell* c2 = addCell(5, 5, "");
    Cell* c3 = addCell(9, 9, "");

    qt.insert(c1, 0, 0);
    qt.insert(c2, 50, 50);
    qt.insert(c3, 99, 99);

    // Query covering all
    auto results = qt.query(0, 0, 100, 100);
    EXPECT_EQ(results.size(), 3u);

    // Query covering only first two
    results = qt.query(0, 0, 60, 60);
    EXPECT_EQ(results.size(), 2u);

    // Query covering only middle one
    results = qt.query(40, 40, 60, 60);
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].x, 50u);
    EXPECT_EQ(results[0].y, 50u);
}

// Build from sheet tests

TEST_F(QuadtreeTest, BuildFromSheet) {
    // Add cells to the sheet
    addCell(0, 0, "A1");
    addCell(1, 0, "B1");
    addCell(0, 1, "A2");
    addCell(5, 10, "F11");

    Quadtree qt;
    qt.build(*sheet_);

    EXPECT_EQ(qt.count(), 4u);

    // Query for cells in top-left area
    auto results = qt.query(0, 0, 2, 2);
    EXPECT_EQ(results.size(), 3u);  // A1, B1, A2
}

// Stress test: many cells trigger subdivision

TEST_F(QuadtreeTest, SubdivisionOccurs) {
    Quadtree qt(Rect(0, 0, 100, 100));

    // Insert more than MAX_ENTRIES cells at different positions
    std::vector<std::unique_ptr<Cell>> cells;
    for (int i = 0; i < 50; i++) {
        auto cell = std::make_unique<Cell>(generate_id());
        qt.insert(cell.get(), static_cast<uint32_t>(i * 2), static_cast<uint32_t>(i));
        cells.push_back(std::move(cell));
    }

    EXPECT_EQ(qt.count(), 50u);

    // Query should still work after subdivision
    auto results = qt.query(0, 0, 100, 100);
    EXPECT_EQ(results.size(), 50u);

    // Query partial area
    results = qt.query(0, 0, 10, 10);
    EXPECT_EQ(results.size(), 5u);  // Cells at positions (0,0), (2,1), (4,2), (6,3), (8,4)
}

// All entries test

TEST_F(QuadtreeTest, AllEntries) {
    Quadtree qt;

    Cell* c1 = addCell(0, 0, "");
    Cell* c2 = addCell(1, 1, "");
    Cell* c3 = addCell(2, 2, "");

    qt.insert(c1, 0, 0);
    qt.insert(c2, 10, 10);
    qt.insert(c3, 20, 20);

    auto results = qt.all();
    EXPECT_EQ(results.size(), 3u);
}

// Clear test

TEST_F(QuadtreeTest, Clear) {
    Quadtree qt;

    Cell* c1 = addCell(0, 0, "");
    Cell* c2 = addCell(1, 1, "");

    qt.insert(c1, 0, 0);
    qt.insert(c2, 10, 10);
    EXPECT_EQ(qt.count(), 2u);

    qt.clear();
    EXPECT_EQ(qt.count(), 0u);
    EXPECT_TRUE(qt.all().empty());
}

// Bounds test

TEST_F(QuadtreeTest, DefaultBounds) {
    Quadtree qt;
    const Rect& bounds = qt.bounds();

    EXPECT_EQ(bounds.x1, 0u);
    EXPECT_EQ(bounds.y1, 0u);
    EXPECT_EQ(bounds.x2, 16384u);    // Excel column count
    EXPECT_EQ(bounds.y2, 1048576u);  // Excel row count
}

TEST_F(QuadtreeTest, CustomBounds) {
    Quadtree qt(Rect(10, 20, 100, 200));
    const Rect& bounds = qt.bounds();

    EXPECT_EQ(bounds.x1, 10u);
    EXPECT_EQ(bounds.y1, 20u);
    EXPECT_EQ(bounds.x2, 100u);
    EXPECT_EQ(bounds.y2, 200u);
}

// Edge case: cells at exact boundary positions

TEST_F(QuadtreeTest, CellsAtBoundary) {
    Quadtree qt(Rect(0, 0, 100, 100));

    auto cell = std::make_unique<Cell>(generate_id());

    // Position at (0,0) should work
    EXPECT_TRUE(qt.insert(cell.get(), 0, 0));

    // Position at (99,99) should work (inside bounds)
    auto cell2 = std::make_unique<Cell>(generate_id());
    EXPECT_TRUE(qt.insert(cell2.get(), 99, 99));

    // Position at (100,100) should fail (outside bounds)
    auto cell3 = std::make_unique<Cell>(generate_id());
    EXPECT_FALSE(qt.insert(cell3.get(), 100, 100));
}

}  // namespace
}  // namespace cells
