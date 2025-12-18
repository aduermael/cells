#include "core/cells/csv_writer.h"

#include <string>

#include "core/cells/csv_reader.h"
#include "core/cells/id.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// Helper to create a simple sheet with data
std::unique_ptr<Sheet> createSimpleSheet(const std::vector<std::string>& colNames,
                                         const std::vector<std::vector<std::string>>& data) {
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");

    // Create columns
    std::vector<ID> colIds;
    ID prevColId;
    for (size_t c = 0; c < colNames.size(); c++) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->name = colNames[c];
        col->prevId = prevColId;

        if (!prevColId.isNull()) {
            Axis* prevCol = sheet->getColumn(prevColId);
            if (prevCol) {
                prevCol->nextId = col->id;
            }
        }

        if (c == 0) {
            sheet->firstCol = col->id;
        }
        if (c == colNames.size() - 1) {
            sheet->lastCol = col->id;
        }

        colIds.push_back(col->id);
        prevColId = col->id;
        sheet->addColumn(std::move(col));
    }

    // Create rows and cells
    std::vector<ID> rowIds;
    ID prevRowId;
    for (size_t r = 0; r < data.size(); r++) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->prevId = prevRowId;

        if (!prevRowId.isNull()) {
            Axis* prevRow = sheet->getRow(prevRowId);
            if (prevRow) {
                prevRow->nextId = row->id;
            }
        }

        if (r == 0) {
            sheet->firstRow = row->id;
        }
        if (r == data.size() - 1) {
            sheet->lastRow = row->id;
        }

        rowIds.push_back(row->id);
        prevRowId = row->id;
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
std::unique_ptr<Sheet> createNumericSheet(const std::vector<std::string>& colNames,
                                          const std::vector<std::vector<double>>& data) {
    auto sheet = std::make_unique<Sheet>(generate_id(), "Sheet1");

    // Create columns
    std::vector<ID> colIds;
    ID prevColId;
    for (size_t c = 0; c < colNames.size(); c++) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->name = colNames[c];
        col->prevId = prevColId;

        if (!prevColId.isNull()) {
            sheet->getColumn(prevColId)->nextId = col->id;
        }

        if (c == 0) {
            sheet->firstCol = col->id;
        }
        if (c == colNames.size() - 1) {
            sheet->lastCol = col->id;
        }

        colIds.push_back(col->id);
        prevColId = col->id;
        sheet->addColumn(std::move(col));
    }

    // Create rows and cells
    std::vector<ID> rowIds;
    ID prevRowId;
    for (size_t r = 0; r < data.size(); r++) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->prevId = prevRowId;

        if (!prevRowId.isNull()) {
            sheet->getRow(prevRowId)->nextId = row->id;
        }

        if (r == 0) {
            sheet->firstRow = row->id;
        }
        if (r == data.size() - 1) {
            sheet->lastRow = row->id;
        }

        rowIds.push_back(row->id);
        prevRowId = row->id;
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
    auto sheet = createSimpleSheet({"Name", "Age", "City"}, {});
    CSVWriteResult result = writeCSV(*sheet);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.output, "Name,Age,City\r\n");
}

TEST(CSVWriterTest, WriteSimpleData) {
    auto sheet =
        createSimpleSheet({"Name", "Age", "City"}, {{"Alice", "30", "NYC"}, {"Bob", "25", "LA"}});

    CSVWriteResult result = writeCSV(*sheet);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.output, "Name,Age,City\r\nAlice,30,NYC\r\nBob,25,LA\r\n");
}

TEST(CSVWriterTest, WriteNumericData) {
    auto sheet = createNumericSheet({"A", "B", "C"}, {{1.0, 2.0, 3.0}, {4.5, 5.5, 6.5}});

    CSVWriteResult result = writeCSV(*sheet);
    EXPECT_TRUE(result.ok());
    // Numbers should be written as-is
    EXPECT_NE(result.output.find("1"), std::string::npos);
    EXPECT_NE(result.output.find("4.5"), std::string::npos);
}

TEST(CSVWriterTest, WriteWithoutHeader) {
    auto sheet = createSimpleSheet({"Name", "Age"}, {{"Alice", "30"}});

    CSVWriteOptions options;
    options.includeHeader = false;

    CSVWriteResult result = writeCSV(*sheet, options);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.output, "Alice,30\r\n");
}

// --- Escaping Tests (RFC 4180) ---

TEST(CSVWriterTest, EscapeFieldWithComma) {
    auto sheet = createSimpleSheet({"Name", "Description"}, {{"Test", "Hello, World"}});

    CSVWriteResult result = writeCSV(*sheet);
    EXPECT_TRUE(result.ok());
    // Field with comma should be quoted
    EXPECT_NE(result.output.find("\"Hello, World\""), std::string::npos);
}

TEST(CSVWriterTest, EscapeFieldWithQuotes) {
    auto sheet = createSimpleSheet({"Name", "Quote"}, {{"Test", "Say \"Hello\""}});

    CSVWriteResult result = writeCSV(*sheet);
    EXPECT_TRUE(result.ok());
    // Quotes should be doubled and field should be quoted
    EXPECT_NE(result.output.find("\"Say \"\"Hello\"\"\""), std::string::npos);
}

TEST(CSVWriterTest, EscapeFieldWithNewline) {
    auto sheet = createSimpleSheet({"Name", "Multiline"}, {{"Test", "Line1\nLine2"}});

    CSVWriteResult result = writeCSV(*sheet);
    EXPECT_TRUE(result.ok());
    // Field with newline should be quoted
    EXPECT_NE(result.output.find("\"Line1\nLine2\""), std::string::npos);
}

TEST(CSVWriterTest, EscapeFieldWithCR) {
    auto sheet = createSimpleSheet({"Name", "Text"}, {{"Test", "Line1\rLine2"}});

    CSVWriteResult result = writeCSV(*sheet);
    EXPECT_TRUE(result.ok());
    // Field with CR should be quoted
    EXPECT_NE(result.output.find("\"Line1\rLine2\""), std::string::npos);
}

TEST(CSVWriterTest, NoEscapeNormalField) {
    auto sheet = createSimpleSheet({"Name", "Value"}, {{"Hello", "World"}});

    CSVWriteResult result = writeCSV(*sheet);
    EXPECT_TRUE(result.ok());
    // Normal fields should not be quoted
    EXPECT_EQ(result.output.find("\"Hello\""), std::string::npos);
    EXPECT_EQ(result.output.find("\"World\""), std::string::npos);
}

// --- Delimiter Tests ---

TEST(CSVWriterTest, WriteTabDelimited) {
    auto sheet = createSimpleSheet({"Name", "Age"}, {{"Alice", "30"}});

    CSVWriteOptions options;
    options.delimiter = '\t';

    CSVWriteResult result = writeCSV(*sheet, options);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.output, "Name\tAge\r\nAlice\t30\r\n");
}

TEST(CSVWriterTest, WriteSemicolonDelimited) {
    auto sheet = createSimpleSheet({"Name", "Age"}, {{"Alice", "30"}});

    CSVWriteOptions options;
    options.delimiter = ';';

    CSVWriteResult result = writeCSV(*sheet, options);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.output, "Name;Age\r\nAlice;30\r\n");
}

TEST(CSVWriterTest, EscapeTabInTabDelimited) {
    auto sheet = createSimpleSheet({"Name", "Value"}, {{"Test", "Has\tTab"}});

    CSVWriteOptions options;
    options.delimiter = '\t';

    CSVWriteResult result = writeCSV(*sheet, options);
    EXPECT_TRUE(result.ok());
    // Field with tab should be quoted in TSV
    EXPECT_NE(result.output.find("\"Has\tTab\""), std::string::npos);
}

// --- Line Ending Tests ---

TEST(CSVWriterTest, WriteLFLineEndings) {
    auto sheet = createSimpleSheet({"A", "B"}, {{"1", "2"}});

    CSVWriteOptions options;
    options.useCRLF = false;

    CSVWriteResult result = writeCSV(*sheet, options);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.output, "A,B\n1,2\n");
}

TEST(CSVWriterTest, WriteCRLFLineEndings) {
    auto sheet = createSimpleSheet({"A", "B"}, {{"1", "2"}});

    CSVWriteOptions options;
    options.useCRLF = true;

    CSVWriteResult result = writeCSV(*sheet, options);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.output, "A,B\r\n1,2\r\n");
}

// --- Empty Cell Tests ---

TEST(CSVWriterTest, WriteEmptyCells) {
    auto sheet = createSimpleSheet({"A", "B", "C"}, {{"1", "", "3"}, {"", "2", ""}});

    CSVWriteResult result = writeCSV(*sheet);
    EXPECT_TRUE(result.ok());
    // Empty cells should produce empty fields
    EXPECT_EQ(result.output, "A,B,C\r\n1,,3\r\n,2,\r\n");
}

// --- Workbook Tests ---

TEST(CSVWriterTest, WriteWorkbook) {
    auto workbook = std::make_unique<Workbook>(generate_id(), "Test");
    workbook->addSheet(createSimpleSheet({"A"}, {{"1"}}));

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
    auto sheet = std::make_unique<Sheet>(generate_id(), "Booleans");

    // Create column
    auto col = std::make_unique<Axis>(generate_id(), true);
    col->name = "Active";
    ID colId = col->id;
    sheet->firstCol = colId;
    sheet->lastCol = colId;
    sheet->addColumn(std::move(col));

    // Create rows with boolean cells
    ID prevRowId;
    for (int i = 0; i < 2; i++) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->prevId = prevRowId;
        if (!prevRowId.isNull()) {
            sheet->getRow(prevRowId)->nextId = row->id;
        }
        ID rowId = row->id;

        if (i == 0) {
            sheet->firstRow = rowId;
        }
        if (i == 1) {
            sheet->lastRow = rowId;
        }
        prevRowId = rowId;
        sheet->addRow(std::move(row));

        auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
        cell->value = CellValue(i == 0);  // true, false
        sheet->addCell(std::move(cell));
    }

    CSVWriteResult result = writeCSV(*sheet);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.output, "Active\r\ntrue\r\nfalse\r\n");
}

// --- Error Value Tests ---

TEST(CSVWriterTest, WriteErrorValues) {
    auto sheet = std::make_unique<Sheet>(generate_id(), "Errors");

    // Create column
    auto col = std::make_unique<Axis>(generate_id(), true);
    col->name = "Value";
    ID colId = col->id;
    sheet->firstCol = colId;
    sheet->lastCol = colId;
    sheet->addColumn(std::move(col));

    // Create row with error cell
    auto row = std::make_unique<Axis>(generate_id(), false);
    ID rowId = row->id;
    sheet->firstRow = rowId;
    sheet->lastRow = rowId;
    sheet->addRow(std::move(row));

    auto cell = std::make_unique<Cell>(generate_id(), colId, rowId);
    cell->value = CellValue(CellError::DIV);
    sheet->addCell(std::move(cell));

    CSVWriteResult result = writeCSV(*sheet);
    EXPECT_TRUE(result.ok());
    EXPECT_NE(result.output.find("#DIV/0!"), std::string::npos);
}

// --- Generated Column Names ---

TEST(CSVWriterTest, GenerateColumnNamesWhenEmpty) {
    auto sheet = std::make_unique<Sheet>(generate_id(), "Test");

    // Create columns without names
    std::vector<ID> colIds;
    ID prevColId;
    for (int c = 0; c < 3; c++) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        // Leave name empty
        col->prevId = prevColId;
        if (!prevColId.isNull()) {
            sheet->getColumn(prevColId)->nextId = col->id;
        }
        if (c == 0) {
            sheet->firstCol = col->id;
        }
        if (c == 2) {
            sheet->lastCol = col->id;
        }
        colIds.push_back(col->id);
        prevColId = col->id;
        sheet->addColumn(std::move(col));
    }

    // Create one row
    auto row = std::make_unique<Axis>(generate_id(), false);
    ID rowId = row->id;
    sheet->firstRow = rowId;
    sheet->lastRow = rowId;
    sheet->addRow(std::move(row));

    // Add cells
    for (size_t c = 0; c < 3; c++) {
        auto cell = std::make_unique<Cell>(generate_id(), colIds[c], rowId);
        cell->value = CellValue(std::to_string(c + 1));
        sheet->addCell(std::move(cell));
    }

    CSVWriteResult result = writeCSV(*sheet);
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

    // Numbers get detected and stored, but output should match
    EXPECT_EQ(writeResult.output, original);
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

}  // namespace
}  // namespace cells
