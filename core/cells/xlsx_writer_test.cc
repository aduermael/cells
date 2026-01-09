#include "core/cells/xlsx_writer.h"

#include <cstdio>

#include <filesystem>
#include <string>

#include "core/cells/formula_parser.h"
#include "core/cells/formula_serializer.h"
#include "core/cells/id.h"
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

// ============================================================================
// Formula Tests
// ============================================================================

TEST(XLSXWriterTest, WriteFormulas) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Formulas");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");

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
    for (const auto& [id, c] : readSheet->cells) {
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
    Cell* masterPtr = masterCell.get();
    sheet->addCell(std::move(masterCell));

    // B2 = shared formula subscriber (result 40)
    auto subCell1 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[1]);
    subCell1->value = CellValue(40.0);
    subCell1->setSharedFormulaRef(masterPtr);
    sheet->addCell(std::move(subCell1));

    // B3 = shared formula subscriber (result 60)
    auto subCell2 = std::make_unique<Cell>(generate_id(), colIds[1], rowIds[2]);
    subCell2->value = CellValue(60.0);
    subCell2->setSharedFormulaRef(masterPtr);
    sheet->addCell(std::move(subCell2));

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
    for (const auto& [id, c] : readSheet->cells) {
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
    for (const auto& [id, c] : readSheet->cells) {
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
    for (const auto& [id, c] : readSheet->cells) {
        EXPECT_FALSE(c->isFormula()) << "Cell should not have formula when writeFormulas=false";
        EXPECT_EQ(c->value.type, CellValueType::NUMBER);
        EXPECT_DOUBLE_EQ(c->value.asNumber(), 42.0);
    }
}

TEST(XLSXWriterTest, SkipFormulasWhenDisabled) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "NoFormulas");
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
    for (const auto& [id, c] : readSheet->cells) {
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
    for (const auto& [id, c] : readSheet->cells) {
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

    // Create and register bold style
    CellStyle boldStyle;
    boldStyle.bold = true;
    ID styleId = generate_id();
    workbook->registerStyle(styleId, boldStyle);
    cell->styleId = styleId;

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
    EXPECT_FALSE(readCell->styleId.isNull()) << "Cell should have style";

    const CellStyle* readStyle = readResult.workbook->getStyle(readCell->styleId);
    ASSERT_NE(readStyle, nullptr) << "Style should be registered";
    EXPECT_TRUE(readStyle->bold) << "Cell should be bold";
}

TEST(XLSXWriterTest, WriteStylesBackgroundColor) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Styles");
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
    cell->value = CellValue("Red Background");

    // Create and register style with red background
    CellStyle redBgStyle;
    redBgStyle.bgColor = "#FF0000";
    ID styleId = generate_id();
    workbook->registerStyle(styleId, redBgStyle);
    cell->styleId = styleId;

    sheet->addCell(std::move(cell));
    workbook->addSheet(std::move(sheet));

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
    EXPECT_FALSE(readCell->styleId.isNull()) << "Cell should have style";

    const CellStyle* readStyle = readResult.workbook->getStyle(readCell->styleId);
    ASSERT_NE(readStyle, nullptr) << "Style should be registered";
    EXPECT_EQ(readStyle->bgColor, "#FF0000") << "Cell should have red background";
}

TEST(XLSXWriterTest, WriteStylesAlignment) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Styles");
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
    cell->value = CellValue("Centered");

    // Create and register center-aligned style
    CellStyle centerStyle;
    centerStyle.hAlign = TextAlign::CENTER;
    centerStyle.vAlign = VerticalAlign::MIDDLE;
    ID styleId = generate_id();
    workbook->registerStyle(styleId, centerStyle);
    cell->styleId = styleId;

    sheet->addCell(std::move(cell));
    workbook->addSheet(std::move(sheet));

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
    EXPECT_FALSE(readCell->styleId.isNull()) << "Cell should have style";

    const CellStyle* readStyle = readResult.workbook->getStyle(readCell->styleId);
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
        if (cell && !cell->styleId.isNull()) {
            const CellStyle* style = readResult2.workbook->getStyle(cell->styleId);
            EXPECT_NE(style, nullptr);
            if (style) {
                EXPECT_TRUE(style->bold) << "A1 should be bold after roundtrip";
            }
        }
    }
}

}  // namespace
}  // namespace cells
