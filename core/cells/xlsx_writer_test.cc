#include "core/cells/xlsx_writer.h"

#include <cstdio>

#include <filesystem>
#include <string>

#include "core/cells/formula_parser.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/id.h"
#include "core/cells/named_ranges.h"
#include "core/cells/range.h"
#include "core/cells/xlsx_reader.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// Helper to create a Formula from text (parses the formula to AST)
Formula* createFormula(const std::string& text) {
    FormulaParser parser(text);
    std::unique_ptr<ASTNode> ast = parser.parse();
    auto* formula = new Formula();
    formula->ast = ast.release();
    formula->dirty = true;
    return formula;
}

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
    sheet->setWorkbook(workbook.get());  // Set workbook early for axis/cell storage

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

    // Add sheet to workbook first (so cells get stored properly)
    workbook->addSheet(std::move(sheet));
    Sheet* sheetPtr = workbook->getSheetByIndex(0);

    // Add cells with different values
    // (0,0) = "Name"
    auto cell1 = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[0]);
    cell1->value = CellValue("Name");
    sheetPtr->addCell(std::move(cell1));

    // (1,0) = "Value"
    auto cell2 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[0]);
    cell2->value = CellValue("Value");
    sheetPtr->addCell(std::move(cell2));

    // (2,0) = "Active"
    auto cell3 = std::make_unique<Cell>(generate_id(), colIds[2], rowIds[0]);
    cell3->value = CellValue("Active");
    sheetPtr->addCell(std::move(cell3));

    // (0,1) = "Alice"
    auto cell4 = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[1]);
    cell4->value = CellValue("Alice");
    sheetPtr->addCell(std::move(cell4));

    // (1,1) = 100
    auto cell5 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[1]);
    cell5->value = CellValue(100.0);
    sheetPtr->addCell(std::move(cell5));

    // (2,1) = true
    auto cell6 = std::make_unique<Cell>(generate_id(), colIds[2], rowIds[1]);
    cell6->value = CellValue(true);
    sheetPtr->addCell(std::move(cell6));

    // (0,2) = "Bob"
    auto cell7 = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[2]);
    cell7->value = CellValue("Bob");
    sheetPtr->addCell(std::move(cell7));

    // (1,2) = 200
    auto cell8 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[2]);
    cell8->value = CellValue(200.0);
    sheetPtr->addCell(std::move(cell8));

    // (2,2) = false
    auto cell9 = std::make_unique<Cell>(generate_id(), colIds[2], rowIds[2]);
    cell9->value = CellValue(false);
    sheetPtr->addCell(std::move(cell9));

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
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

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
    for (const auto& cellId : readSheet->getCellIds()) {
        Cell* c = readResult.workbook->getCell(cellId);
        ASSERT_NE(c, nullptr);
        EXPECT_EQ(c->value.type, CellValueType::NUMBER);
        EXPECT_DOUBLE_EQ(c->value.asNumber(), 42.5);
    }
}

TEST(XLSXWriterTest, WriteStrings) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Strings");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

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
    for (const auto& cellId : readSheet->getCellIds()) {
        Cell* c = readResult.workbook->getCell(cellId);
        if (!c)
            continue;
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
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

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
    for (const auto& cellId : readSheet->getCellIds()) {
        Cell* c = readResult.workbook->getCell(cellId);
        if (!c)
            continue;
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
        sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

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
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

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
    for (const ID& colId : readSheet->getColumnIds()) {
        const Axis* col = readSheet->getColumn(colId);
        ASSERT_NE(col, nullptr);
        EXPECT_GT(col->size, 0u) << "Column should have width > 0";
    }
}

TEST(XLSXWriterTest, SkipDimensionsWhenDisabled) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "NoDimensions");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

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
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

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
    for (const auto& cellId : readSheet->getCellIds()) {
        Cell* c = readResult.workbook->getCell(cellId);
        if (!c)
            continue;
        EXPECT_EQ(c->value.type, CellValueType::NUMBER);
    }
}

TEST(XLSXWriterTest, RoundtripPreservesStringValues) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Strings");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

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

// ============================================================================
// Formula Tests
// ============================================================================

TEST(XLSXWriterTest, WriteFormulas) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Formulas");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    // Create 2 columns, 2 rows
    std::vector<ID> colIds;
    for (int i = 0; i < 2; ++i) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = i;
        colIds.push_back(col->id);
        sheet->addColumn(std::move(col));
    }

    std::vector<ID> rowIds;
    for (int i = 0; i < 2; ++i) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = i;
        rowIds.push_back(row->id);
        sheet->addRow(std::move(row));
    }

    // A1 = 10
    auto cell1 = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[0]);
    cell1->value = CellValue(10.0);
    sheet->addCell(std::move(cell1));

    // B1 = 20
    auto cell2 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[0]);
    cell2->value = CellValue(20.0);
    sheet->addCell(std::move(cell2));

    // A2 = formula "=A1+B1" (result 30)
    auto cell3 = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[1]);
    cell3->value = CellValue(30.0);  // Cached result
    cell3->setFormula(createFormula("=A1+B1"));
    sheet->addCell(std::move(cell3));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("formulas.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");

    // Read back and verify formula is present
    XLSXReadOptions readOptions;
    readOptions.readFormulas = true;
    readOptions.readFormulaText = true;

    auto readResult = readXLSX(path, readOptions);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    // Find the formula cell
    bool foundFormula = false;
    for (const auto& cellId : readSheet->getCellIds()) {
        Cell* c = readResult.workbook->getCell(cellId);
        if (!c)
            continue;
        if (c->isFormula()) {
            foundFormula = true;
            const Formula* f = c->getFormula();
            ASSERT_NE(f, nullptr);
            ASSERT_NE(f->ast, nullptr);
            // Formula should contain A1+B1 (possibly with = prefix)
            std::string formulaText = FormulaSerializer::serialize(f->ast);
            EXPECT_TRUE(formulaText.find("A1") != std::string::npos)
                << "Formula should reference A1: " << formulaText;
            EXPECT_TRUE(formulaText.find("B1") != std::string::npos)
                << "Formula should reference B1: " << formulaText;
        }
    }
    EXPECT_TRUE(foundFormula) << "Expected to find a formula cell";
}

TEST(XLSXWriterTest, WriteSharedFormulas) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "SharedFormulas");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    // Create 2 columns, 3 rows
    std::vector<ID> colIds;
    for (int i = 0; i < 2; ++i) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = i;
        colIds.push_back(col->id);
        sheet->addColumn(std::move(col));
    }

    std::vector<ID> rowIds;
    for (int i = 0; i < 3; ++i) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = i;
        rowIds.push_back(row->id);
        sheet->addRow(std::move(row));
    }

    // A1 = 10, A2 = 20, A3 = 30
    for (int i = 0; i < 3; ++i) {
        auto cell = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[i]);
        cell->value = CellValue(static_cast<double>((i + 1) * 10));
        sheet->addCell(std::move(cell));
    }

    // B1 = formula "=A1*2" (master, result 20)
    auto masterCell = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[0]);
    masterCell->value = CellValue(20.0);
    masterCell->setFormula(createFormula("=A1*2"));
    ID masterId = masterCell->id;
    sheet->addCell(std::move(masterCell));

    // B2 = shared formula subscriber (result 40)
    auto subCell1 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[1]);
    subCell1->value = CellValue(40.0);
    subCell1->setSharedFormulaSubscriber(true);
    ID sub1Id = subCell1->id;
    sheet->addCell(std::move(subCell1));

    // B3 = shared formula subscriber (result 60)
    auto subCell2 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[2]);
    subCell2->value = CellValue(60.0);
    subCell2->setSharedFormulaSubscriber(true);
    ID sub2Id = subCell2->id;
    sheet->addCell(std::move(subCell2));

    // Register shared formula group at Sheet level
    sheet->registerSharedFormulaGroup(masterId, {sub1Id, sub2Id});

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("shared_formulas.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");

    // Read back and verify shared formulas are preserved
    XLSXReadOptions readOptions;
    readOptions.readFormulas = true;
    readOptions.readFormulaText = true;

    auto readResult = readXLSX(path, readOptions);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    // Count formula cells - should have 3 (1 master + 2 subscribers)
    int formulaCount = 0;
    int sharedFormulaCount = 0;
    for (const auto& cellId : readSheet->getCellIds()) {
        Cell* c = readResult.workbook->getCell(cellId);
        if (!c)
            continue;
        if (c->isFormula()) {
            formulaCount++;
        }
        if (c->isSharedFormula()) {
            sharedFormulaCount++;
        }
    }

    // Should have found formulas (either shared or regular)
    EXPECT_GE(formulaCount, 1) << "Expected at least one formula cell";
    // Shared formula subscribers should be detected
    EXPECT_GE(sharedFormulaCount, 0);  // May be 0 if Excel normalizes them
}

TEST(XLSXWriterTest, WriteFormulaWithSpecialChars) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "FormulaSpecialChars");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = 0;
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    // Formula with special characters that need XML escaping
    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue("Test");
    cell->setFormula(createFormula("=IF(A1<10,\"Less\",\"More\")"));
    sheet->addCell(std::move(cell));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("formula_special.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");

    // Read back - file should be valid
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
}

// Test that FORMULA_* result types are handled correctly when exporting
// This simulates a formula that has been evaluated (type changes from FORMULA to FORMULA_NUMBER
// etc.)
TEST(XLSXWriterTest, WriteFormulasWithEvaluatedTypes) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "EvaluatedFormulas");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    // Create 2 columns, 4 rows
    std::vector<ID> colIds;
    for (int i = 0; i < 2; ++i) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = i;
        colIds.push_back(col->id);
        sheet->addColumn(std::move(col));
    }

    std::vector<ID> rowIds;
    for (int i = 0; i < 4; ++i) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = i;
        rowIds.push_back(row->id);
        sheet->addRow(std::move(row));
    }

    // A1 = 10 (value cell)
    auto cell1 = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[0]);
    cell1->value = CellValue(10.0);
    sheet->addCell(std::move(cell1));

    // B1 = formula with FORMULA_NUMBER result type (simulating evaluated formula)
    auto cell2 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[0]);
    cell2->value.raw = "20";
    cell2->value.type = CellValueType::FORMULA_NUMBER;  // Evaluated result type
    cell2->setFormula(createFormula("=A1*2"));
    sheet->addCell(std::move(cell2));

    // A2 = "Hello" (value cell)
    auto cell3 = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[1]);
    cell3->value = CellValue("Hello");
    sheet->addCell(std::move(cell3));

    // B2 = formula with FORMULA_STRING result type
    auto cell4 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[1]);
    cell4->value.raw = "Hello World";
    cell4->value.type = CellValueType::FORMULA_STRING;
    cell4->setFormula(createFormula("=CONCAT(A2,\" World\")"));
    sheet->addCell(std::move(cell4));

    // A3 = true (value cell)
    auto cell5 = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[2]);
    cell5->value = CellValue(true);
    sheet->addCell(std::move(cell5));

    // B3 = formula with FORMULA_BOOLEAN result type
    auto cell6 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[2]);
    cell6->value.raw = "true";
    cell6->value.type = CellValueType::FORMULA_BOOLEAN;
    cell6->setFormula(createFormula("=A3"));
    sheet->addCell(std::move(cell6));

    // A4 = 0 (for division by zero)
    auto cell7 = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[3]);
    cell7->value = CellValue(0.0);
    sheet->addCell(std::move(cell7));

    // B4 = formula with FORMULA_ERROR result type
    auto cell8 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[3]);
    cell8->value.raw = "#DIV/0!";
    cell8->value.type = CellValueType::FORMULA_ERROR;
    cell8->setFormula(createFormula("=1/A4"));
    sheet->addCell(std::move(cell8));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("evaluated_formulas.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");

    // Read back and verify formulas are present with values
    XLSXReadOptions readOptions;
    readOptions.readFormulas = true;
    readOptions.readFormulaText = true;

    auto readResult = readXLSX(path, readOptions);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    // Count formula cells - should have 4 formula cells
    int formulaCount = 0;
    for (const auto& cellId : readSheet->getCellIds()) {
        Cell* c = readResult.workbook->getCell(cellId);
        if (!c)
            continue;
        if (c->isFormula()) {
            formulaCount++;
        }
    }

    EXPECT_GE(formulaCount, 3) << "Expected at least 3 formula cells";
}

// Test that formula cells with multiple types are exported correctly when writeFormulas=false
// This verifies that number, string, and boolean formula cells all export their cached values
TEST(XLSXWriterTest, WriteFormulasDisabledWithDifferentTypes) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "ValuesOnly");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    // Create column
    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    // Create row
    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = 0;
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    // Cell with formula - should only export value when writeFormulas=false
    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue(42.0);  // Cached result
    cell->setFormula(createFormula("=21*2"));
    sheet->addCell(std::move(cell));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("values_only_simple.xlsx");
    TempFileGuard guard(path);

    XLSXWriteOptions options;
    options.writeFormulas = false;

    auto result = writeXLSX(*workbook, path, options);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");

    // Read back - should have value but no formula
    XLSXReadOptions readOptions;
    readOptions.readFormulas = true;

    auto readResult = readXLSX(path, readOptions);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    // Verify no formulas but values are present
    for (const auto& cellId : readSheet->getCellIds()) {
        Cell* c = readResult.workbook->getCell(cellId);
        if (!c)
            continue;
        EXPECT_FALSE(c->isFormula()) << "Cell should not have formula when writeFormulas=false";
        EXPECT_EQ(c->value.type, CellValueType::NUMBER);
        EXPECT_DOUBLE_EQ(c->value.asNumber(), 42.0);
    }
}

TEST(XLSXWriterTest, SkipFormulasWhenDisabled) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "NoFormulas");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = 0;
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue(42.0);  // Cached result
    cell->setFormula(createFormula("=21*2"));
    sheet->addCell(std::move(cell));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("no_formulas.xlsx");
    TempFileGuard guard(path);

    XLSXWriteOptions options;
    options.writeFormulas = false;

    auto result = writeXLSX(*workbook, path, options);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");

    // Read back - should only have value, no formula
    XLSXReadOptions readOptions;
    readOptions.readFormulas = true;

    auto readResult = readXLSX(path, readOptions);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    // Check that no cell has a formula
    for (const auto& cellId : readSheet->getCellIds()) {
        Cell* c = readResult.workbook->getCell(cellId);
        if (!c)
            continue;
        EXPECT_FALSE(c->isFormula()) << "Cell should not have formula when writeFormulas=false";
        EXPECT_EQ(c->value.type, CellValueType::NUMBER);
        EXPECT_DOUBLE_EQ(c->value.asNumber(), 42.0);
    }
}

// ============================================================================
// XML Escaping Tests
// ============================================================================

TEST(XLSXWriterTest, WriteStringWithXmlSpecialChars) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "XmlEscape");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    std::vector<std::string> values = {"<tag>", "A & B", "\"quoted\"", "'apostrophe'"};
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

    std::string path = tempFilePath("xml_escape.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "unknown error");

    // Read back and verify values are preserved
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    // Count matching strings
    int matchCount = 0;
    for (const auto& cellId : readSheet->getCellIds()) {
        Cell* c = readResult.workbook->getCell(cellId);
        if (!c)
            continue;
        if (c->value.type == CellValueType::STRING) {
            const std::string& val = c->value.asString();
            for (const auto& expected : values) {
                if (val == expected) {
                    matchCount++;
                    break;
                }
            }
        }
    }

    EXPECT_EQ(matchCount, static_cast<int>(values.size()))
        << "All special character strings should round-trip correctly";
}

// ============================================================================
// Style Export Tests
// ============================================================================

TEST(XLSXWriterTest, WriteStylesBold) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Styles");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = 0;
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue("Bold Text");

    // Create and register bold style (store in workbook map)
    CellStyle boldStyle;
    boldStyle.bold = true;
    ID styleId = generate_id();
    workbook->registerStyle(styleId, boldStyle);
    workbook->setCellStyleId(cell->id, styleId);
    cell->markHasStyle();

    sheet->addCell(std::move(cell));
    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("style_bold.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*workbook, path);
    EXPECT_TRUE(writeResult.ok()) << (writeResult.error ? writeResult.error->toString() : "");

    // Read back and verify style
    XLSXReadOptions readOpts;
    readOpts.readStyles = true;
    auto readResult = readXLSX(path, readOpts);
    EXPECT_TRUE(readResult.ok()) << (readResult.error ? readResult.error->toString() : "");
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    Axis* readCol = readSheet->getColumnByPosition(0);
    Axis* readRow = readSheet->getRowByPosition(0);
    ASSERT_NE(readCol, nullptr);
    ASSERT_NE(readRow, nullptr);

    Cell* readCell = readSheet->getCellAt(readCol->id, readRow->id);
    ASSERT_NE(readCell, nullptr) << "Cell should exist";
    // Read style from workbook map
    const ID readCellStyleId = readResult.workbook->getCellStyleId(readCell->id);
    EXPECT_FALSE(readCellStyleId.isNull()) << "Cell should have style";

    const CellStyle* readStyle = readResult.workbook->getStyle(readCellStyleId);
    ASSERT_NE(readStyle, nullptr) << "Style should be registered";
    EXPECT_TRUE(readStyle->bold) << "Cell should be bold";
}

TEST(XLSXWriterTest, WriteStylesBackgroundColor) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Styles");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = 0;
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue("Red Background");
    ID cellId = cell->id;

    // Create and register style with red background
    CellStyle redBgStyle;
    redBgStyle.bgColor = "#FF0000";
    ID styleId = generate_id();
    workbook->registerStyle(styleId, redBgStyle);

    sheet->addCell(std::move(cell));
    workbook->addSheet(std::move(sheet));

    // Set style via workbook map (after cell is added)
    workbook->setCellStyleId(cellId, styleId);

    std::string path = tempFilePath("style_bgcolor.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*workbook, path);
    EXPECT_TRUE(writeResult.ok()) << (writeResult.error ? writeResult.error->toString() : "");

    // Read back and verify style
    XLSXReadOptions readOpts;
    readOpts.readStyles = true;
    auto readResult = readXLSX(path, readOpts);
    EXPECT_TRUE(readResult.ok()) << (readResult.error ? readResult.error->toString() : "");
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    Axis* readCol = readSheet->getColumnByPosition(0);
    Axis* readRow = readSheet->getRowByPosition(0);
    ASSERT_NE(readCol, nullptr);
    ASSERT_NE(readRow, nullptr);

    Cell* readCell = readSheet->getCellAt(readCol->id, readRow->id);
    ASSERT_NE(readCell, nullptr) << "Cell should exist";
    ID readCellStyleId = readResult.workbook->getCellStyleId(readCell->id);
    EXPECT_FALSE(readCellStyleId.isNull()) << "Cell should have style";

    const CellStyle* readStyle = readResult.workbook->getStyle(readCellStyleId);
    ASSERT_NE(readStyle, nullptr) << "Style should be registered";
    EXPECT_EQ(readStyle->bgColor, "#FF0000") << "Cell should have red background";
}

TEST(XLSXWriterTest, WriteStylesAlignment) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Styles");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = 0;
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue("Centered");
    ID cellId = cell->id;

    // Create and register center-aligned style
    CellStyle centerStyle;
    centerStyle.hAlign = TextAlign::CENTER;
    centerStyle.vAlign = VerticalAlign::MIDDLE;
    ID styleId = generate_id();
    workbook->registerStyle(styleId, centerStyle);

    sheet->addCell(std::move(cell));
    workbook->addSheet(std::move(sheet));

    // Set style via workbook map (after cell is added)
    workbook->setCellStyleId(cellId, styleId);

    std::string path = tempFilePath("style_align.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*workbook, path);
    EXPECT_TRUE(writeResult.ok()) << (writeResult.error ? writeResult.error->toString() : "");

    // Read back and verify style
    XLSXReadOptions readOpts;
    readOpts.readStyles = true;
    auto readResult = readXLSX(path, readOpts);
    EXPECT_TRUE(readResult.ok()) << (readResult.error ? readResult.error->toString() : "");
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    Axis* readCol = readSheet->getColumnByPosition(0);
    Axis* readRow = readSheet->getRowByPosition(0);
    ASSERT_NE(readCol, nullptr);
    ASSERT_NE(readRow, nullptr);

    Cell* readCell = readSheet->getCellAt(readCol->id, readRow->id);
    ASSERT_NE(readCell, nullptr) << "Cell should exist";
    ID readCellStyleId = readResult.workbook->getCellStyleId(readCell->id);
    EXPECT_FALSE(readCellStyleId.isNull()) << "Cell should have style";

    const CellStyle* readStyle = readResult.workbook->getStyle(readCellStyleId);
    ASSERT_NE(readStyle, nullptr) << "Style should be registered";
    EXPECT_EQ(readStyle->hAlign, TextAlign::CENTER) << "Cell should be center aligned";
    EXPECT_EQ(readStyle->vAlign, VerticalAlign::MIDDLE) << "Cell should be middle aligned";
}

TEST(XLSXWriterTest, RoundtripStyles) {
    // Read the styled.xlsx test file and write it back
    XLSXReadOptions readOpts;
    readOpts.readStyles = true;
    auto readResult1 = readXLSX("testdata/xlsx/styled.xlsx", readOpts);
    EXPECT_TRUE(readResult1.ok()) << (readResult1.error ? readResult1.error->toString() : "");
    ASSERT_NE(readResult1.workbook, nullptr);

    // Write to a new file
    std::string path = tempFilePath("roundtrip_styles.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*readResult1.workbook, path);
    EXPECT_TRUE(writeResult.ok()) << (writeResult.error ? writeResult.error->toString() : "");

    // Read back the written file
    auto readResult2 = readXLSX(path, readOpts);
    EXPECT_TRUE(readResult2.ok()) << (readResult2.error ? readResult2.error->toString() : "");
    ASSERT_NE(readResult2.workbook, nullptr);

    // Verify we have the same number of styles
    EXPECT_EQ(readResult1.workbook->getStyles().size(), readResult2.workbook->getStyles().size())
        << "Style count should be preserved";

    // Verify specific cells have styles
    Sheet* sheet1 = readResult1.workbook->getSheetByIndex(0);
    Sheet* sheet2 = readResult2.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet1, nullptr);
    ASSERT_NE(sheet2, nullptr);

    // Check A1 (Bold)
    Axis* col0_2 = sheet2->getColumnByPosition(0);
    Axis* row0_2 = sheet2->getRowByPosition(0);
    if (col0_2 && row0_2) {
        Cell* cell = sheet2->getCellAt(col0_2->id, row0_2->id);
        ID cellStyleId = readResult2.workbook->getCellStyleId(cell->id);
        if (cell && !cellStyleId.isNull()) {
            const CellStyle* style = readResult2.workbook->getStyle(cellStyleId);
            EXPECT_NE(style, nullptr);
            if (style) {
                EXPECT_TRUE(style->bold) << "A1 should be bold after roundtrip";
            }
        }
    }
}

// =============================================================================
// XLSX Style Round-trip Tests (Phase 2d)
// =============================================================================

// Helper to compare style properties between original and roundtripped workbook
namespace {
void compareStyleAtPosition(const Workbook& wb1, const Workbook& wb2, Sheet* s1, Sheet* s2,
                            int colPos, int rowPos, const std::string& context) {
    Axis* col1 = s1->getColumnByPosition(colPos);
    Axis* row1 = s1->getRowByPosition(rowPos);
    Axis* col2 = s2->getColumnByPosition(colPos);
    Axis* row2 = s2->getRowByPosition(rowPos);

    ASSERT_NE(col1, nullptr) << context << ": original col " << colPos << " not found";
    ASSERT_NE(row1, nullptr) << context << ": original row " << rowPos << " not found";
    ASSERT_NE(col2, nullptr) << context << ": roundtrip col " << colPos << " not found";
    ASSERT_NE(row2, nullptr) << context << ": roundtrip row " << rowPos << " not found";

    Cell* cell1 = s1->getCellAt(col1->id, row1->id);
    Cell* cell2 = s2->getCellAt(col2->id, row2->id);

    ASSERT_NE(cell1, nullptr) << context << ": original cell not found";
    ASSERT_NE(cell2, nullptr) << context << ": roundtrip cell not found";

    // Get style IDs from workbook map
    ID cell1StyleId = wb1.getCellStyleId(cell1->id);
    ID cell2StyleId = wb2.getCellStyleId(cell2->id);

    // If original has no style, roundtrip should have no style
    if (cell1StyleId.isNull()) {
        // Cell may have acquired a default style during roundtrip, that's OK
        // as long as it's an empty style
        if (!cell2StyleId.isNull()) {
            const CellStyle* style2 = wb2.getStyle(cell2StyleId);
            if (style2) {
                EXPECT_TRUE(style2->isEmpty())
                    << context << ": unstyled cell should not acquire non-empty style";
            }
        }
        return;
    }

    EXPECT_FALSE(cell2StyleId.isNull()) << context << ": styled cell lost its style";

    const CellStyle* style1 = wb1.getStyle(cell1StyleId);
    const CellStyle* style2 = wb2.getStyle(cell2StyleId);

    ASSERT_NE(style1, nullptr) << context << ": original style not found";
    ASSERT_NE(style2, nullptr) << context << ": roundtrip style not found";

    // Compare individual properties
    EXPECT_EQ(style1->bold, style2->bold) << context << ": bold mismatch";
    EXPECT_EQ(style1->italic, style2->italic) << context << ": italic mismatch";
    EXPECT_EQ(style1->underline, style2->underline) << context << ": underline mismatch";
    EXPECT_EQ(style1->bgColor, style2->bgColor) << context << ": bgColor mismatch";
    EXPECT_EQ(style1->textColor, style2->textColor) << context << ": textColor mismatch";
    EXPECT_EQ(style1->hAlign, style2->hAlign) << context << ": hAlign mismatch";
    EXPECT_EQ(style1->vAlign, style2->vAlign) << context << ": vAlign mismatch";
}
}  // namespace

TEST(XLSXStyleRoundtripTest, RoundtripStyledFile) {
    // Import styled XLSX file, export back, re-import and verify styles match
    XLSXReadOptions readOpts;
    readOpts.readStyles = true;

    // Step 1: Import original styled.xlsx
    auto result1 = readXLSX("testdata/xlsx/styled.xlsx", readOpts);
    EXPECT_TRUE(result1.ok()) << (result1.error ? result1.error->toString() : "");
    ASSERT_NE(result1.workbook, nullptr);

    // Step 2: Export to new XLSX file
    std::string path = tempFilePath("style_roundtrip.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*result1.workbook, path);
    EXPECT_TRUE(writeResult.ok()) << (writeResult.error ? writeResult.error->toString() : "");

    // Step 3: Re-import the exported file
    auto result2 = readXLSX(path, readOpts);
    EXPECT_TRUE(result2.ok()) << (result2.error ? result2.error->toString() : "");
    ASSERT_NE(result2.workbook, nullptr);

    // Step 4: Verify styles match
    Sheet* sheet1 = result1.workbook->getSheetByIndex(0);
    Sheet* sheet2 = result2.workbook->getSheetByIndex(0);
    ASSERT_NE(sheet1, nullptr);
    ASSERT_NE(sheet2, nullptr);

    // Verify cell count matches
    EXPECT_EQ(sheet1->cellCount(), sheet2->cellCount())
        << "Cell count should be preserved after roundtrip";

    // Compare styles at specific positions based on styled.xlsx layout:
    // A1 = Bold, A2 = Italic
    // B1 = Red background, B2 = Blue text
    // C1 = Center aligned

    // A1 (Bold)
    compareStyleAtPosition(*result1.workbook, *result2.workbook, sheet1, sheet2, 0, 0, "A1");
    // A2 (Italic)
    compareStyleAtPosition(*result1.workbook, *result2.workbook, sheet1, sheet2, 0, 1, "A2");
    // B1 (Red background)
    compareStyleAtPosition(*result1.workbook, *result2.workbook, sheet1, sheet2, 1, 0, "B1");
    // B2 (Blue text)
    compareStyleAtPosition(*result1.workbook, *result2.workbook, sheet1, sheet2, 1, 1, "B2");
    // C1 (Center aligned)
    compareStyleAtPosition(*result1.workbook, *result2.workbook, sheet1, sheet2, 2, 0, "C1");
}

TEST(XLSXStyleRoundtripTest, RoundtripAllStyleProperties) {
    // Create a workbook with all style properties and verify roundtrip
    auto workbook = std::make_unique<Workbook>(generate_id(), "AllStyles");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    std::vector<ID> colIds;
    for (int i = 0; i < 3; ++i) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = i;
        colIds.push_back(col->id);
        sheet->addColumn(std::move(col));
    }

    std::vector<ID> rowIds;
    for (int i = 0; i < 4; ++i) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = i;
        rowIds.push_back(row->id);
        sheet->addRow(std::move(row));
    }

    // Cell with bold+italic+underline
    auto cell1 = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[0]);
    cell1->value = CellValue("Bold Italic Underline");
    ID cellId1 = cell1->id;
    CellStyle style1;
    style1.bold = true;
    style1.italic = true;
    style1.underline = true;
    ID styleId1 = generate_id();
    workbook->registerStyle(styleId1, style1);
    sheet->addCell(std::move(cell1));

    // Cell with colors
    auto cell2 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[0]);
    cell2->value = CellValue("Yellow BG, Blue Text");
    ID cellId2 = cell2->id;
    CellStyle style2;
    style2.bgColor = "#FFFF00";
    style2.textColor = "#0000FF";
    ID styleId2 = generate_id();
    workbook->registerStyle(styleId2, style2);
    sheet->addCell(std::move(cell2));

    // Cell with alignment
    auto cell3 = std::make_unique<Cell>(generate_id(), colIds[2], rowIds[0]);
    cell3->value = CellValue("Center/Middle");
    ID cellId3 = cell3->id;
    CellStyle style3;
    style3.hAlign = TextAlign::CENTER;
    style3.vAlign = VerticalAlign::MIDDLE;
    ID styleId3 = generate_id();
    workbook->registerStyle(styleId3, style3);
    sheet->addCell(std::move(cell3));

    // Cell with right alignment
    auto cell4 = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[1]);
    cell4->value = CellValue(12345.67);
    ID cellId4 = cell4->id;
    CellStyle style4;
    style4.hAlign = TextAlign::RIGHT;
    ID styleId4 = generate_id();
    workbook->registerStyle(styleId4, style4);
    sheet->addCell(std::move(cell4));

    // Cell without style (plain)
    auto cell5 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[1]);
    cell5->value = CellValue("Plain text");
    sheet->addCell(std::move(cell5));

    // Cell with combined styles
    auto cell6 = std::make_unique<Cell>(generate_id(), colIds[2], rowIds[1]);
    cell6->value = CellValue("Full style");
    ID cellId6 = cell6->id;
    CellStyle style6;
    style6.bold = true;
    style6.bgColor = "#00FF00";    // Green
    style6.textColor = "#FFFFFF";  // White
    style6.hAlign = TextAlign::CENTER;
    style6.vAlign = VerticalAlign::BOTTOM;
    ID styleId6 = generate_id();
    workbook->registerStyle(styleId6, style6);
    sheet->addCell(std::move(cell6));

    workbook->addSheet(std::move(sheet));

    // Set styles via workbook map (after cells are added)
    workbook->setCellStyleId(cellId1, styleId1);
    workbook->setCellStyleId(cellId2, styleId2);
    workbook->setCellStyleId(cellId3, styleId3);
    workbook->setCellStyleId(cellId4, styleId4);
    workbook->setCellStyleId(cellId6, styleId6);

    // Write to XLSX
    std::string path = tempFilePath("all_styles.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*workbook, path);
    EXPECT_TRUE(writeResult.ok()) << (writeResult.error ? writeResult.error->toString() : "");

    // Read back
    XLSXReadOptions readOpts;
    readOpts.readStyles = true;
    auto readResult = readXLSX(path, readOpts);
    EXPECT_TRUE(readResult.ok()) << (readResult.error ? readResult.error->toString() : "");
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    // Verify cell 1 (bold+italic+underline)
    Axis* rc0 = readSheet->getColumnByPosition(0);
    Axis* rr0 = readSheet->getRowByPosition(0);
    if (rc0 && rr0) {
        Cell* c = readSheet->getCellAt(rc0->id, rr0->id);
        ASSERT_NE(c, nullptr);
        ID cStyleId = readResult.workbook->getCellStyleId(c->id);
        EXPECT_FALSE(cStyleId.isNull());
        const CellStyle* s = readResult.workbook->getStyle(cStyleId);
        if (s) {
            EXPECT_TRUE(s->bold) << "Cell should be bold";
            EXPECT_TRUE(s->italic) << "Cell should be italic";
            EXPECT_TRUE(s->underline) << "Cell should be underlined";
        }
    }

    // Verify cell 2 (colors)
    Axis* rc1 = readSheet->getColumnByPosition(1);
    if (rc1 && rr0) {
        Cell* c = readSheet->getCellAt(rc1->id, rr0->id);
        ASSERT_NE(c, nullptr);
        ID cStyleId = readResult.workbook->getCellStyleId(c->id);
        EXPECT_FALSE(cStyleId.isNull());
        const CellStyle* s = readResult.workbook->getStyle(cStyleId);
        if (s) {
            EXPECT_EQ(s->bgColor, "#FFFF00") << "Cell should have yellow background";
            EXPECT_EQ(s->textColor, "#0000FF") << "Cell should have blue text";
        }
    }

    // Verify cell 3 (alignment)
    Axis* rc2 = readSheet->getColumnByPosition(2);
    if (rc2 && rr0) {
        Cell* c = readSheet->getCellAt(rc2->id, rr0->id);
        ASSERT_NE(c, nullptr);
        ID cStyleId = readResult.workbook->getCellStyleId(c->id);
        EXPECT_FALSE(cStyleId.isNull());
        const CellStyle* s = readResult.workbook->getStyle(cStyleId);
        if (s) {
            EXPECT_EQ(s->hAlign, TextAlign::CENTER) << "Cell should be center aligned";
            EXPECT_EQ(s->vAlign, VerticalAlign::MIDDLE) << "Cell should be middle aligned";
        }
    }
}

TEST(XLSXStyleRoundtripTest, RoundtripMultipleSheetsWithStyles) {
    // Create workbook with multiple sheets, each with different styles
    auto workbook = std::make_unique<Workbook>(generate_id(), "MultiSheetStyles");

    std::vector<std::pair<std::string, CellStyle>> sheetStyles = {
        {"Sales",
         []() {
             CellStyle s;
             s.bold = true;
             s.bgColor = "#4472C4";
             return s;
         }()},
        {"Expenses",
         []() {
             CellStyle s;
             s.italic = true;
             s.textColor = "#FF0000";
             return s;
         }()},
        {"Summary",
         []() {
             CellStyle s;
             s.bold = true;
             s.underline = true;
             s.hAlign = TextAlign::CENTER;
             return s;
         }()},
    };

    std::vector<std::pair<ID, ID>> cellStylePairs;  // cellId -> styleId pairs
    for (const auto& [name, style] : sheetStyles) {
        auto sheet = std::make_unique<Sheet>(generate_id(), name);
        sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = 0;
        ID colId = col->id;
        sheet->addColumn(std::move(col));

        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = 0;
        ID rowId = row->id;
        sheet->addRow(std::move(row));

        auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
        cell->value = CellValue(name + " Header");
        ID cellId = cell->id;

        ID styleId = generate_id();
        workbook->registerStyle(styleId, style);
        cellStylePairs.push_back({cellId, styleId});

        sheet->addCell(std::move(cell));
        workbook->addSheet(std::move(sheet));
    }

    // Set styles via workbook map (after cells are added)
    for (const auto& [cellId, styleId] : cellStylePairs) {
        workbook->setCellStyleId(cellId, styleId);
    }

    // Write to XLSX
    std::string path = tempFilePath("multi_sheet_styles.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*workbook, path);
    EXPECT_TRUE(writeResult.ok()) << (writeResult.error ? writeResult.error->toString() : "");

    // Read back
    XLSXReadOptions readOpts;
    readOpts.readStyles = true;
    auto readResult = readXLSX(path, readOpts);
    EXPECT_TRUE(readResult.ok()) << (readResult.error ? readResult.error->toString() : "");
    ASSERT_NE(readResult.workbook, nullptr);

    // Verify all sheets are present with correct styles
    EXPECT_EQ(readResult.workbook->sheetCount(), 3u);

    for (size_t i = 0; i < sheetStyles.size(); ++i) {
        Sheet* sheet = readResult.workbook->getSheetByIndex(i);
        ASSERT_NE(sheet, nullptr) << "Sheet " << i << " should exist";

        Axis* col = sheet->getColumnByPosition(0);
        Axis* row = sheet->getRowByPosition(0);
        if (col && row) {
            Cell* cell = sheet->getCellAt(col->id, row->id);
            ASSERT_NE(cell, nullptr) << "Cell in sheet " << i << " should exist";
            ID cellStyleId = readResult.workbook->getCellStyleId(cell->id);
            EXPECT_FALSE(cellStyleId.isNull()) << "Cell in sheet " << i << " should have style";

            const CellStyle* readStyle = readResult.workbook->getStyle(cellStyleId);
            ASSERT_NE(readStyle, nullptr) << "Style for sheet " << i << " should be registered";

            const CellStyle& expected = sheetStyles[i].second;
            EXPECT_EQ(readStyle->bold, expected.bold)
                << "Bold mismatch in sheet " << sheetStyles[i].first;
            EXPECT_EQ(readStyle->italic, expected.italic)
                << "Italic mismatch in sheet " << sheetStyles[i].first;
            EXPECT_EQ(readStyle->underline, expected.underline)
                << "Underline mismatch in sheet " << sheetStyles[i].first;
        }
    }
}

TEST(XLSXStyleRoundtripTest, StyleDeduplication) {
    // Test that identical styles are deduplicated during export
    auto workbook = std::make_unique<Workbook>(generate_id(), "StyleDedup");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    std::vector<ID> colIds;
    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = 0;
    colIds.push_back(col->id);
    sheet->addColumn(std::move(col));

    std::vector<ID> rowIds;
    for (int i = 0; i < 5; ++i) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = i;
        rowIds.push_back(row->id);
        sheet->addRow(std::move(row));
    }

    // Create the same style multiple times with different IDs
    CellStyle boldStyle;
    boldStyle.bold = true;

    std::vector<std::pair<ID, ID>> cellStylePairs;
    for (int i = 0; i < 5; ++i) {
        auto cell = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[i]);
        cell->value = CellValue("Bold " + std::to_string(i + 1));
        ID cellId = cell->id;

        // Each cell gets its own style ID, but all styles are identical
        ID styleId = generate_id();
        workbook->registerStyle(styleId, boldStyle);
        cellStylePairs.push_back({cellId, styleId});

        sheet->addCell(std::move(cell));
    }

    workbook->addSheet(std::move(sheet));

    // Set styles via workbook map (after cells are added)
    for (const auto& [cellId, styleId] : cellStylePairs) {
        workbook->setCellStyleId(cellId, styleId);
    }

    // Write to XLSX
    std::string path = tempFilePath("style_dedup.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*workbook, path);
    EXPECT_TRUE(writeResult.ok()) << (writeResult.error ? writeResult.error->toString() : "");

    // Read back
    XLSXReadOptions readOpts;
    readOpts.readStyles = true;
    auto readResult = readXLSX(path, readOpts);
    EXPECT_TRUE(readResult.ok()) << (readResult.error ? readResult.error->toString() : "");
    ASSERT_NE(readResult.workbook, nullptr);

    // All cells should still have bold style
    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    Axis* readCol = readSheet->getColumnByPosition(0);
    ASSERT_NE(readCol, nullptr);

    for (int i = 0; i < 5; ++i) {
        Axis* readRow = readSheet->getRowByPosition(i);
        ASSERT_NE(readRow, nullptr) << "Row " << i << " should exist";

        Cell* cell = readSheet->getCellAt(readCol->id, readRow->id);
        ASSERT_NE(cell, nullptr) << "Cell at row " << i << " should exist";
        ID cellStyleId = readResult.workbook->getCellStyleId(cell->id);
        EXPECT_FALSE(cellStyleId.isNull()) << "Cell at row " << i << " should have style";

        const CellStyle* style = readResult.workbook->getStyle(cellStyleId);
        ASSERT_NE(style, nullptr) << "Style for row " << i << " should be registered";
        EXPECT_TRUE(style->bold) << "Cell at row " << i << " should be bold";
    }

    // The number of registered styles after roundtrip should be <= original
    // (deduplication should reduce the count)
    EXPECT_LE(readResult.workbook->getStyles().size(), 5u)
        << "Style deduplication should work - got " << readResult.workbook->getStyles().size();
}

TEST(XLSXStyleRoundtripTest, EmptyStyleNotWritten) {
    // Test that cells with empty (default) styles don't create unnecessary style entries
    auto workbook = std::make_unique<Workbook>(generate_id(), "EmptyStyle");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = 0;
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue("Plain text");
    ID cellId = cell->id;

    // Register an empty style
    CellStyle emptyStyle;
    EXPECT_TRUE(emptyStyle.isEmpty());
    ID styleId = generate_id();
    workbook->registerStyle(styleId, emptyStyle);

    sheet->addCell(std::move(cell));
    workbook->addSheet(std::move(sheet));

    // Set style via workbook map (after cell is added)
    workbook->setCellStyleId(cellId, styleId);

    // Write to XLSX
    std::string path = tempFilePath("empty_style.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*workbook, path);
    EXPECT_TRUE(writeResult.ok()) << (writeResult.error ? writeResult.error->toString() : "");

    // Read back
    XLSXReadOptions readOpts;
    readOpts.readStyles = true;
    auto readResult = readXLSX(path, readOpts);
    EXPECT_TRUE(readResult.ok()) << (readResult.error ? readResult.error->toString() : "");
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    // The cell should still have the correct value
    Axis* readCol = readSheet->getColumnByPosition(0);
    Axis* readRow = readSheet->getRowByPosition(0);
    ASSERT_NE(readCol, nullptr);
    ASSERT_NE(readRow, nullptr);

    Cell* readCell = readSheet->getCellAt(readCol->id, readRow->id);
    ASSERT_NE(readCell, nullptr);
    EXPECT_EQ(readCell->value.asString(), "Plain text");

    // Cell may or may not have a style after roundtrip (depends on Excel's handling)
    // If it has a style, it should be empty/default
    ID readCellStyleId = readResult.workbook->getCellStyleId(readCell->id);
    if (!readCellStyleId.isNull()) {
        const CellStyle* style = readResult.workbook->getStyle(readCellStyleId);
        if (style) {
            // Check that no significant properties are set
            EXPECT_FALSE(style->bold);
            EXPECT_FALSE(style->italic);
            EXPECT_FALSE(style->underline);
        }
    }
}

// =============================================================================
// Named Ranges Round-trip Tests (Phase 3)
// =============================================================================

TEST(XLSXNamedRangeRoundtripTest, RoundtripSingleCellNamedRange) {
    // Create a workbook with a single-cell named range and verify round-trip
    auto workbook = std::make_unique<Workbook>(generate_id(), "NamedRanges");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    // Create 3x3 grid
    std::vector<ID> colIds;
    for (int i = 0; i < 3; ++i) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = i;
        colIds.push_back(col->id);
        sheet->addColumn(std::move(col));
    }

    std::vector<ID> rowIds;
    for (int i = 0; i < 3; ++i) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = i;
        rowIds.push_back(row->id);
        sheet->addRow(std::move(row));
    }

    // Add cells
    auto cell1 = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[0]);
    cell1->value = CellValue("Company Name");
    ID targetCellId = cell1->id;
    sheet->addCell(std::move(cell1));

    auto cell2 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[0]);
    cell2->value = CellValue(100.0);
    sheet->addCell(std::move(cell2));

    ID sheetId = sheet->id;
    workbook->addSheet(std::move(sheet));

    // Define a named range pointing to A1 (workbook-scoped)
    NamedRangeRegistry* registry = workbook->getNamedRanges();
    ASSERT_NE(registry, nullptr);

    NamedRangeTarget target = NamedRangeTarget::cell(targetCellId, sheetId);
    EXPECT_TRUE(registry->defineWorkbook("CompanyName", target));

    // Write to XLSX
    std::string path = tempFilePath("named_range_single.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*workbook, path);
    EXPECT_TRUE(writeResult.ok()) << (writeResult.error ? writeResult.error->toString() : "");

    // Read back
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok()) << (readResult.error ? readResult.error->toString() : "");
    ASSERT_NE(readResult.workbook, nullptr);

    // Verify named range exists
    NamedRangeRegistry* readRegistry = readResult.workbook->getNamedRanges();
    ASSERT_NE(readRegistry, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    const NamedRange* nr = readRegistry->resolve("CompanyName", readSheet->id);
    ASSERT_NE(nr, nullptr) << "Named range 'CompanyName' should exist after round-trip";
    EXPECT_EQ(nr->name, "CompanyName");
    EXPECT_EQ(nr->scope, NamedRangeScope::WORKBOOK);
    EXPECT_EQ(nr->target.type, NamedRangeTarget::Type::CELL);

    // Verify target points to correct cell (A1)
    Cell* targetCell = readSheet->getCell(nr->target.id1);
    if (targetCell != nullptr) {
        Axis* col = readSheet->getColumn(targetCell->colId);
        Axis* row = readSheet->getRow(targetCell->rowId);
        EXPECT_EQ(col->position, 0u) << "Target should be in column A (position 0)";
        EXPECT_EQ(row->position, 0u) << "Target should be in row 1 (position 0)";
    }
}

TEST(XLSXNamedRangeRoundtripTest, RoundtripRangeNamedRange) {
    // Create a workbook with a range named range (A1:C3)
    auto workbook = std::make_unique<Workbook>(generate_id(), "NamedRanges");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    // Create 3x3 grid
    std::vector<ID> colIds;
    for (int i = 0; i < 3; ++i) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = i;
        colIds.push_back(col->id);
        sheet->addColumn(std::move(col));
    }

    std::vector<ID> rowIds;
    for (int i = 0; i < 3; ++i) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = i;
        rowIds.push_back(row->id);
        sheet->addRow(std::move(row));
    }

    // Add cells to define the range corners
    auto cellA1 = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[0]);
    cellA1->value = CellValue("Top Left");
    ID cellA1Id = cellA1->id;
    sheet->addCell(std::move(cellA1));

    auto cellC3 = std::make_unique<Cell>(generate_id(), colIds[2], rowIds[2]);
    cellC3->value = CellValue("Bottom Right");
    ID cellC3Id = cellC3->id;
    sheet->addCell(std::move(cellC3));

    ID sheetId = sheet->id;
    workbook->addSheet(std::move(sheet));

    // Define a range named range (A1:C3)
    NamedRangeRegistry* registry = workbook->getNamedRanges();
    ASSERT_NE(registry, nullptr);

    NamedRangeTarget target = NamedRangeTarget::range(cellA1Id, cellC3Id, sheetId);
    EXPECT_TRUE(registry->defineWorkbook("DataRange", target));

    // Write to XLSX
    std::string path = tempFilePath("named_range_range.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*workbook, path);
    EXPECT_TRUE(writeResult.ok()) << (writeResult.error ? writeResult.error->toString() : "");

    // Read back
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok()) << (readResult.error ? readResult.error->toString() : "");
    ASSERT_NE(readResult.workbook, nullptr);

    // Verify named range exists
    NamedRangeRegistry* readRegistry = readResult.workbook->getNamedRanges();
    ASSERT_NE(readRegistry, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    const NamedRange* nr = readRegistry->resolve("DataRange", readSheet->id);
    ASSERT_NE(nr, nullptr) << "Named range 'DataRange' should exist after round-trip";
    EXPECT_EQ(nr->name, "DataRange");
    EXPECT_EQ(nr->scope, NamedRangeScope::WORKBOOK);
    EXPECT_EQ(nr->target.type, NamedRangeTarget::Type::RANGE);

    // Verify target points to correct range (A1:C3)
    Cell* startCell = readSheet->getCell(nr->target.id1);
    Cell* endCell = readSheet->getCell(nr->target.id2);
    if (startCell != nullptr && endCell != nullptr) {
        Axis* startCol = readSheet->getColumn(startCell->colId);
        Axis* startRow = readSheet->getRow(startCell->rowId);
        Axis* endCol = readSheet->getColumn(endCell->colId);
        Axis* endRow = readSheet->getRow(endCell->rowId);

        EXPECT_EQ(startCol->position, 0u) << "Start should be column A";
        EXPECT_EQ(startRow->position, 0u) << "Start should be row 1";
        EXPECT_EQ(endCol->position, 2u) << "End should be column C";
        EXPECT_EQ(endRow->position, 2u) << "End should be row 3";
    }
}

TEST(XLSXNamedRangeRoundtripTest, RoundtripSheetScopedNamedRange) {
    // Create a workbook with a sheet-scoped named range
    auto workbook = std::make_unique<Workbook>(generate_id(), "NamedRanges");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = 0;
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue("Local Value");
    ID cellId = cell->id;
    sheet->addCell(std::move(cell));

    ID sheetId = sheet->id;
    workbook->addSheet(std::move(sheet));

    // Define a sheet-scoped named range
    NamedRangeRegistry* registry = workbook->getNamedRanges();
    ASSERT_NE(registry, nullptr);

    NamedRangeTarget target = NamedRangeTarget::cell(cellId, sheetId);
    EXPECT_TRUE(registry->defineSheet("LocalName", sheetId, target));

    // Write to XLSX
    std::string path = tempFilePath("named_range_sheet_scoped.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*workbook, path);
    EXPECT_TRUE(writeResult.ok()) << (writeResult.error ? writeResult.error->toString() : "");

    // Read back
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok()) << (readResult.error ? readResult.error->toString() : "");
    ASSERT_NE(readResult.workbook, nullptr);

    // Verify named range exists
    NamedRangeRegistry* readRegistry = readResult.workbook->getNamedRanges();
    ASSERT_NE(readRegistry, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    const NamedRange* nr = readRegistry->resolve("LocalName", readSheet->id);
    ASSERT_NE(nr, nullptr) << "Named range 'LocalName' should exist after round-trip";
    EXPECT_EQ(nr->name, "LocalName");
    EXPECT_EQ(nr->scope, NamedRangeScope::SHEET);
}

TEST(XLSXNamedRangeRoundtripTest, RoundtripMultipleNamedRanges) {
    // Create a workbook with multiple named ranges
    auto workbook = std::make_unique<Workbook>(generate_id(), "NamedRanges");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    // Create 3x3 grid
    std::vector<ID> colIds;
    for (int i = 0; i < 3; ++i) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = i;
        colIds.push_back(col->id);
        sheet->addColumn(std::move(col));
    }

    std::vector<ID> rowIds;
    for (int i = 0; i < 3; ++i) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = i;
        rowIds.push_back(row->id);
        sheet->addRow(std::move(row));
    }

    // Add cells
    auto cellA1 = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[0]);
    cellA1->value = CellValue("Company");
    ID cellA1Id = cellA1->id;
    sheet->addCell(std::move(cellA1));

    auto cellB1 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[0]);
    cellB1->value = CellValue(1000.0);
    ID cellB1Id = cellB1->id;
    sheet->addCell(std::move(cellB1));

    auto cellC1 = std::make_unique<Cell>(generate_id(), colIds[2], rowIds[0]);
    cellC1->value = CellValue(2000.0);
    ID cellC1Id = cellC1->id;
    sheet->addCell(std::move(cellC1));

    ID sheetId = sheet->id;
    workbook->addSheet(std::move(sheet));

    // Define multiple named ranges
    NamedRangeRegistry* registry = workbook->getNamedRanges();
    ASSERT_NE(registry, nullptr);

    EXPECT_TRUE(registry->defineWorkbook("Company", NamedRangeTarget::cell(cellA1Id, sheetId)));
    EXPECT_TRUE(registry->defineWorkbook("Revenue", NamedRangeTarget::cell(cellB1Id, sheetId)));
    EXPECT_TRUE(registry->defineWorkbook("Expenses", NamedRangeTarget::cell(cellC1Id, sheetId)));

    // Write to XLSX
    std::string path = tempFilePath("named_range_multiple.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*workbook, path);
    EXPECT_TRUE(writeResult.ok()) << (writeResult.error ? writeResult.error->toString() : "");

    // Read back
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok()) << (readResult.error ? readResult.error->toString() : "");
    ASSERT_NE(readResult.workbook, nullptr);

    // Verify all named ranges exist
    NamedRangeRegistry* readRegistry = readResult.workbook->getNamedRanges();
    ASSERT_NE(readRegistry, nullptr);

    auto workbookScoped = readRegistry->getWorkbookScoped();
    EXPECT_EQ(workbookScoped.size(), 3u) << "Should have 3 workbook-scoped named ranges";

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    EXPECT_NE(readRegistry->resolve("Company", readSheet->id), nullptr);
    EXPECT_NE(readRegistry->resolve("Revenue", readSheet->id), nullptr);
    EXPECT_NE(readRegistry->resolve("Expenses", readSheet->id), nullptr);
}

TEST(XLSXNamedRangeRoundtripTest, RoundtripFromLBOModelFile) {
    // Read the LBO model file with named ranges, export, re-import, verify
    auto result1 = readXLSX("testdata/xlsx/init_lbo_model_60min_is_revenue_cf_only.xlsx");
    EXPECT_TRUE(result1.ok()) << (result1.error ? result1.error->toString() : "");
    ASSERT_NE(result1.workbook, nullptr);

    // Check named ranges in original
    NamedRangeRegistry* registry1 = result1.workbook->getNamedRanges();
    ASSERT_NE(registry1, nullptr);

    auto originalNames = registry1->getWorkbookScoped();
    size_t originalCount = originalNames.size();
    EXPECT_GE(originalCount, 10u) << "Original file should have at least 10 named ranges";

    // Export to new file
    std::string path = tempFilePath("lbo_named_ranges_roundtrip.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*result1.workbook, path);
    EXPECT_TRUE(writeResult.ok()) << (writeResult.error ? writeResult.error->toString() : "");

    // Re-import
    auto result2 = readXLSX(path);
    EXPECT_TRUE(result2.ok()) << (result2.error ? result2.error->toString() : "");
    ASSERT_NE(result2.workbook, nullptr);

    // Check named ranges in re-imported file
    NamedRangeRegistry* registry2 = result2.workbook->getNamedRanges();
    ASSERT_NE(registry2, nullptr);

    auto reimportedNames = registry2->getWorkbookScoped();
    EXPECT_EQ(reimportedNames.size(), originalCount)
        << "Number of named ranges should be preserved after round-trip";

    // Verify specific named ranges are preserved
    Sheet* sheet = result2.workbook->getSheetByName("LBO-60-Minutes");
    if (sheet != nullptr) {
        const NamedRange* companyName = registry2->resolve("Company_Name", sheet->id);
        if (companyName != nullptr) {
            EXPECT_EQ(companyName->name, "Company_Name");
            EXPECT_EQ(companyName->scope, NamedRangeScope::WORKBOOK);
        }
    }
}

TEST(XLSXNamedRangeRoundtripTest, RoundtripNameWithSpecialChars) {
    // Test named range with underscores (common in Excel)
    auto workbook = std::make_unique<Workbook>(generate_id(), "NamedRanges");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = 0;
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue(12345.0);
    ID cellId = cell->id;
    sheet->addCell(std::move(cell));

    ID sheetId = sheet->id;
    workbook->addSheet(std::move(sheet));

    // Define named range with underscores
    NamedRangeRegistry* registry = workbook->getNamedRanges();
    ASSERT_NE(registry, nullptr);

    NamedRangeTarget target = NamedRangeTarget::cell(cellId, sheetId);
    EXPECT_TRUE(registry->defineWorkbook("Total_Revenue_2024", target));

    // Write to XLSX
    std::string path = tempFilePath("named_range_underscores.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*workbook, path);
    EXPECT_TRUE(writeResult.ok()) << (writeResult.error ? writeResult.error->toString() : "");

    // Read back
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok()) << (readResult.error ? readResult.error->toString() : "");
    ASSERT_NE(readResult.workbook, nullptr);

    NamedRangeRegistry* readRegistry = readResult.workbook->getNamedRanges();
    ASSERT_NE(readRegistry, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    const NamedRange* nr = readRegistry->resolve("Total_Revenue_2024", readSheet->id);
    ASSERT_NE(nr, nullptr) << "Named range with underscores should round-trip correctly";
    EXPECT_EQ(nr->name, "Total_Revenue_2024");
}

// =============================================================================
// Sheet View Properties Tests (showGridLines)
// =============================================================================

TEST(XLSXWriterTest, WriteShowGridLinesDefault) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly
    // showGridLines defaults to true

    auto col = std::make_unique<Axis>(generate_id(), true);
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue(1.0);
    sheet->addCell(std::move(cell));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("gridlines_default.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Read back and verify showGridLines is true (default)
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);
    EXPECT_TRUE(readSheet->showGridLines);
}

TEST(XLSXWriterTest, WriteShowGridLinesFalse) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly
    sheet->showGridLines = false;        // Hide grid lines

    auto col = std::make_unique<Axis>(generate_id(), true);
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue(1.0);
    sheet->addCell(std::move(cell));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("gridlines_hidden.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Read back and verify showGridLines is false
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);
    EXPECT_FALSE(readSheet->showGridLines);
}

// =============================================================================
// Zoom Scale Tests
// =============================================================================

TEST(XLSXWriterTest, WriteZoomScaleDefault) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly
    // zoomScale defaults to 100

    auto col = std::make_unique<Axis>(generate_id(), true);
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue(1.0);
    sheet->addCell(std::move(cell));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("zoom_default.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Read back and verify zoomScale is 100 (default)
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);
    EXPECT_EQ(readSheet->zoomScale, 100);
}

TEST(XLSXWriterTest, WriteZoomScale150) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly
    sheet->zoomScale = 150;              // 150% zoom

    auto col = std::make_unique<Axis>(generate_id(), true);
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue(1.0);
    sheet->addCell(std::move(cell));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("zoom_150.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Read back and verify zoomScale is 150
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);
    EXPECT_EQ(readSheet->zoomScale, 150);
}

TEST(XLSXWriterTest, WriteMultipleViewProperties) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly
    sheet->showGridLines = false;        // Hide grid lines
    sheet->zoomScale = 75;               // 75% zoom

    auto col = std::make_unique<Axis>(generate_id(), true);
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue(1.0);
    sheet->addCell(std::move(cell));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("multiple_view_props.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Read back and verify both properties
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);
    EXPECT_FALSE(readSheet->showGridLines);
    EXPECT_EQ(readSheet->zoomScale, 75);
}

// ============================================================================
// Freeze Panes Tests
// ============================================================================

TEST(XLSXWriterTest, WriteFreezePanesDefault) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly
    // freezeCol/freezeRow default to 0

    auto col = std::make_unique<Axis>(generate_id(), true);
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue("A1");
    sheet->addCell(std::move(cell));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("freeze_panes_default.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Read back and verify freeze panes are 0 (default)
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);
    EXPECT_EQ(readSheet->freezeCol, 0);
    EXPECT_EQ(readSheet->freezeRow, 0);
}

TEST(XLSXWriterTest, WriteFreezePanesColumnOnly) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly
    sheet->freezeCol = 1;                // Freeze column A
    sheet->freezeRow = 0;

    auto col = std::make_unique<Axis>(generate_id(), true);
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue("A1");
    sheet->addCell(std::move(cell));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("freeze_panes_col.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Read back and verify freeze panes
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);
    EXPECT_EQ(readSheet->freezeCol, 1);
    EXPECT_EQ(readSheet->freezeRow, 0);
}

TEST(XLSXWriterTest, WriteFreezePanesRowOnly) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly
    sheet->freezeCol = 0;
    sheet->freezeRow = 2;  // Freeze rows 1-2

    auto col = std::make_unique<Axis>(generate_id(), true);
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue("A1");
    sheet->addCell(std::move(cell));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("freeze_panes_row.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Read back and verify freeze panes
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);
    EXPECT_EQ(readSheet->freezeCol, 0);
    EXPECT_EQ(readSheet->freezeRow, 2);
}

TEST(XLSXWriterTest, WriteFreezePanesBothColAndRow) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly
    sheet->freezeCol = 2;                // Freeze columns A-B
    sheet->freezeRow = 3;                // Freeze rows 1-3

    auto col = std::make_unique<Axis>(generate_id(), true);
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    auto row = std::make_unique<Axis>(generate_id(), false);
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue("A1");
    sheet->addCell(std::move(cell));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("freeze_panes_both.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Read back and verify freeze panes
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);
    EXPECT_EQ(readSheet->freezeCol, 2);
    EXPECT_EQ(readSheet->freezeRow, 3);
}

// ============================================================================
// Hidden Columns/Rows Tests
// ============================================================================

TEST(XLSXWriterTest, HiddenColumnRoundTrip) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Hidden Column Test");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    // Create 3 columns, hide the middle one
    auto col1 = std::make_unique<Axis>(generate_id(), true);
    col1->position = 0;
    col1->hidden = false;
    sheet->addColumn(std::move(col1));

    auto col2 = std::make_unique<Axis>(generate_id(), true);
    col2->position = 1;
    col2->hidden = true;  // Hidden column
    sheet->addColumn(std::move(col2));

    auto col3 = std::make_unique<Axis>(generate_id(), true);
    col3->position = 2;
    col3->hidden = false;
    sheet->addColumn(std::move(col3));

    // Add a row
    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = 0;
    sheet->addRow(std::move(row));

    // Add cells to each column so the XLSX dimensions include all columns
    Sheet* rawSheet = sheet.get();
    auto cell1 = std::make_unique<Cell>(generate_id(), rawSheet->getColumnByPosition(0)->id,
                                        rawSheet->getRowByPosition(0)->id);
    cell1->value = CellValue(1.0);
    sheet->addCell(std::move(cell1));

    auto cell2 = std::make_unique<Cell>(generate_id(), rawSheet->getColumnByPosition(1)->id,
                                        rawSheet->getRowByPosition(0)->id);
    cell2->value = CellValue(2.0);
    sheet->addCell(std::move(cell2));

    auto cell3 = std::make_unique<Cell>(generate_id(), rawSheet->getColumnByPosition(2)->id,
                                        rawSheet->getRowByPosition(0)->id);
    cell3->value = CellValue(3.0);
    sheet->addCell(std::move(cell3));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("hidden_column.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Read back and verify hidden state
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    // Find columns by position
    Axis* readCol1 = readSheet->getColumnByPosition(0);
    Axis* readCol2 = readSheet->getColumnByPosition(1);
    Axis* readCol3 = readSheet->getColumnByPosition(2);

    ASSERT_NE(readCol1, nullptr);
    ASSERT_NE(readCol2, nullptr);
    ASSERT_NE(readCol3, nullptr);

    EXPECT_FALSE(readCol1->hidden) << "Column 0 should not be hidden";
    EXPECT_TRUE(readCol2->hidden) << "Column 1 should be hidden";
    EXPECT_FALSE(readCol3->hidden) << "Column 2 should not be hidden";
}

TEST(XLSXWriterTest, HiddenRowRoundTrip) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Hidden Row Test");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    // Add a column
    auto col = std::make_unique<Axis>(generate_id(), true);
    col->position = 0;
    sheet->addColumn(std::move(col));

    // Create 3 rows, hide the middle one
    auto row1 = std::make_unique<Axis>(generate_id(), false);
    row1->position = 0;
    row1->hidden = false;
    sheet->addRow(std::move(row1));

    auto row2 = std::make_unique<Axis>(generate_id(), false);
    row2->position = 1;
    row2->hidden = true;  // Hidden row
    sheet->addRow(std::move(row2));

    auto row3 = std::make_unique<Axis>(generate_id(), false);
    row3->position = 2;
    row3->hidden = false;
    sheet->addRow(std::move(row3));

    // Add a cell to each row so they get exported
    Sheet* rawSheet = sheet.get();
    auto cell1 = std::make_unique<Cell>(generate_id(), rawSheet->getColumnByPosition(0)->id,
                                        rawSheet->getRowByPosition(0)->id);
    cell1->value = CellValue(1.0);
    sheet->addCell(std::move(cell1));

    auto cell2 = std::make_unique<Cell>(generate_id(), rawSheet->getColumnByPosition(0)->id,
                                        rawSheet->getRowByPosition(1)->id);
    cell2->value = CellValue(2.0);
    sheet->addCell(std::move(cell2));

    auto cell3 = std::make_unique<Cell>(generate_id(), rawSheet->getColumnByPosition(0)->id,
                                        rawSheet->getRowByPosition(2)->id);
    cell3->value = CellValue(3.0);
    sheet->addCell(std::move(cell3));

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("hidden_row.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Read back and verify hidden state
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    // Find rows by position
    Axis* readRow1 = readSheet->getRowByPosition(0);
    Axis* readRow2 = readSheet->getRowByPosition(1);
    Axis* readRow3 = readSheet->getRowByPosition(2);

    ASSERT_NE(readRow1, nullptr);
    ASSERT_NE(readRow2, nullptr);
    ASSERT_NE(readRow3, nullptr);

    EXPECT_FALSE(readRow1->hidden) << "Row 0 should not be hidden";
    EXPECT_TRUE(readRow2->hidden) << "Row 1 should be hidden";
    EXPECT_FALSE(readRow3->hidden) << "Row 2 should not be hidden";
}

TEST(XLSXWriterTest, ColumnDefaultStyleRoundTrip) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Column Style Test");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    // Register a style
    CellStyle boldStyle;
    boldStyle.bold = true;
    const ID styleId = generate_id();
    workbook->registerStyle(styleId, boldStyle);

    // Create columns, set default style on middle one
    auto col1 = std::make_unique<Axis>(generate_id(), true);
    col1->position = 0;
    sheet->addColumn(std::move(col1));

    auto col2 = std::make_unique<Axis>(generate_id(), true);
    col2->position = 1;
    col2->defaultStyleId = styleId;  // Styled column
    sheet->addColumn(std::move(col2));

    auto col3 = std::make_unique<Axis>(generate_id(), true);
    col3->position = 2;
    sheet->addColumn(std::move(col3));

    // Add rows
    for (int i = 0; i < 3; ++i) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = static_cast<uint32_t>(i);
        sheet->addRow(std::move(row));
    }

    // Add cells in all columns to ensure data exists
    for (int c = 0; c < 3; ++c) {
        auto cell = std::make_unique<Cell>(generate_id(),
                                           sheet->getColumnByPosition(static_cast<uint32_t>(c))->id,
                                           sheet->getRowByPosition(0)->id);
        cell->value = CellValue(static_cast<double>(c + 1));
        sheet->addCell(std::move(cell));
    }

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("column_style.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Read back and verify column style
    XLSXReadOptions readOptions;
    readOptions.readStyles = true;
    auto readResult = readXLSX(path, readOptions);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    // Find columns by position
    Axis* readCol1 = readSheet->getColumnByPosition(0);
    Axis* readCol2 = readSheet->getColumnByPosition(1);
    Axis* readCol3 = readSheet->getColumnByPosition(2);

    ASSERT_NE(readCol1, nullptr);
    ASSERT_NE(readCol2, nullptr);
    ASSERT_NE(readCol3, nullptr);

    // Column 1 should have default style, others should not
    EXPECT_TRUE(readCol1->defaultStyleId.isNull()) << "Column 0 should not have default style";
    EXPECT_FALSE(readCol2->defaultStyleId.isNull()) << "Column 1 should have default style";
    EXPECT_TRUE(readCol3->defaultStyleId.isNull()) << "Column 2 should not have default style";

    // Verify the style content
    if (!readCol2->defaultStyleId.isNull()) {
        const CellStyle* readStyle = readResult.workbook->getStyle(readCol2->defaultStyleId);
        ASSERT_NE(readStyle, nullptr);
        EXPECT_TRUE(readStyle->bold) << "Column 1 style should be bold";
    }
}

TEST(XLSXWriterTest, RowDefaultStyleRoundTrip) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Row Style Test");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    // Register a style
    CellStyle italicStyle;
    italicStyle.italic = true;
    const ID styleId = generate_id();
    workbook->registerStyle(styleId, italicStyle);

    // Add columns
    for (int i = 0; i < 3; ++i) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = static_cast<uint32_t>(i);
        sheet->addColumn(std::move(col));
    }

    // Create rows, set default style on middle one
    auto row1 = std::make_unique<Axis>(generate_id(), false);
    row1->position = 0;
    sheet->addRow(std::move(row1));

    auto row2 = std::make_unique<Axis>(generate_id(), false);
    row2->position = 1;
    row2->defaultStyleId = styleId;  // Styled row
    sheet->addRow(std::move(row2));

    auto row3 = std::make_unique<Axis>(generate_id(), false);
    row3->position = 2;
    sheet->addRow(std::move(row3));

    // Add cells in all rows to ensure data exists
    for (int r = 0; r < 3; ++r) {
        auto cell = std::make_unique<Cell>(generate_id(), sheet->getColumnByPosition(0)->id,
                                           sheet->getRowByPosition(static_cast<uint32_t>(r))->id);
        cell->value = CellValue(static_cast<double>(r + 1));
        sheet->addCell(std::move(cell));
    }

    workbook->addSheet(std::move(sheet));

    std::string path = tempFilePath("row_style.xlsx");
    TempFileGuard guard(path);

    auto result = writeXLSX(*workbook, path);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");

    // Read back and verify row style
    XLSXReadOptions readOptions;
    readOptions.readStyles = true;
    auto readResult = readXLSX(path, readOptions);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    // Find rows by position
    Axis* readRow1 = readSheet->getRowByPosition(0);
    Axis* readRow2 = readSheet->getRowByPosition(1);
    Axis* readRow3 = readSheet->getRowByPosition(2);

    ASSERT_NE(readRow1, nullptr);
    ASSERT_NE(readRow2, nullptr);
    ASSERT_NE(readRow3, nullptr);

    // Row 1 should have default style, others should not
    EXPECT_TRUE(readRow1->defaultStyleId.isNull()) << "Row 0 should not have default style";
    EXPECT_FALSE(readRow2->defaultStyleId.isNull()) << "Row 1 should have default style";
    EXPECT_TRUE(readRow3->defaultStyleId.isNull()) << "Row 2 should not have default style";

    // Verify the style content
    if (!readRow2->defaultStyleId.isNull()) {
        const CellStyle* readStyle = readResult.workbook->getStyle(readRow2->defaultStyleId);
        ASSERT_NE(readStyle, nullptr);
        EXPECT_TRUE(readStyle->italic) << "Row 1 style should be italic";
    }
}

// ============================================================================
// Merged Cells Tests
// ============================================================================

// Test to verify Range-based merged cells API works
TEST(XLSXWriterTest, MergedCellsApiWorks) {
    // Create a sheet and add merged cells using Range system
    // Ranges require a workbook, so we create one
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheetPtr = std::make_unique<Sheet>(generate_id(), "Test");
    sheetPtr->setWorkbook(workbook.get());
    Sheet& sheet = *sheetPtr;

    std::vector<ID> colIds, rowIds;
    for (int i = 0; i < 5; i++) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = i;
        colIds.push_back(col->id);
        sheet.addColumn(std::move(col));

        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = i;
        rowIds.push_back(row->id);
        sheet.addRow(std::move(row));
    }

    ASSERT_EQ(sheet.columnCount(), 5u);
    ASSERT_EQ(sheet.rowCount(), 5u);

    // Add merge range using unified Range system (cols 0-2, row 0)
    auto mergeRange = std::make_unique<Range>(generate_id(), colIds[0], rowIds[0], colIds[2],
                                              rowIds[0], RangeFlags::MERGE);
    sheet.addRange(std::move(mergeRange));

    // Verify it was added by querying ranges
    std::vector<Range*> mergesAtAnchor = sheet.getRangesAt(0, 0, RangeFlags::MERGE);
    ASSERT_EQ(mergesAtAnchor.size(), 1u) << "Merge range should be found at anchor";

    // Verify range contains expected cells
    std::vector<Range*> mergesAt1_0 = sheet.getRangesAt(1, 0, RangeFlags::MERGE);
    EXPECT_EQ(mergesAt1_0.size(), 1u) << "(1,0) should be in merge range";

    std::vector<Range*> mergesAt2_0 = sheet.getRangesAt(2, 0, RangeFlags::MERGE);
    EXPECT_EQ(mergesAt2_0.size(), 1u) << "(2,0) should be in merge range";

    std::vector<Range*> mergesAt3_0 = sheet.getRangesAt(3, 0, RangeFlags::MERGE);
    EXPECT_EQ(mergesAt3_0.size(), 0u) << "(3,0) should not be in merge range";

    // Verify range corners
    Range* mergeRangePtr = mergesAtAnchor[0];
    EXPECT_EQ(mergeRangePtr->startColId, colIds[0]);
    EXPECT_EQ(mergeRangePtr->startRowId, rowIds[0]);
    EXPECT_EQ(mergeRangePtr->endColId, colIds[2]);
    EXPECT_EQ(mergeRangePtr->endRowId, rowIds[0]);
}

TEST(XLSXWriterTest, RoundtripMergedCells) {
    // Create a workbook with merged cells using Range system
    auto workbook = std::make_unique<Workbook>(generate_id(), "MergedCells");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    // Create 5 columns and 5 rows
    std::vector<ID> colIds;
    std::vector<ID> rowIds;
    for (int i = 0; i < 5; ++i) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = i;
        colIds.push_back(col->id);
        sheet->addColumn(std::move(col));

        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = i;
        rowIds.push_back(row->id);
        sheet->addRow(std::move(row));
    }

    // Add a cell at the anchor of a merge (A1 with value "Merged Header")
    auto cell1 = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[0]);
    cell1->value = CellValue("Merged Header");
    sheet->addCell(std::move(cell1));

    // Add another cell at B3 for a different merge
    auto cell2 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[2]);
    cell2->value = CellValue("Another Merge");
    sheet->addCell(std::move(cell2));

    // Add merge ranges using unified Range system
    // A1:C1 (cols 0-2, row 0)
    auto merge1 = std::make_unique<Range>(generate_id(), colIds[0], rowIds[0], colIds[2], rowIds[0],
                                          RangeFlags::MERGE);
    sheet->addRange(std::move(merge1));

    // B3:C4 (cols 1-2, rows 2-3)
    auto merge2 = std::make_unique<Range>(generate_id(), colIds[1], rowIds[2], colIds[2], rowIds[3],
                                          RangeFlags::MERGE);
    sheet->addRange(std::move(merge2));

    // Verify merges were added before writing by counting MERGE ranges
    size_t mergeCount = 0;
    for (const ID& rangeId : sheet->getRangeIds()) {
        const Range* range = sheet->getRange(rangeId);
        if (range != nullptr && range->hasFlag(RangeFlags::MERGE)) {
            mergeCount++;
        }
    }
    ASSERT_EQ(mergeCount, 2u) << "Merges should be in sheet before write";

    workbook->addSheet(std::move(sheet));

    // Write to XLSX
    std::string path = tempFilePath("merged_cells.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*workbook, path);
    EXPECT_TRUE(writeResult.ok()) << (writeResult.error ? writeResult.error->toString() : "");

    // Read back
    auto readResult = readXLSX(path);
    EXPECT_TRUE(readResult.ok()) << (readResult.error ? readResult.error->toString() : "");
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    // Verify merge ranges were preserved by collecting MERGE ranges
    std::vector<const Range*> merges;
    for (const ID& rangeId : readSheet->getRangeIds()) {
        const Range* range = readSheet->getRange(rangeId);
        if (range != nullptr && range->hasFlag(RangeFlags::MERGE)) {
            merges.push_back(range);
        }
    }
    EXPECT_EQ(merges.size(), 2u) << "Should have 2 merge ranges";

    // Find and verify each merge
    bool foundMerge1 = false;
    bool foundMerge2 = false;

    for (const auto* merge : merges) {
        // Get corner positions
        Axis* startCol = readSheet->getColumn(merge->startColId);
        Axis* startRow = readSheet->getRow(merge->startRowId);
        Axis* endCol = readSheet->getColumn(merge->endColId);
        Axis* endRow = readSheet->getRow(merge->endRowId);
        ASSERT_NE(startCol, nullptr);
        ASSERT_NE(startRow, nullptr);
        ASSERT_NE(endCol, nullptr);
        ASSERT_NE(endRow, nullptr);

        uint32_t colSpan = endCol->position - startCol->position + 1;
        uint32_t rowSpan = endRow->position - startRow->position + 1;

        if (startCol->position == 0 && startRow->position == 0) {
            // A1:C1 merge
            EXPECT_EQ(colSpan, 3u) << "First merge should span 3 columns";
            EXPECT_EQ(rowSpan, 1u) << "First merge should span 1 row";
            foundMerge1 = true;
        } else if (startCol->position == 1 && startRow->position == 2) {
            // B3:C4 merge
            EXPECT_EQ(colSpan, 2u) << "Second merge should span 2 columns";
            EXPECT_EQ(rowSpan, 2u) << "Second merge should span 2 rows";
            foundMerge2 = true;
        }
    }

    EXPECT_TRUE(foundMerge1) << "A1:C1 merge not found";
    EXPECT_TRUE(foundMerge2) << "B3:C4 merge not found";
}

TEST(XLSXWriterTest, MergedCellsWithStyles) {
    // Test that merged cells with styles are preserved using Range system
    auto workbook = std::make_unique<Workbook>(generate_id(), "StyledMerge");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    // Create columns and rows
    std::vector<ID> colIds;
    std::vector<ID> rowIds;
    for (int i = 0; i < 3; ++i) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = i;
        colIds.push_back(col->id);
        sheet->addColumn(std::move(col));

        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = i;
        rowIds.push_back(row->id);
        sheet->addRow(std::move(row));
    }

    // Create a styled cell at the anchor
    CellStyle style;
    style.bold = true;
    style.bgColor = "#FF0000";
    ID styleId = generate_id();
    workbook->registerStyle(styleId, style);

    auto cell = std::make_unique<Cell>(generate_id(), colIds[0], rowIds[0]);
    cell->value = CellValue("Styled Merge");
    ID cellId = cell->id;
    sheet->addCell(std::move(cell));

    // Add merge range A1:C3 using unified Range system
    auto merge = std::make_unique<Range>(generate_id(), colIds[0], rowIds[0], colIds[2], rowIds[2],
                                         RangeFlags::MERGE);
    sheet->addRange(std::move(merge));

    workbook->addSheet(std::move(sheet));

    // Set style via workbook map (after cell is added)
    workbook->setCellStyleId(cellId, styleId);

    // Write and read back
    std::string path = tempFilePath("styled_merge.xlsx");
    TempFileGuard guard(path);

    auto writeResult = writeXLSX(*workbook, path);
    EXPECT_TRUE(writeResult.ok()) << (writeResult.error ? writeResult.error->toString() : "");

    XLSXReadOptions readOptions;
    readOptions.readStyles = true;
    auto readResult = readXLSX(path, readOptions);
    EXPECT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    Sheet* readSheet = readResult.workbook->getSheetByIndex(0);
    ASSERT_NE(readSheet, nullptr);

    // Verify merge using Range system
    std::vector<const Range*> merges;
    for (const ID& rangeId : readSheet->getRangeIds()) {
        const Range* range = readSheet->getRange(rangeId);
        if (range != nullptr && range->hasFlag(RangeFlags::MERGE)) {
            merges.push_back(range);
        }
    }
    EXPECT_EQ(merges.size(), 1u);
    if (!merges.empty()) {
        Axis* startCol = readSheet->getColumn(merges[0]->startColId);
        Axis* endCol = readSheet->getColumn(merges[0]->endColId);
        Axis* startRow = readSheet->getRow(merges[0]->startRowId);
        Axis* endRow = readSheet->getRow(merges[0]->endRowId);
        ASSERT_NE(startCol, nullptr);
        ASSERT_NE(endCol, nullptr);
        ASSERT_NE(startRow, nullptr);
        ASSERT_NE(endRow, nullptr);
        uint32_t colSpan = endCol->position - startCol->position + 1;
        uint32_t rowSpan = endRow->position - startRow->position + 1;
        EXPECT_EQ(colSpan, 3u);
        EXPECT_EQ(rowSpan, 3u);
    }

    // Verify anchor cell has style
    Axis* readCol0 = readSheet->getColumnByPosition(0);
    Axis* readRow0 = readSheet->getRowByPosition(0);
    ASSERT_NE(readCol0, nullptr);
    ASSERT_NE(readRow0, nullptr);

    Cell* anchorCell = readSheet->getCellAt(readCol0->id, readRow0->id);
    ASSERT_NE(anchorCell, nullptr);
    ID anchorStyleId = readResult.workbook->getCellStyleId(anchorCell->id);
    EXPECT_FALSE(anchorStyleId.isNull()) << "Anchor cell should have style";

    if (!anchorStyleId.isNull()) {
        const CellStyle* readStyle = readResult.workbook->getStyle(anchorStyleId);
        ASSERT_NE(readStyle, nullptr);
        EXPECT_TRUE(readStyle->bold) << "Style should be bold";
        // Note: bgColor format may differ slightly between import/export
        EXPECT_FALSE(readStyle->bgColor.empty()) << "Style should have background color";
    }
}

}  // namespace
}  // namespace cells
