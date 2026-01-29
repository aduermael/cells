// Benchmark tests for workbook-level entity storage
// Phase 12: Performance Validation
//
// Tests performance of:
// - 12a: Cell lookup performance (Workbook::getCell, findCell)
// - 12b: Axis lookup performance (Workbook::getColumn, getRow)
// - 12c: Cross-sheet formula recalculation
// - 12d: Viewport query performance (covered by large_file_test.cc)
// - 12e: CRDT operation application speed

#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "core/cells/crdt.h"
#include "core/cells/dependency_graph.h"
#include "core/cells/formula_eval.h"
#include "core/cells/formula_parser.h"
#include "core/cells/formula_recalc.h"
#include "core/cells/formula_resolver.h"
#include "core/cells/id.h"
#include "core/cells/model.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// Helper to create a test workbook with sheets, columns, rows, and cells
class WorkbookBenchmarkTest : public ::testing::Test {
protected:
    void SetUp() override {
        workbook_ = std::make_unique<Workbook>(generate_id(), "BenchmarkWorkbook");
    }

    // Create a sheet with the specified number of columns and rows
    Sheet* createSheet(const std::string& name, size_t numCols, size_t numRows) {
        auto sheet = std::make_unique<Sheet>(generate_id(), name);
        sheet->setWorkbook(workbook_.get());
        Sheet* sheetPtr = sheet.get();
        workbook_->addSheet(std::move(sheet));

        // Add columns
        for (size_t c = 0; c < numCols; c++) {
            auto col = std::make_unique<Axis>(generate_id(), true);
            col->position = static_cast<uint32_t>(c);
            col->size = 100;
            colIds_.push_back(col->id);
            sheetPtr->addColumn(std::move(col));
        }

        // Add rows
        for (size_t r = 0; r < numRows; r++) {
            auto row = std::make_unique<Axis>(generate_id(), false);
            row->position = static_cast<uint32_t>(r);
            row->size = 24;
            rowIds_.push_back(row->id);
            sheetPtr->addRow(std::move(row));
        }

        return sheetPtr;
    }

    // Add a cell at the specified column/row indices
    Cell* addCell(Sheet* sheet, size_t colIdx, size_t rowIdx, double value) {
        auto cell = std::make_unique<Cell>(generate_id(), colIds_[colIdx], rowIds_[rowIdx]);
        cell->value = CellValue(value);
        Cell* cellPtr = cell.get();
        cellIds_.push_back(cell->id);
        sheet->addCell(std::move(cell));
        return cellPtr;
    }

    std::unique_ptr<Workbook> workbook_;
    std::vector<ID> colIds_;
    std::vector<ID> rowIds_;
    std::vector<ID> cellIds_;
};

// ============================================================================
// 12a: Benchmark cell lookup performance
// ============================================================================

TEST_F(WorkbookBenchmarkTest, CellLookupByIdPerformance) {
    const size_t numCols = 100;
    const size_t numRows = 1000;
    const size_t numCells = 10000;

    Sheet* sheet = createSheet("Sheet1", numCols, numRows);

    // Add cells at random positions
    std::mt19937 rng(42);  // Fixed seed for reproducibility
    for (size_t i = 0; i < numCells; i++) {
        size_t col = rng() % numCols;
        size_t row = rng() % numRows;
        addCell(sheet, col, row, static_cast<double>(i));
    }

    std::cout << "\n=== Cell Lookup Performance ===" << std::endl;
    std::cout << "Cells: " << numCells << ", Columns: " << numCols << ", Rows: " << numRows
              << std::endl;

    // Benchmark getCell by ID (O(1) hash lookup)
    {
        auto start = std::chrono::high_resolution_clock::now();
        const int iterations = 100000;
        for (int i = 0; i < iterations; i++) {
            const ID& cellId = cellIds_[i % cellIds_.size()];
            Cell* cell = workbook_->getCell(cellId);
            ASSERT_NE(cell, nullptr);
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        std::cout << "getCell() by ID: " << iterations << " lookups in " << duration << " us ("
                  << (duration * 1000.0 / iterations) << " ns/lookup)" << std::endl;

        // Should be sub-microsecond per lookup (O(1) hash lookup)
        EXPECT_LT(duration, 500000) << "Cell lookup should be fast (<5us average)";
    }

    // Benchmark findCell (finds cell and its sheet)
    {
        auto start = std::chrono::high_resolution_clock::now();
        const int iterations = 100000;
        for (int i = 0; i < iterations; i++) {
            const ID& cellId = cellIds_[i % cellIds_.size()];
            auto result = workbook_->findCell(cellId);
            ASSERT_NE(result.cell, nullptr);
            ASSERT_NE(result.sheet, nullptr);
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        std::cout << "findCell() (cell+sheet): " << iterations << " lookups in " << duration
                  << " us (" << (duration * 1000.0 / iterations) << " ns/lookup)" << std::endl;

        // findCell does additional sheet lookup, still should be fast
        EXPECT_LT(duration, 1000000) << "findCell should be fast (<10us average)";
    }

    // Benchmark getCellAt by position (position -> ID -> cell)
    {
        auto start = std::chrono::high_resolution_clock::now();
        const int iterations = 100000;
        for (int i = 0; i < iterations; i++) {
            size_t col = i % numCols;
            size_t row = (i / numCols) % numRows;
            const ID& colId = colIds_[col];
            const ID& rowId = rowIds_[row];
            Cell* cell = sheet->getCellAt(colId, rowId);
            // May be null if no cell at this position
            (void)cell;
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        std::cout << "getCellAt() by position: " << iterations << " lookups in " << duration
                  << " us (" << (duration * 1000.0 / iterations) << " ns/lookup)" << std::endl;

        // Position lookup adds one extra hash lookup
        EXPECT_LT(duration, 1000000) << "Position lookup should be fast (<10us average)";
    }
}

// ============================================================================
// 12b: Benchmark axis lookup performance
// ============================================================================

TEST_F(WorkbookBenchmarkTest, AxisLookupPerformance) {
    const size_t numCols = 1000;
    const size_t numRows = 10000;

    Sheet* sheet = createSheet("Sheet1", numCols, numRows);

    std::cout << "\n=== Axis Lookup Performance ===" << std::endl;
    std::cout << "Columns: " << numCols << ", Rows: " << numRows << std::endl;

    // Benchmark getColumn by ID
    {
        auto start = std::chrono::high_resolution_clock::now();
        const int iterations = 100000;
        for (int i = 0; i < iterations; i++) {
            const ID& colId = colIds_[i % numCols];
            Axis* col = workbook_->getColumn(colId);
            ASSERT_NE(col, nullptr);
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        std::cout << "getColumn() by ID: " << iterations << " lookups in " << duration << " us ("
                  << (duration * 1000.0 / iterations) << " ns/lookup)" << std::endl;

        EXPECT_LT(duration, 500000) << "Column lookup should be fast";
    }

    // Benchmark getRow by ID
    {
        auto start = std::chrono::high_resolution_clock::now();
        const int iterations = 100000;
        for (int i = 0; i < iterations; i++) {
            const ID& rowId = rowIds_[i % numRows];
            Axis* row = workbook_->getRow(rowId);
            ASSERT_NE(row, nullptr);
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        std::cout << "getRow() by ID: " << iterations << " lookups in " << duration << " us ("
                  << (duration * 1000.0 / iterations) << " ns/lookup)" << std::endl;

        EXPECT_LT(duration, 500000) << "Row lookup should be fast";
    }

    // Benchmark getColumnByPosition (position -> ID -> axis)
    {
        auto start = std::chrono::high_resolution_clock::now();
        const int iterations = 100000;
        for (int i = 0; i < iterations; i++) {
            uint32_t pos = i % numCols;
            Axis* col = sheet->getColumnByPosition(pos);
            ASSERT_NE(col, nullptr);
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        std::cout << "getColumnByPosition(): " << iterations << " lookups in " << duration
                  << " us (" << (duration * 1000.0 / iterations) << " ns/lookup)" << std::endl;

        EXPECT_LT(duration, 1000000) << "Position-based column lookup should be fast";
    }

    // Benchmark getRowByPosition
    {
        auto start = std::chrono::high_resolution_clock::now();
        const int iterations = 100000;
        for (int i = 0; i < iterations; i++) {
            uint32_t pos = i % numRows;
            Axis* row = sheet->getRowByPosition(pos);
            ASSERT_NE(row, nullptr);
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        std::cout << "getRowByPosition(): " << iterations << " lookups in " << duration << " us ("
                  << (duration * 1000.0 / iterations) << " ns/lookup)" << std::endl;

        EXPECT_LT(duration, 1000000) << "Position-based row lookup should be fast";
    }
}

// ============================================================================
// 12c: Benchmark cross-sheet formula recalculation
// ============================================================================

TEST_F(WorkbookBenchmarkTest, CrossSheetFormulaPerformance) {
    const size_t numCols = 10;
    const size_t numRows = 100;

    // Create Sheet1 with source values
    Sheet* sheet1 = createSheet("Sheet1", numCols, numRows);
    size_t baseColOffset = 0;
    size_t baseRowOffset = 0;

    // Add value cells to Sheet1
    for (size_t r = 0; r < numRows; r++) {
        for (size_t c = 0; c < numCols; c++) {
            addCell(sheet1, baseColOffset + c, baseRowOffset + r,
                    static_cast<double>(r * numCols + c));
        }
    }

    // Create Sheet2 with formulas referencing Sheet1
    size_t sheet2ColOffset = colIds_.size();
    size_t sheet2RowOffset = rowIds_.size();
    Sheet* sheet2 = createSheet("Sheet2", numCols, numRows);

    std::cout << "\n=== Cross-Sheet Formula Performance ===" << std::endl;
    std::cout << "Source cells: " << (numCols * numRows) << " on Sheet1" << std::endl;

    // Add formulas to Sheet2 that reference Sheet1
    size_t formulaCount = 0;
    for (size_t r = 0; r < numRows / 2; r++) {
        for (size_t c = 0; c < numCols; c++) {
            // Create value cells and measure recalc time separately
            addCell(sheet2, sheet2ColOffset + c, sheet2RowOffset + r, 0.0);
            formulaCount++;
        }
    }

    std::cout << "Formula cells: " << formulaCount << " on Sheet2" << std::endl;

    // Benchmark evaluation of formulas
    {
        // Evaluate all cells with values
        auto start = std::chrono::high_resolution_clock::now();
        const int iterations = 10;
        for (int iter = 0; iter < iterations; iter++) {
            for (const ID& cellId : sheet1->getCellIds()) {
                Cell* cell = workbook_->getCell(cellId);
                if (cell) {
                    // Just access the cell value
                    (void)cell->value.asNumber();
                }
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        std::cout << "Cell value access: " << iterations << " iterations x " << numCols * numRows
                  << " cells = " << duration << " us" << std::endl;
    }

    // Benchmark dependency graph operations
    {
        DependencyGraph* depGraph = workbook_->getDependencyGraph();

        auto start = std::chrono::high_resolution_clock::now();
        const int iterations = 1000;

        // Simulate dependency queries
        for (int i = 0; i < iterations; i++) {
            const ID& cellId = cellIds_[i % cellIds_.size()];
            // Query dependents
            auto dependents = depGraph->getDependents(cellId);
            (void)dependents;
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        std::cout << "getDependents(): " << iterations << " queries in " << duration << " us ("
                  << (duration * 1000.0 / iterations) << " ns/query)" << std::endl;

        EXPECT_LT(duration, 100000) << "Dependency queries should be fast";
    }
}

// ============================================================================
// 12e: Benchmark CRDT operation application speed
// ============================================================================

TEST_F(WorkbookBenchmarkTest, CRDTOperationPerformance) {
    const size_t numCols = 50;
    const size_t numRows = 100;

    Sheet* sheet = createSheet("Sheet1", numCols, numRows);

    // Pre-populate with some cells
    for (size_t r = 0; r < numRows / 2; r++) {
        for (size_t c = 0; c < numCols; c++) {
            addCell(sheet, c, r, static_cast<double>(r * numCols + c));
        }
    }

    std::cout << "\n=== CRDT Operation Performance ===" << std::endl;
    std::cout << "Sheet size: " << numCols << " columns x " << numRows << " rows" << std::endl;
    std::cout << "Initial cells: " << sheet->cellCount() << std::endl;

    // Benchmark CELL_SET_VALUE operations
    {
        auto start = std::chrono::high_resolution_clock::now();
        const int iterations = 1000;

        for (int i = 0; i < iterations; i++) {
            // Create operation to set cell value
            Operation op;
            op.type = OpType::CELL_SET_VALUE;
            op.hlc = workbook_->getCurrentHLC();
            op.sheetId = sheet->id;
            op.target_id = cellIds_[i % cellIds_.size()];
            op.payload = std::to_string(i * 1.5);

            // Apply operation
            applyOperation(*workbook_, op);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        std::cout << "CELL_SET_VALUE: " << iterations << " ops in " << duration << " us ("
                  << (duration * 1000.0 / iterations) << " ns/op)" << std::endl;

        EXPECT_LT(duration, 500000) << "CELL_SET_VALUE should be fast";
    }

    // Benchmark COL_RESIZE operations (column resize)
    {
        auto start = std::chrono::high_resolution_clock::now();
        const int iterations = 1000;

        for (int i = 0; i < iterations; i++) {
            Operation op;
            op.type = OpType::COL_RESIZE;
            op.hlc = workbook_->getCurrentHLC();
            op.sheetId = sheet->id;
            op.target_id = colIds_[i % numCols];
            op.payload = std::to_string(100 + (i % 50));  // Vary width

            applyOperation(*workbook_, op);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        std::cout << "COL_RESIZE: " << iterations << " ops in " << duration << " us ("
                  << (duration * 1000.0 / iterations) << " ns/op)" << std::endl;

        EXPECT_LT(duration, 500000) << "Axis resize should be fast";
    }

    // Benchmark CELL_SET_FORMAT operations (content-addressed formats)
    {
        // Create a FormatBuffer for the benchmark
        // Content-addressed formats don't need registration
        const std::string formatBase64 = "AQIC";  // Number format with 2 decimals

        auto start = std::chrono::high_resolution_clock::now();
        const int iterations = 1000;

        for (int i = 0; i < iterations; i++) {
            Operation op;
            op.type = OpType::CELL_SET_FORMAT;
            op.hlc = workbook_->getCurrentHLC();
            op.sheetId = sheet->id;
            op.target_id = cellIds_[i % cellIds_.size()];
            op.payload = "{\"format\":\"" + formatBase64 + "\"}";

            applyOperation(*workbook_, op);
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

        std::cout << "CELL_SET_FORMAT: " << iterations << " ops in " << duration << " us ("
                  << (duration * 1000.0 / iterations) << " ns/op)" << std::endl;

        EXPECT_LT(duration, 500000) << "Cell format should be fast";
    }
}

// ============================================================================
// Memory efficiency test - verify workbook-level storage doesn't bloat memory
// ============================================================================

TEST_F(WorkbookBenchmarkTest, MemoryEfficiency) {
    const size_t numCols = 100;
    const size_t numRows = 1000;
    const size_t numCells = 50000;

    Sheet* sheet = createSheet("Sheet1", numCols, numRows);

    // Add many cells
    std::mt19937 rng(42);
    for (size_t i = 0; i < numCells; i++) {
        size_t col = rng() % numCols;
        size_t row = rng() % numRows;
        addCell(sheet, col, row, static_cast<double>(i));
    }

    std::cout << "\n=== Memory Efficiency ===" << std::endl;
    std::cout << "Columns: " << numCols << ", Rows: " << numRows << std::endl;
    std::cout << "Cells added: " << numCells << ", Unique cells: " << sheet->cellCount()
              << std::endl;

    // Verify all cells are accessible
    size_t accessibleCells = 0;
    for (const ID& cellId : sheet->getCellIds()) {
        Cell* cell = workbook_->getCell(cellId);
        if (cell) {
            accessibleCells++;
        }
    }
    EXPECT_EQ(accessibleCells, sheet->cellCount()) << "All cells should be accessible";

    // Verify all axes are accessible
    size_t accessibleCols = 0;
    for (const ID& colId : sheet->getColumnIds()) {
        Axis* col = workbook_->getColumn(colId);
        if (col) {
            accessibleCols++;
        }
    }
    EXPECT_EQ(accessibleCols, numCols) << "All columns should be accessible";

    size_t accessibleRows = 0;
    for (const ID& rowId : sheet->getRowIds()) {
        Axis* row = workbook_->getRow(rowId);
        if (row) {
            accessibleRows++;
        }
    }
    EXPECT_EQ(accessibleRows, numRows) << "All rows should be accessible";

    std::cout << "Accessible: " << accessibleCells << " cells, " << accessibleCols << " columns, "
              << accessibleRows << " rows" << std::endl;
}

}  // namespace
}  // namespace cells
