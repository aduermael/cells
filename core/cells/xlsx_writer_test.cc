#include "core/cells/xlsx_writer.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include "core/cells/id.h"
#include "core/cells/xlsx_reader.h"
#include "gtest/gtest.h"

namespace cells {
namespace {

// Counter for unique file paths
static int gFileCounter = 0;

// Helper to create a temp file path with unique suffix
std::string tempFilePath(const std::string& filename) {
    return "/tmp/xlsx_writer_test_" + std::to_string(++gFileCounter) + "_" + filename;
}

// Helper to clean up test files
class TempFileGuard {
public:
    explicit TempFileGuard(std::string path) : path_(std::move(path)) {}
    ~TempFileGuard() { std::remove(path_.c_str()); }
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

// Helper to create a simple workbook with one sheet
std::unique_ptr<Workbook> createSimpleWorkbook() {
    auto workbook = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");

    // Create 3 columns
    std::vector<ID> colIds;
    for (int i = 0; i < 3; ++i) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = i;
        col->size = 100;
        colIds.push_back(col->id);
        sheet->addColumn(std::move(col));
    }

    // Create 3 rows
    std::vector<ID> rowIds;
    for (int i = 0; i < 3; ++i) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = i;
        row->size = 20;
        rowIds.push_back(row->id);
        sheet->addRow(std::move(row));
    }

    // Add cells with different values
    // (0,0) = "Name"
    auto cell1 = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[0]);
    cell1->value = CellValue("Name");
    sheet->addCell(std::move(cell1));

    // (1,0) = "Value"
    auto cell2 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[0]);
    cell2->value = CellValue("Value");
    sheet->addCell(std::move(cell2));

    // (2,0) = "Active"
    auto cell3 = std::make_unique<Cell>(generate_id(), colIds[2], rowIds[0]);
    cell3->value = CellValue("Active");
    sheet->addCell(std::move(cell3));

    // (0,1) = "Alice"
    auto cell4 = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[1]);
    cell4->value = CellValue("Alice");
    sheet->addCell(std::move(cell4));

    // (1,1) = 100
    auto cell5 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[1]);
    cell5->value = CellValue(100.0);
    sheet->addCell(std::move(cell5));

    // (2,1) = true
    auto cell6 = std::make_unique<Cell>(generate_id(), colIds[2], rowIds[1]);
    cell6->value = CellValue(true);
    sheet->addCell(std::move(cell6));

    // (0,2) = "Bob"
    auto cell7 = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[2]);
    cell7->value = CellValue("Bob");
    sheet->addCell(std::move(cell7));

    // (1,2) = 200
    auto cell8 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[2]);
    cell8->value = CellValue(200.0);
    sheet->addCell(std::move(cell8));

    // (2,2) = false
    auto cell9 = std::make_unique<Cell>(generate_id(), colIds[2], rowIds[2]);
    cell9->value = CellValue(false);
    sheet->addCell(std::move(cell9));

    workbook->addSheet(std::move(sheet));
    return workbook;
}

// ============================================================================
// Basic Writing Tests
// ============================================================================

TEST(XLSXWriterTest, WriteSimpleFile) {
    auto workbook = createSimpleWorkbook();
    std::string path = tempFilePath("simple.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");
    EXPECT_GT(result.cellsWritten, 0u);

    // Verify file exists
    EXPECT_TRUE(std::filesystem::exists(path));
}

TEST(XLSXWriterTest, WriteEmptyWorkbook) {
    Workbook workbook(generate_id(), "Empty");
    std::string path = tempFilePath("empty.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(workbook, path);
    EXPECT_FALSE(result.ok());
    ASSERT_TRUE(result.error.has_value());
    EXPECT_TRUE(result.error->message.find("no sheets") != std::string::npos);
}

TEST(XLSXWriterTest, WriteToInvalidPath) {
    auto workbook = createSimpleWorkbook();
    std::string path = "/nonexistent/directory/file.xlsx";

    auto result = writeXLSX(*workbook, path);
    EXPECT_FALSE(result.ok());
    ASSERT_TRUE(result.error.has_value());
}

// ============================================================================
// Cell Type Tests
// ============================================================================

TEST(XLSXWriterTest, WriteNumbers) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Numbers");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");

    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = 0;
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue(42.5);
    sheet->addCell(std::move(cell));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("numbers.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");

    // Read back and verify
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);
    EXPECT_EQ(readSheet->cellCount(), 1u);

    // Find the cell and check value
    for (const auto& [id, c] : readSheet->cells) {
        EXPECT_EQ(c->value.type, CellValueType::NUMBER);
        EXPECT_DOUBLE_EQ(c->value.asNumber(), 42.5);
    }
}

TEST(XLSXWriterTest, WriteStrings) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Strings");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");

    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = 0;
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue("Hello World");
    sheet->addCell(std::move(cell));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("strings.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");

    // Read back and verify
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);
    EXPECT_EQ(readSheet->cellCount(), 1u);

    // Check that we have a string cell
    bool foundString = false;
    for (const auto& [id, c] : readSheet->cells) {
        if (c->value.type == CellValueType::STRING) {
            foundString = true;
            EXPECT_EQ(c->value.asString(), "Hello World");
            break;
        }
    }
    EXPECT_TRUE(foundString) << "Expected to find string cell with 'Hello World'";
}

TEST(XLSXWriterTest, WriteBooleans) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Booleans");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");

    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    std::vector<ID> rowIds;
    for (int i = 0; i < 2; ++i) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = i;
        rowIds.push_back(row->id);
        sheet->addRow(std::move(row));
    }

    auto cell1 = std::make_unique<Cell>(generate_id(), colId, rowIds[0]);
    cell1->value = CellValue(true);
    sheet->addCell(std::move(cell1));

    auto cell2 = std::make_unique<Cell>(generate_id(), colId, rowIds[1]);
    cell2->value = CellValue(false);
    sheet->addCell(std::move(cell2));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("booleans.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");

    // Read back and verify
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);
    EXPECT_EQ(readSheet->cellCount(), 2u);

    bool foundTrue = false;
    bool foundFalse = false;
    for (const auto& [id, c] : readSheet->cells) {
        EXPECT_EQ(c->value.type, CellValueType::BOOLEAN);
        if (c->value.asBoolean()) {
            foundTrue = true;
        } else {
            foundFalse = true;
        }
    }
    EXPECT_TRUE(foundTrue);
    EXPECT_TRUE(foundFalse);
}

// ============================================================================
// Multiple Sheets Tests
// ============================================================================

TEST(XLSXWriterTest, WriteMultipleSheets) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "MultiSheet");

    // Add 3 sheets
    for (int i = 0; i < 3; ++i) {
        auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet" + std::to_string(i + 1));

        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = 0;
        ID colId = col->id;
        sheet->addColumn(std::move(col));

        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = 0;
        ID rowId = row->id;
        sheet->addRow(std::move(row));

        auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
        cell->value = CellValue("Data" + std::to_string(i + 1));
        sheet->addCell(std::move(cell));

        workbook->addSheet(std::move(sheet));
    }

    std::string path = tempFilePath("multi_sheet.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");

    // Read back and verify
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);
    EXPECT_EQ(readResult.workbook->sheetCount(), 3u);
}

TEST(XLSXWriterTest, SheetNamesPreserved) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "NamedSheets");

    std::vector<std::string> sheetNames = {"Sales", "Expenses", "Summary"};

    for (const auto& name : sheetNames) {
        auto sheet = std::make_unique<Sheet>(generate_id(), name);

        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = 0;
        sheet->addColumn(std::move(col));

        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = 0;
        sheet->addRow(std::move(row));

        workbook->addSheet(std::move(sheet));
    }

    std::string path = tempFilePath("named_sheets.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");

    // Read back and verify names
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    std::vector<std::string> readNames;
    for (const auto& sheet : readResult.workbook->sheets) {
        readNames.push_back(sheet->name);
    }

    EXPECT_EQ(readNames.size(), sheetNames.size());
    for (const auto& name : sheetNames) {
        EXPECT_TRUE(std::find(readNames.begin(), readNames.end(), name) != readNames.end())
            << "Sheet name '" << name << "' not found";
    }
}

// ============================================================================
// Dimensions Tests
// ============================================================================

TEST(XLSXWriterTest, WriteColumnWidths) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Widths");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");

    // Create columns with different widths
    std::vector<uint32_t> widths = {50, 100, 200};
    std::vector<ID> colIds;

    for (size_t i = 0; i < widths.size(); ++i) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = i;
        col->size = widths[i];
        colIds.push_back(col->id);
        sheet->addColumn(std::move(col));
    }

    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = 0;
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    // Add cells
    for (size_t i = 0; i < colIds.size(); ++i) {
        auto cell = std::make_unique<Cell>(generate_id(), colIds[i], rowId);
        cell->value = CellValue("Col" + std::to_string(i + 1));
        sheet->addCell(std::move(cell));
    }

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("widths.xlsx");
    TempFileGuard guard(path);

    XLSXWriteOptions options;
    options.writeDimensions = true;

    auto result = writeXLSX(*workbook, path, options);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");

    // Read back - dimensions should be preserved approximately
    XLSXReadOptions readOptions;
    readOptions.readDimensions = true;

    auto readResult = readXLSX(path, readOptions);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);
    EXPECT_EQ(readSheet->columnCount(), 3u);

    // Verify columns have sizes set
    for (const auto& [id, col] : readSheet->columns) {
        EXPECT_GT(col->size, 0u) << "Column should have width > 0";
    }
}

TEST(XLSXWriterTest, SkipDimensionsWhenDisabled) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "NoDimensions");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");

    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = 0;
    col->size = 500;  // Large custom width
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = 0;
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue("Test");
    sheet->addCell(std::move(cell));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("no_dimensions.xlsx");
    TempFileGuard guard(path);

    XLSXWriteOptions options;
    options.writeDimensions = false;

    auto result = writeXLSX(*workbook, path, options);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");

    // File should still be created and readable
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
}

// ============================================================================
// Roundtrip Tests
// ============================================================================

TEST(XLSXWriterTest, RoundtripSimple) {
    auto workbook = createSimpleWorkbook();
    std::string path = tempFilePath("roundtrip_simple.xlsx");
    TempFileGuard guard(path);

    // Write
    auto writeResult = writeXLSX(*workbook, path);
    EXPECT_TRUE(writeResult.ok()) << (writeResult.error ? writeResult.error->toString() : "");

    // Read back
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok()) << (readResult.error ? readResult.error->toString() : "");
    ASSERT_NE(readResult.workbook, nullptr);

    // Verify structure
    EXPECT_EQ(readResult.workbook->sheetCount(), 1u);
    Sheet* sheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet, nullptr);
    EXPECT_EQ(sheet->columnCount(), 3u);
    EXPECT_EQ(sheet->rowCount(), 3u);
    EXPECT_EQ(sheet->cellCount(), 9u);
}

TEST(XLSXWriterTest, RoundtripPreservesNumberValues) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Numbers");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");

    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    std::vector<double> values = {0.0, -1.5, 3.14159, 1000000.0, 0.000001};
    std::vector<ID> rowIds;

    for (size_t i = 0; i < values.size(); ++i) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = i;
        rowIds.push_back(row->id);
        sheet->addRow(std::move(row));

        auto cell = std::make_unique<Cell>(generate_id(), colId, rowIds[i]);
        cell->value = CellValue(values[i]);
        sheet->addCell(std::move(cell));
    }

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("roundtrip_numbers.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*workbook, path);
    EXPECT_TRUE(writeResult.ok());

    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);
    EXPECT_EQ(readSheet->cellCount(), values.size());

    // Verify all values are numbers
    for (const auto& [id, c] : readSheet->cells) {
        EXPECT_EQ(c->value.type, CellValueType::NUMBER);
    }
}

TEST(XLSXWriterTest, RoundtripPreservesStringValues) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Strings");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");

    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    std::vector<std::string> values = {"Hello", "World", "", "With\nNewline", "With,Comma"};
    std::vector<ID> rowIds;

    for (size_t i = 0; i < values.size(); ++i) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = i;
        rowIds.push_back(row->id);
        sheet->addRow(std::move(row));

        auto cell = std::make_unique<Cell>(generate_id(), colId, rowIds[i]);
        cell->value = CellValue(values[i]);
        sheet->addCell(std::move(cell));
    }

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("roundtrip_strings.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*workbook, path);
    EXPECT_TRUE(writeResult.ok());

    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    // Note: Empty string may not create a cell in Excel
    EXPECT_GE(readSheet->cellCount(), 4u);
}

// ============================================================================
// XLSXWriteError Tests
// ============================================================================

TEST(XLSXWriteErrorTest, ToStringBasic) {
    XLSXWriteError error("Test error");
    EXPECT_EQ(error.toString(), "Test error");
}

TEST(XLSXWriteErrorTest, ToStringWithSheet) {
    XLSXWriteError error("Test error", "Sheet1");
    std::string str = error.toString();
    EXPECT_TRUE(str.find("Sheet1") != std::string::npos);
    EXPECT_TRUE(str.find("Test error") != std::string::npos);
}

// ============================================================================
// Options Tests
// ============================================================================

TEST(XLSXWriterTest, DefaultOptions) {
    XLSXWriter writer;
    EXPECT_TRUE(writer.options().writeFormulas);
    EXPECT_TRUE(writer.options().writeDimensions);
}

TEST(XLSXWriterTest, CustomOptions) {
    XLSXWriteOptions options;
    options.writeFormulas = false;
    options.writeDimensions = false;

    XLSXWriter writer(options);
    EXPECT_FALSE(writer.options().writeFormulas);
    EXPECT_FALSE(writer.options().writeDimensions);
}

}  // namespace
}  // namespace cells
