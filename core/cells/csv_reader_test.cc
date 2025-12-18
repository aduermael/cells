#include "core/cells/csv_reader.h"

#include <fstream>
#include <sstream>
#include <string>

#include "gtest/gtest.h"

namespace cells {
namespace {

// Helper to read test files
std::string readTestFile(const std::string& filename) {
    const std::string path = "core/testdata/csv/" + filename;
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// --- Basic Parsing Tests ---

TEST(CSVReaderTest, ParseEmptyString) {
    CSVReadResult result = readCSV("");
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);
    EXPECT_EQ(result.workbook->sheetCount(), 1u);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->cellCount(), 0u);
}

TEST(CSVReaderTest, ParseSingleValue) {
    CSVReadResult result = readCSV("value");
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    // With header=true (default), single value is treated as header
    EXPECT_EQ(sheet->columnCount(), 1u);
    EXPECT_EQ(sheet->cellCount(), 0u);
}

TEST(CSVReaderTest, ParseSimpleCSV) {
    const std::string csv = "A,B,C\n1,2,3\n4,5,6\n";
    CSVReadResult result = readCSV(csv);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->columnCount(), 3u);
    EXPECT_EQ(sheet->rowCount(), 2u);
    EXPECT_EQ(sheet->cellCount(), 6u);
}

TEST(CSVReaderTest, ParseNoHeaderMode) {
    const std::string csv = "1,2,3\n4,5,6\n";
    CSVReadOptions options;
    options.hasHeader = false;

    CSVReadResult result = readCSV(csv, options);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->columnCount(), 3u);
    EXPECT_EQ(sheet->rowCount(), 2u);
    EXPECT_EQ(sheet->cellCount(), 6u);
}

// --- Quoted Fields Tests (RFC 4180) ---

TEST(CSVReaderTest, ParseQuotedField) {
    const std::string csv = "Name,Desc\n\"Hello\",World\n";
    CSVReadResult result = readCSV(csv);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->cellCount(), 2u);
}

TEST(CSVReaderTest, ParseFieldWithComma) {
    const std::string csv = "Name,Desc\n\"Hello, World\",Test\n";
    CSVReadResult result = readCSV(csv);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->columnCount(), 2u);
    EXPECT_EQ(sheet->cellCount(), 2u);
}

TEST(CSVReaderTest, ParseEscapedQuotes) {
    const std::string csv = "Name,Desc\n\"Say \"\"Hello\"\"\",Test\n";
    CSVReadResult result = readCSV(csv);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->cellCount(), 2u);
}

TEST(CSVReaderTest, ParseMultilineField) {
    const std::string csv = "Name,Desc\n\"Line1\nLine2\",Test\n";
    CSVReadResult result = readCSV(csv);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->columnCount(), 2u);
    EXPECT_EQ(sheet->rowCount(), 1u);
    EXPECT_EQ(sheet->cellCount(), 2u);
}

TEST(CSVReaderTest, UnterminatedQuoteReturnsError) {
    const std::string csv = "Name,Desc\n\"unterminated,Test\n";
    CSVReadResult result = readCSV(csv);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.error.has_value());
    EXPECT_NE(result.error->message.find("Unterminated"), std::string::npos);
}

// --- Delimiter Tests ---

TEST(CSVReaderTest, ParseTabDelimited) {
    const std::string tsv = "A\tB\tC\n1\t2\t3\n";
    CSVReadOptions options;
    options.delimiter = '\t';

    CSVReadResult result = readCSV(tsv, options);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->columnCount(), 3u);
    EXPECT_EQ(sheet->cellCount(), 3u);
}

TEST(CSVReaderTest, ParseSemicolonDelimited) {
    const std::string csv = "A;B;C\n1;2;3\n";
    CSVReadOptions options;
    options.delimiter = ';';

    CSVReadResult result = readCSV(csv, options);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->columnCount(), 3u);
    EXPECT_EQ(sheet->cellCount(), 3u);
}

// --- Type Detection Tests ---

TEST(CSVReaderTest, DetectNumbers) {
    const std::string csv = "Value\n42\n3.14\n-100\n";
    CSVReadResult result = readCSV(csv);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->cellCount(), 3u);

    // Check that values are detected as numbers
    for (const auto& [id, cell] : sheet->cells) {
        EXPECT_EQ(cell->value.type, CellValueType::NUMBER);
    }
}

TEST(CSVReaderTest, DetectStrings) {
    const std::string csv = "Value\nhello\nworld\n";
    CSVReadResult result = readCSV(csv);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    for (const auto& [id, cell] : sheet->cells) {
        EXPECT_EQ(cell->value.type, CellValueType::STRING);
    }
}

TEST(CSVReaderTest, DetectBooleans) {
    const std::string csv = "Value\ntrue\nfalse\nTRUE\nFALSE\n";
    CSVReadResult result = readCSV(csv);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->cellCount(), 4u);

    for (const auto& [id, cell] : sheet->cells) {
        EXPECT_EQ(cell->value.type, CellValueType::BOOLEAN);
    }
}

TEST(CSVReaderTest, DisableAutoDetect) {
    const std::string csv = "Value\n42\ntrue\n";
    CSVReadOptions options;
    options.autoDetectTypes = false;

    CSVReadResult result = readCSV(csv, options);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    for (const auto& [id, cell] : sheet->cells) {
        EXPECT_EQ(cell->value.type, CellValueType::STRING);
    }
}

// --- Line Ending Tests ---

TEST(CSVReaderTest, ParseCRLF) {
    const std::string csv = "A,B\r\n1,2\r\n3,4\r\n";
    CSVReadResult result = readCSV(csv);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->rowCount(), 2u);
}

TEST(CSVReaderTest, ParseLF) {
    const std::string csv = "A,B\n1,2\n3,4\n";
    CSVReadResult result = readCSV(csv);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->rowCount(), 2u);
}

TEST(CSVReaderTest, ParseCR) {
    const std::string csv = "A,B\r1,2\r3,4\r";
    CSVReadResult result = readCSV(csv);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->rowCount(), 2u);
}

// --- BOM Handling ---

TEST(CSVReaderTest, SkipUTF8BOM) {
    // UTF-8 BOM followed by simple CSV
    const std::string csv = "\xEF\xBB\xBF" "A,B\n1,2\n";
    CSVReadResult result = readCSV(csv);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->columnCount(), 2u);

    // First column should be named "A", not "\xEF\xBB\xBFA"
    Axis* firstCol = sheet->getColumn(sheet->firstCol);
    ASSERT_NE(firstCol, nullptr);
    EXPECT_EQ(firstCol->name, "A");
}

// --- Column Names from Header ---

TEST(CSVReaderTest, ColumnNamesFromHeader) {
    const std::string csv = "Name,Age,City\nAlice,30,NYC\n";
    CSVReadResult result = readCSV(csv);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->columnCount(), 3u);

    // Verify column names
    Axis* col1 = sheet->getColumn(sheet->firstCol);
    ASSERT_NE(col1, nullptr);
    EXPECT_EQ(col1->name, "Name");

    Axis* col2 = sheet->getColumn(col1->nextId);
    ASSERT_NE(col2, nullptr);
    EXPECT_EQ(col2->name, "Age");

    Axis* col3 = sheet->getColumn(col2->nextId);
    ASSERT_NE(col3, nullptr);
    EXPECT_EQ(col3->name, "City");
}

// --- Edge Cases ---

TEST(CSVReaderTest, EmptyFields) {
    const std::string csv = "A,B,C\n,2,\n1,,3\n";
    CSVReadResult result = readCSV(csv);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    // Empty fields are skipped, so we should have 3 cells (2, 1, 3 from data)
    EXPECT_EQ(sheet->cellCount(), 3u);
}

TEST(CSVReaderTest, JaggedRows) {
    // Rows with different number of fields
    const std::string csv = "A,B,C\n1\n1,2\n1,2,3,4\n";
    CSVReadResult result = readCSV(csv);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    // Should handle jagged rows by using max columns
    EXPECT_EQ(sheet->columnCount(), 4u);  // Max is 4 from last row
    EXPECT_EQ(sheet->rowCount(), 3u);
}

TEST(CSVReaderTest, OnlyHeader) {
    const std::string csv = "A,B,C\n";
    CSVReadResult result = readCSV(csv);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->columnCount(), 3u);
    EXPECT_EQ(sheet->rowCount(), 0u);
    EXPECT_EQ(sheet->cellCount(), 0u);
}

TEST(CSVReaderTest, SkipEmptyLines) {
    const std::string csv = "A,B\n\n1,2\n\n3,4\n\n";
    CSVReadResult result = readCSV(csv);
    EXPECT_TRUE(result.ok());
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->rowCount(), 2u);
}

// --- Sample File Tests ---

TEST(SampleCSVFileTest, ParseSimpleCSV) {
    const std::string content = readTestFile("simple.csv");
    ASSERT_FALSE(content.empty()) << "Could not read simple.csv";

    CSVReadResult result = readCSV(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->columnCount(), 3u);  // Name, Age, City
    EXPECT_EQ(sheet->rowCount(), 3u);     // Alice, Bob, Charlie
    EXPECT_EQ(sheet->cellCount(), 9u);
}

TEST(SampleCSVFileTest, ParseQuotedCSV) {
    const std::string content = readTestFile("quoted.csv");
    ASSERT_FALSE(content.empty()) << "Could not read quoted.csv";

    CSVReadResult result = readCSV(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->columnCount(), 3u);  // Name, Description, Value
    EXPECT_EQ(sheet->rowCount(), 4u);     // 4 data rows
}

TEST(SampleCSVFileTest, ParseUnicodeCSV) {
    const std::string content = readTestFile("unicode.csv");
    ASSERT_FALSE(content.empty()) << "Could not read unicode.csv";

    CSVReadResult result = readCSV(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->columnCount(), 3u);
    EXPECT_EQ(sheet->rowCount(), 4u);  // Hans, Marie, Yuki, Chen
}

TEST(SampleCSVFileTest, ParseNumericCSV) {
    const std::string content = readTestFile("numeric.csv");
    ASSERT_FALSE(content.empty()) << "Could not read numeric.csv";

    CSVReadResult result = readCSV(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->columnCount(), 5u);  // Integer, Float, Negative, Scientific, Text
    EXPECT_EQ(sheet->rowCount(), 3u);
}

TEST(SampleCSVFileTest, ParseTabsTSV) {
    const std::string content = readTestFile("tabs.tsv");
    ASSERT_FALSE(content.empty()) << "Could not read tabs.tsv";

    CSVReadOptions options;
    options.delimiter = '\t';

    CSVReadResult result = readCSV(content, options);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->columnCount(), 3u);  // Name, Score, Grade
    EXPECT_EQ(sheet->rowCount(), 3u);     // Alice, Bob, Charlie
}

TEST(SampleCSVFileTest, ParseNoHeaderCSV) {
    const std::string content = readTestFile("noheader.csv");
    ASSERT_FALSE(content.empty()) << "Could not read noheader.csv";

    CSVReadOptions options;
    options.hasHeader = false;

    CSVReadResult result = readCSV(content, options);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->columnCount(), 3u);
    EXPECT_EQ(sheet->rowCount(), 3u);
    EXPECT_EQ(sheet->cellCount(), 9u);
}

TEST(SampleCSVFileTest, ParseBooleanCSV) {
    const std::string content = readTestFile("boolean.csv");
    ASSERT_FALSE(content.empty()) << "Could not read boolean.csv";

    CSVReadResult result = readCSV(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    EXPECT_EQ(sheet->columnCount(), 3u);  // Name, Active, Verified
    EXPECT_EQ(sheet->rowCount(), 3u);
}

TEST(SampleCSVFileTest, ParseBOMCSV) {
    const std::string content = readTestFile("bom.csv");
    ASSERT_FALSE(content.empty()) << "Could not read bom.csv";

    CSVReadResult result = readCSV(content);
    EXPECT_TRUE(result.ok()) << (result.error ? result.error->toString() : "");
    ASSERT_NE(result.workbook, nullptr);

    Sheet* sheet = result.workbook->getSheetByIndex(0);
    // First column should be "Name", not with BOM
    Axis* firstCol = sheet->getColumn(sheet->firstCol);
    ASSERT_NE(firstCol, nullptr);
    EXPECT_EQ(firstCol->name, "Name");
}

}  // namespace
}  // namespace cells
