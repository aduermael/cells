#include "core/cells/csv_writer.h"

#include <string>

#include "core/cells/csv_reader.h"
#include "core/cells/id.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// Helper to create a simple sheet with data
std::unique_ptr<Sheet> createSimpleSheet(Workbook* workbook,
                                         const std::vector<std::string>& colNames,
                                         const std::vector<std::vector<std::string>>& data) {
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook);  // Set workbook early so cells get stored properly

    // Create columns
    std::vector<ID> colIds;
    for (size_t c = 0; c < colNames.size(); c++) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->name = colNames[c];
        col->position = static_cast<uint32_t>(c);
        colIds.push_back(col->id);
        sheet->addColumn(std::move(col));
    }

    // Create rows and cells
    std::vector<ID> rowIds;
    for (size_t r = 0; r < data.size(); r++) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = static_cast<uint32_t>(r);
        rowIds.push_back(row->id);
        sheet->addRow(std::move(row));

        // Add cells for this row
        for (size_t c = 0; c < data[r].size() && c < colIds.size(); c++) {
            if (!data[r][c].empty()) {
                auto cell = std::make_unique<Cell>(generate_id(), colIds[c], rowIds[r]);
                cell->value = CellValue(data[r][c]);
                sheet->addCell(std::move(cell));
            }
        }
    }

    return sheet;
}

// Helper to create a sheet with numeric data
std::unique_ptr<Sheet> createNumericSheet(Workbook* workbook,
                                          const std::vector<std::string>& colNames,
                                          const std::vector<std::vector<double>>& data) {
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");
    sheet->setWorkbook(workbook);  // Set workbook early so cells get stored properly

    // Create columns
    std::vector<ID> colIds;
    for (size_t c = 0; c < colNames.size(); c++) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->name = colNames[c];
        col->position = static_cast<uint32_t>(c);
        colIds.push_back(col->id);
        sheet->addColumn(std::move(col));
    }

    // Create rows and cells
    std::vector<ID> rowIds;
    for (size_t r = 0; r < data.size(); r++) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = static_cast<uint32_t>(r);
        rowIds.push_back(row->id);
        sheet->addRow(std::move(row));

        // Add cells for this row
        for (size_t c = 0; c < data[r].size() && c < colIds.size(); c++) {
            auto cell = std::make_unique<Cell>(generate_id(), colIds[c], rowIds[r]);
            cell->value = CellValue(data[r][c]);
            sheet->addCell(std::move(cell));
        }
    }

    return sheet;
}

// --- Basic Writing Tests ---

TEST(CSVWriterTest, WriteEmptySheet) {
    auto sheet = std::make_unique<Sheet>(generate_id(), "Empty");
    CSVWriteResult result = writeCSV(*sheet);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.output, "");
}

TEST(CSVWriterTest, WriteHeaderOnly) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = createSimpleSheet(workbook.get(), {"Name", "Age", "City"}, {});
    workbook->addSheet(std::move(sheet));
    CSVWriteResult result = writeCSV(*workbook);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.output, "Name,Age,City\r\n");
}

TEST(CSVWriterTest, WriteSimpleData) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = createSimpleSheet(workbook.get(), {"Name", "Age", "City"},
                                   {{"Alice", "30", "NYC"}, {"Bob", "25", "LA"}});
    workbook->addSheet(std::move(sheet));

    CSVWriteResult result = writeCSV(*workbook);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.output, "Name,Age,City\r\nAlice,30,NYC\r\nBob,25,LA\r\n");
}

TEST(CSVWriterTest, WriteNumericData) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet =
        createNumericSheet(workbook.get(), {"A", "B", "C"}, {{1.0, 2.0, 3.0}, {4.5, 5.5, 6.5}});
    workbook->addSheet(std::move(sheet));

    CSVWriteResult result = writeCSV(*workbook);
    EXPECT_TRUE(result.ok());
    // Numbers should be written as-is
    EXPECT_NE(result.output.find("1"), std::string::npos);
    EXPECT_NE(result.output.find("4.5"), std::string::npos);
}

TEST(CSVWriterTest, WriteWithoutHeader) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = createSimpleSheet(workbook.get(), {"Name", "Age"}, {{"Alice", "30"}});
    workbook->addSheet(std::move(sheet));

    CSVWriteOptions options;
    options.includeHeader = false;

    CSVWriteResult result = writeCSV(*workbook, options);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.output, "Alice,30\r\n");
}

// --- Escaping Tests (RFC 4180) ---

TEST(CSVWriterTest, EscapeFieldWithComma) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet =
        createSimpleSheet(workbook.get(), {"Name", "Description"}, {{"Test", "Hello, World"}});
    workbook->addSheet(std::move(sheet));

    CSVWriteResult result = writeCSV(*workbook);
    EXPECT_TRUE(result.ok());
    // Field with comma should be quoted
    EXPECT_NE(result.output.find("\"Hello, World\""), std::string::npos);
}

TEST(CSVWriterTest, EscapeFieldWithQuotes) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = createSimpleSheet(workbook.get(), {"Name", "Quote"}, {{"Test", "Say \"Hello\""}});
    workbook->addSheet(std::move(sheet));

    CSVWriteResult result = writeCSV(*workbook);
    EXPECT_TRUE(result.ok());
    // Quotes should be doubled and field should be quoted
    EXPECT_NE(result.output.find("\"Say \"\"Hello\"\"\""), std::string::npos);
}

TEST(CSVWriterTest, EscapeFieldWithNewline) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet =
        createSimpleSheet(workbook.get(), {"Name", "Multiline"}, {{"Test", "Line1\nLine2"}});
    workbook->addSheet(std::move(sheet));

    CSVWriteResult result = writeCSV(*workbook);
    EXPECT_TRUE(result.ok());
    // Field with newline should be quoted
    EXPECT_NE(result.output.find("\"Line1\nLine2\""), std::string::npos);
}

TEST(CSVWriterTest, EscapeFieldWithCR) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = createSimpleSheet(workbook.get(), {"Name", "Text"}, {{"Test", "Line1\rLine2"}});
    workbook->addSheet(std::move(sheet));

    CSVWriteResult result = writeCSV(*workbook);
    EXPECT_TRUE(result.ok());
    // Field with CR should be quoted
    EXPECT_NE(result.output.find("\"Line1\rLine2\""), std::string::npos);
}

TEST(CSVWriterTest, NoEscapeNormalField) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = createSimpleSheet(workbook.get(), {"Name", "Value"}, {{"Hello", "World"}});
    workbook->addSheet(std::move(sheet));

    CSVWriteResult result = writeCSV(*workbook);
    EXPECT_TRUE(result.ok());
    // Normal fields should not be quoted
    EXPECT_EQ(result.output.find("\"Hello\""), std::string::npos);
    EXPECT_EQ(result.output.find("\"World\""), std::string::npos);
}

// --- Delimiter Tests ---

TEST(CSVWriterTest, WriteTabDelimited) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = createSimpleSheet(workbook.get(), {"Name", "Age"}, {{"Alice", "30"}});
    workbook->addSheet(std::move(sheet));

    CSVWriteOptions options;
    options.delimiter = '\t';

    CSVWriteResult result = writeCSV(*workbook, options);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.output, "Name\tAge\r\nAlice\t30\r\n");
}

TEST(CSVWriterTest, WriteSemicolonDelimited) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = createSimpleSheet(workbook.get(), {"Name", "Age"}, {{"Alice", "30"}});
    workbook->addSheet(std::move(sheet));

    CSVWriteOptions options;
    options.delimiter = ';';

    CSVWriteResult result = writeCSV(*workbook, options);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.output, "Name;Age\r\nAlice;30\r\n");
}

TEST(CSVWriterTest, EscapeTabInTabDelimited) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = createSimpleSheet(workbook.get(), {"Name", "Value"}, {{"Test", "Has\tTab"}});
    workbook->addSheet(std::move(sheet));

    CSVWriteOptions options;
    options.delimiter = '\t';

    CSVWriteResult result = writeCSV(*workbook, options);
    EXPECT_TRUE(result.ok());
    // Field with tab should be quoted in TSV
    EXPECT_NE(result.output.find("\"Has\tTab\""), std::string::npos);
}

// --- Line Ending Tests ---

TEST(CSVWriterTest, WriteLFLineEndings) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = createSimpleSheet(workbook.get(), {"A", "B"}, {{"1", "2"}});
    workbook->addSheet(std::move(sheet));

    CSVWriteOptions options;
    options.useCRLF = false;

    CSVWriteResult result = writeCSV(*workbook, options);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.output, "A,B\n1,2\n");
}

TEST(CSVWriterTest, WriteCRLFLineEndings) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = createSimpleSheet(workbook.get(), {"A", "B"}, {{"1", "2"}});
    workbook->addSheet(std::move(sheet));

    CSVWriteOptions options;
    options.useCRLF = true;

    CSVWriteResult result = writeCSV(*workbook, options);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.output, "A,B\r\n1,2\r\n");
}

// --- Empty Cell Tests ---

TEST(CSVWriterTest, WriteEmptyCells) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet =
        createSimpleSheet(workbook.get(), {"A", "B", "C"}, {{"1", "", "3"}, {"", "2", ""}});
    workbook->addSheet(std::move(sheet));

    CSVWriteResult result = writeCSV(*workbook);
    EXPECT_TRUE(result.ok());
    // Empty cells should produce empty fields
    EXPECT_EQ(result.output, "A,B,C\r\n1,,3\r\n,2,\r\n");
}

// --- Workbook Tests ---

TEST(CSVWriterTest, WriteWorkbook) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = createSimpleSheet(workbook.get(), {"A"}, {{"1"}});
    workbook->addSheet(std::move(sheet));

    CSVWriteResult result = writeCSV(*workbook);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.output, "A\r\n1\r\n");
}

TEST(CSVWriterTest, WriteEmptyWorkbookReturnsError) {
    Workbook workbook(generate_id(), "Empty");

    CSVWriteResult result = writeCSV(workbook);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
    EXPECT_NE(result.error->message.find("no sheets"), std::string::npos);
}

// --- Boolean Tests ---

TEST(CSVWriterTest, WriteBooleanValues) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Booleans");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    // Create column
    auto col = std::make_unique<Axis>(generate_id(), true);
    col->name = "Active";
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    // Create rows with boolean cells
    for (int i = 0; i < 2; i++) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = static_cast<uint32_t>(i);
        ID rowId = row->id;
        sheet->addRow(std::move(row));

        auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
        cell->value = CellValue(i == 0);  // true, false
        sheet->addCell(std::move(cell));
    }

    workbook->addSheet(std::move(sheet));
    CSVWriteResult result = writeCSV(*workbook);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.output, "Active\r\ntrue\r\nfalse\r\n");
}

// --- Error Value Tests ---

TEST(CSVWriterTest, WriteErrorValues) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Errors");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    // Create column
    auto col = std::make_unique<Axis>(generate_id(), true);
    col->name = "Value";
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    // Create row with error cell
    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = 0;
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue(CellError::DIV);
    sheet->addCell(std::move(cell));

    workbook->addSheet(std::move(sheet));
    CSVWriteResult result = writeCSV(*workbook);
    EXPECT_TRUE(result.ok());
    EXPECT_NE(result.output.find("#DIV/0!"), std::string::npos);
}

// --- Generated Column Names ---

TEST(CSVWriterTest, GenerateColumnNamesWhenEmpty) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Test");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    // Create columns without names
    std::vector<ID> colIds;
    for (int c = 0; c < 3; c++) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = static_cast<uint32_t>(c);
        // Leave name empty
        colIds.push_back(col->id);
        sheet->addColumn(std::move(col));
    }

    // Create one row
    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = 0;
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    // Add cells
    for (size_t c = 0; c < 3; c++) {
        auto cell = std::make_unique<Cell>(generate_id(), colIds[c], rowId);
        cell->value = CellValue(std::to_string(c + 1));
        sheet->addCell(std::move(cell));
    }

    workbook->addSheet(std::move(sheet));
    CSVWriteResult result = writeCSV(*workbook);
    EXPECT_TRUE(result.ok());
    // Should generate A, B, C as column names
    EXPECT_EQ(result.output, "A,B,C\r\n1,2,3\r\n");
}

// --- Roundtrip Tests ---

TEST(CSVRoundtripTest, SimpleRoundtrip) {
    const std::string original = "Name,Age,City\r\nAlice,30,NYC\r\nBob,25,LA\r\n";

    // Read
    CSVReadResult readResult = readCSV(original);
    ASSERT_TRUE(readResult.ok());
    ASSERT_NE(readResult.workbook, nullptr);

    // Write
    CSVWriteResult writeResult = writeCSV(*readResult.workbook);
    ASSERT_TRUE(writeResult.ok());

    // Compare (note: may differ in line endings, so normalize)
    std::string normalized = original;
    // Original already uses CRLF which is default for writer
    EXPECT_EQ(writeResult.output, normalized);
}

TEST(CSVRoundtripTest, QuotedFieldsRoundtrip) {
    const std::string original = "Name,Desc\r\n\"Hello, World\",Test\r\n";

    CSVReadResult readResult = readCSV(original);
    ASSERT_TRUE(readResult.ok());

    CSVWriteResult writeResult = writeCSV(*readResult.workbook);
    ASSERT_TRUE(writeResult.ok());

    EXPECT_EQ(writeResult.output, original);
}

TEST(CSVRoundtripTest, EscapedQuotesRoundtrip) {
    const std::string original = "Name,Quote\r\n\"Say \"\"Hi\"\"\",Test\r\n";

    CSVReadResult readResult = readCSV(original);
    ASSERT_TRUE(readResult.ok());

    CSVWriteResult writeResult = writeCSV(*readResult.workbook);
    ASSERT_TRUE(writeResult.ok());

    EXPECT_EQ(writeResult.output, original);
}

TEST(CSVRoundtripTest, TabDelimitedRoundtrip) {
    const std::string original = "Name\tAge\r\nAlice\t30\r\n";

    CSVReadOptions readOpts;
    readOpts.delimiter = '\t';
    CSVReadResult readResult = readCSV(original, readOpts);
    ASSERT_TRUE(readResult.ok());

    CSVWriteOptions writeOpts;
    writeOpts.delimiter = '\t';
    CSVWriteResult writeResult = writeCSV(*readResult.workbook, writeOpts);
    ASSERT_TRUE(writeResult.ok());

    EXPECT_EQ(writeResult.output, original);
}

TEST(CSVRoundtripTest, MultilineFieldRoundtrip) {
    const std::string original = "Name,Text\r\n\"Line1\nLine2\",Test\r\n";

    CSVReadResult readResult = readCSV(original);
    ASSERT_TRUE(readResult.ok());

    CSVWriteResult writeResult = writeCSV(*readResult.workbook);
    ASSERT_TRUE(writeResult.ok());

    EXPECT_EQ(writeResult.output, original);
}

TEST(CSVRoundtripTest, NumericRoundtrip) {
    const std::string original = "Value\r\n42\r\n3.14\r\n-100\r\n";

    CSVReadResult readResult = readCSV(original);
    ASSERT_TRUE(readResult.ok());

    CSVWriteResult writeResult = writeCSV(*readResult.workbook);
    ASSERT_TRUE(writeResult.ok());

    // Numbers get detected and stored. 3.14 cannot be exactly represented in
    // IEEE 754 float, so it may output with more precision (3.1400000000000001).
    // We verify the values are numerically equivalent by parsing them back.
    CSVReadResult rereadResult = readCSV(writeResult.output);
    ASSERT_TRUE(rereadResult.ok());

    // Get the cells from both workbooks and compare numeric values
    auto* origSheet = readResult.workbook->getSheetByIndex(0);
    auto* rereadSheet = rereadResult.workbook->getSheetByIndex(0);
    ASSERT_NE(origSheet, nullptr);
    ASSERT_NE(rereadSheet, nullptr);

    // Verify all cells have equivalent numeric values
    for (const auto& cellId : origSheet->getCellIds()) {
        Cell* origCell = origSheet->getCell(cellId);
        // Find corresponding cell by position
        const Axis* col = origSheet->getColumn(origCell->colId);
        const Axis* row = origSheet->getRow(origCell->rowId);
        if (col && row) {
            const Axis* newCol = rereadSheet->getColumnByPosition(col->position);
            const Axis* newRow = rereadSheet->getRowByPosition(row->position);
            if (newCol && newRow) {
                Cell* newCell = rereadSheet->getCellAt(newCol->id, newRow->id);
                if (newCell && origCell->value.type == CellValueType::NUMBER) {
                    EXPECT_DOUBLE_EQ(origCell->value.asNumber(), newCell->value.asNumber());
                }
            }
        }
    }
}

TEST(CSVRoundtripTest, LargerDataRoundtrip) {
    // Create a larger CSV
    std::string original = "A,B,C,D,E\r\n";
    for (int i = 0; i < 100; i++) {
        original += std::to_string(i) + "," + std::to_string(i * 2) + "," + std::to_string(i * 3) +
                    "," + std::to_string(i * 4) + "," + std::to_string(i * 5) + "\r\n";
    }

    CSVReadResult readResult = readCSV(original);
    ASSERT_TRUE(readResult.ok());
    EXPECT_EQ(readResult.workbook->sheets[0]->rowCount(), 100u);

    CSVWriteResult writeResult = writeCSV(*readResult.workbook);
    ASSERT_TRUE(writeResult.ok());

    EXPECT_EQ(writeResult.output, original);
}

// --- Style Warning Tests ---

TEST(CSVWriterTest, NoStyleWarningForUnstyledSheet) {
    // Sheet with no styles should not produce warnings
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet =
        createSimpleSheet(workbook.get(), {"Name", "Value"}, {{"Alice", "100"}, {"Bob", "200"}});
    workbook->addSheet(std::move(sheet));

    CSVWriteResult result = writeCSV(*workbook);
    EXPECT_TRUE(result.ok());
    EXPECT_FALSE(result.stylesLost);
    EXPECT_TRUE(result.warnings.empty());
}

TEST(CSVWriterTest, StyleWarningForStyledSheet) {
    // Create a sheet with styled cells
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Styled");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    // Create column
    auto col = std::make_unique<Axis>(generate_id(), true);
    col->name = "Value";
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    // Create row
    auto row = std::make_unique<Axis>(generate_id(), false);
    row->position = 0;
    ID rowId = row->id;
    sheet->addRow(std::move(row));

    // Create styled cell
    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue("Bold Text");

    // Apply a style directly on entity (content-addressed)
    CellStyle boldStyle;
    boldStyle.bold = true;
    boldStyle.setDefined(DEFINED_BOLD);
    workbook->setEntityStyle(cell->id, StyleBuffer::fromCellStyle(boldStyle));
    cell->markHasStyle();

    sheet->addCell(std::move(cell));
    workbook->addSheet(std::move(sheet));

    // Export to CSV
    CSVWriteResult result = writeCSV(*workbook);
    EXPECT_TRUE(result.ok());

    // Should have style warning
    EXPECT_TRUE(result.stylesLost);
    EXPECT_FALSE(result.warnings.empty());
    EXPECT_NE(result.warnings[0].find("styles"), std::string::npos);
    EXPECT_NE(result.warnings[0].find("XLSX"), std::string::npos);

    // Data should still be correct
    EXPECT_NE(result.output.find("Bold Text"), std::string::npos);
}

TEST(CSVWriterTest, StyleWarningOnlyOnce) {
    // Multiple styled cells should only produce one warning
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    auto sheet = std::make_unique<Sheet>(generate_id(), "Styled");
    sheet->setWorkbook(workbook.get());  // Set workbook early so cells get stored properly

    // Create column
    auto col = std::make_unique<Axis>(generate_id(), true);
    col->name = "Value";
    col->position = 0;
    ID colId = col->id;
    sheet->addColumn(std::move(col));

    // Create style (content-addressed)
    CellStyle boldStyle;
    boldStyle.bold = true;
    boldStyle.setDefined(DEFINED_BOLD);
    const StyleBuffer boldStyleBuf = StyleBuffer::fromCellStyle(boldStyle);

    // Create multiple styled cells
    for (int i = 0; i < 5; i++) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = i;
        ID rowId = row->id;
        sheet->addRow(std::move(row));

        auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
        cell->value = CellValue("Row " + std::to_string(i));
        // Set style directly on entity
        ID cellId = cell->id;
        cell->markHasStyle();
        sheet->addCell(std::move(cell));
        workbook->setEntityStyle(cellId, boldStyleBuf);
    }

    workbook->addSheet(std::move(sheet));

    CSVWriteResult result = writeCSV(*workbook);
    EXPECT_TRUE(result.ok());
    EXPECT_TRUE(result.stylesLost);

    // Should only have one warning message
    EXPECT_EQ(result.warnings.size(), 1u);
}

}  // namespace
}  // namespace cells
