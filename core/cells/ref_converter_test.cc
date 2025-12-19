#include "core/cells/ref_converter.h"

#include <gtest/gtest.h>

#include "core/cells/id.h"
#include "core/cells/model.h"

namespace cells {
namespace {

// ============================================================================
// columnIndexToLetter Tests
// ============================================================================

TEST(RefConverterTest, ColumnIndexToLetterSingleLetter) {
    EXPECT_EQ(RefConverter::columnIndexToLetter(0), "A");
    EXPECT_EQ(RefConverter::columnIndexToLetter(1), "B");
    EXPECT_EQ(RefConverter::columnIndexToLetter(2), "C");
    EXPECT_EQ(RefConverter::columnIndexToLetter(25), "Z");
}

TEST(RefConverterTest, ColumnIndexToLetterDoubleLetter) {
    EXPECT_EQ(RefConverter::columnIndexToLetter(26), "AA");
    EXPECT_EQ(RefConverter::columnIndexToLetter(27), "AB");
    EXPECT_EQ(RefConverter::columnIndexToLetter(51), "AZ");
    EXPECT_EQ(RefConverter::columnIndexToLetter(52), "BA");
    EXPECT_EQ(RefConverter::columnIndexToLetter(701), "ZZ");
}

TEST(RefConverterTest, ColumnIndexToLetterTripleLetter) {
    EXPECT_EQ(RefConverter::columnIndexToLetter(702), "AAA");
    EXPECT_EQ(RefConverter::columnIndexToLetter(703), "AAB");
}

// ============================================================================
// columnLetterToIndex Tests
// ============================================================================

TEST(RefConverterTest, ColumnLetterToIndexSingleLetter) {
    EXPECT_EQ(RefConverter::columnLetterToIndex("A"), 0);
    EXPECT_EQ(RefConverter::columnLetterToIndex("B"), 1);
    EXPECT_EQ(RefConverter::columnLetterToIndex("C"), 2);
    EXPECT_EQ(RefConverter::columnLetterToIndex("Z"), 25);
}

TEST(RefConverterTest, ColumnLetterToIndexLowercase) {
    EXPECT_EQ(RefConverter::columnLetterToIndex("a"), 0);
    EXPECT_EQ(RefConverter::columnLetterToIndex("b"), 1);
    EXPECT_EQ(RefConverter::columnLetterToIndex("z"), 25);
}

TEST(RefConverterTest, ColumnLetterToIndexDoubleLetter) {
    EXPECT_EQ(RefConverter::columnLetterToIndex("AA"), 26);
    EXPECT_EQ(RefConverter::columnLetterToIndex("AB"), 27);
    EXPECT_EQ(RefConverter::columnLetterToIndex("AZ"), 51);
    EXPECT_EQ(RefConverter::columnLetterToIndex("BA"), 52);
    EXPECT_EQ(RefConverter::columnLetterToIndex("ZZ"), 701);
}

TEST(RefConverterTest, ColumnLetterToIndexTripleLetter) {
    EXPECT_EQ(RefConverter::columnLetterToIndex("AAA"), 702);
    EXPECT_EQ(RefConverter::columnLetterToIndex("AAB"), 703);
}

TEST(RefConverterTest, ColumnLetterToIndexInvalid) {
    EXPECT_EQ(RefConverter::columnLetterToIndex(""), -1);
    EXPECT_EQ(RefConverter::columnLetterToIndex("1"), -1);
    EXPECT_EQ(RefConverter::columnLetterToIndex("A1"), -1);
    EXPECT_EQ(RefConverter::columnLetterToIndex("!"), -1);
}

TEST(RefConverterTest, ColumnLetterRoundtrip) {
    // Test that letter -> index -> letter round-trips correctly
    for (size_t i = 0; i < 1000; ++i) {
        std::string letter = RefConverter::columnIndexToLetter(i);
        int index = RefConverter::columnLetterToIndex(letter);
        EXPECT_EQ(static_cast<size_t>(index), i)
            << "Failed for index " << i << " (letter: " << letter << ")";
    }
}

// ============================================================================
// parseA1Ref Tests
// ============================================================================

TEST(RefConverterTest, ParseA1RefSimple) {
    CellRef ref = RefConverter::parseA1Ref("A1");
    EXPECT_TRUE(ref.valid);
    EXPECT_EQ(ref.colIndex, 0u);
    EXPECT_EQ(ref.rowIndex, 0u);
    EXPECT_EQ(ref.type, ReferenceType::RELATIVE);
}

TEST(RefConverterTest, ParseA1RefLargerCell) {
    CellRef ref = RefConverter::parseA1Ref("C10");
    EXPECT_TRUE(ref.valid);
    EXPECT_EQ(ref.colIndex, 2u);
    EXPECT_EQ(ref.rowIndex, 9u);
    EXPECT_EQ(ref.type, ReferenceType::RELATIVE);
}

TEST(RefConverterTest, ParseA1RefDoubleColumn) {
    CellRef ref = RefConverter::parseA1Ref("AA100");
    EXPECT_TRUE(ref.valid);
    EXPECT_EQ(ref.colIndex, 26u);
    EXPECT_EQ(ref.rowIndex, 99u);
}

TEST(RefConverterTest, ParseA1RefAbsolute) {
    CellRef ref = RefConverter::parseA1Ref("$A$1");
    EXPECT_TRUE(ref.valid);
    EXPECT_EQ(ref.colIndex, 0u);
    EXPECT_EQ(ref.rowIndex, 0u);
    EXPECT_EQ(ref.type, ReferenceType::ABSOLUTE);
}

TEST(RefConverterTest, ParseA1RefAbsoluteCol) {
    CellRef ref = RefConverter::parseA1Ref("$A1");
    EXPECT_TRUE(ref.valid);
    EXPECT_EQ(ref.colIndex, 0u);
    EXPECT_EQ(ref.rowIndex, 0u);
    EXPECT_EQ(ref.type, ReferenceType::COL_ABS);
}

TEST(RefConverterTest, ParseA1RefAbsoluteRow) {
    CellRef ref = RefConverter::parseA1Ref("A$1");
    EXPECT_TRUE(ref.valid);
    EXPECT_EQ(ref.colIndex, 0u);
    EXPECT_EQ(ref.rowIndex, 0u);
    EXPECT_EQ(ref.type, ReferenceType::ROW_ABS);
}

TEST(RefConverterTest, ParseA1RefLowercase) {
    CellRef ref = RefConverter::parseA1Ref("b2");
    EXPECT_TRUE(ref.valid);
    EXPECT_EQ(ref.colIndex, 1u);
    EXPECT_EQ(ref.rowIndex, 1u);
}

TEST(RefConverterTest, ParseA1RefInvalid) {
    EXPECT_FALSE(RefConverter::parseA1Ref("").valid);
    EXPECT_FALSE(RefConverter::parseA1Ref("1").valid);
    EXPECT_FALSE(RefConverter::parseA1Ref("A").valid);
    EXPECT_FALSE(RefConverter::parseA1Ref("A0").valid);  // Row 0 is invalid
    EXPECT_FALSE(RefConverter::parseA1Ref("$").valid);
}

// ============================================================================
// parseRangeRef Tests
// ============================================================================

TEST(RefConverterTest, ParseRangeRefSimple) {
    RangeRef range = RefConverter::parseRangeRef("A1:C3");
    EXPECT_TRUE(range.valid);
    EXPECT_TRUE(range.start.valid);
    EXPECT_TRUE(range.end.valid);
    EXPECT_EQ(range.start.colIndex, 0u);
    EXPECT_EQ(range.start.rowIndex, 0u);
    EXPECT_EQ(range.end.colIndex, 2u);
    EXPECT_EQ(range.end.rowIndex, 2u);
}

TEST(RefConverterTest, ParseRangeRefAbsolute) {
    RangeRef range = RefConverter::parseRangeRef("$A$1:$C$3");
    EXPECT_TRUE(range.valid);
    EXPECT_EQ(range.start.type, ReferenceType::ABSOLUTE);
    EXPECT_EQ(range.end.type, ReferenceType::ABSOLUTE);
}

TEST(RefConverterTest, ParseRangeRefSingleCell) {
    // A single cell reference should also work
    RangeRef range = RefConverter::parseRangeRef("A1");
    EXPECT_TRUE(range.valid);
    EXPECT_EQ(range.start.colIndex, range.end.colIndex);
    EXPECT_EQ(range.start.rowIndex, range.end.rowIndex);
}

TEST(RefConverterTest, ParseRangeRefInvalid) {
    EXPECT_FALSE(RefConverter::parseRangeRef("A1:").valid);
    EXPECT_FALSE(RefConverter::parseRangeRef(":C3").valid);
    EXPECT_FALSE(RefConverter::parseRangeRef("A1:X").valid);
}

// ============================================================================
// formatA1Ref Tests
// ============================================================================

TEST(RefConverterTest, FormatA1RefSimple) {
    CellRef ref;
    ref.colIndex = 0;
    ref.rowIndex = 0;
    ref.type = ReferenceType::RELATIVE;
    ref.valid = true;
    EXPECT_EQ(RefConverter::formatA1Ref(ref), "A1");
}

TEST(RefConverterTest, FormatA1RefAbsolute) {
    CellRef ref;
    ref.colIndex = 2;
    ref.rowIndex = 9;
    ref.type = ReferenceType::ABSOLUTE;
    ref.valid = true;
    EXPECT_EQ(RefConverter::formatA1Ref(ref), "$C$10");
}

TEST(RefConverterTest, FormatA1RefAbsoluteCol) {
    CellRef ref;
    ref.colIndex = 0;
    ref.rowIndex = 0;
    ref.type = ReferenceType::COL_ABS;
    ref.valid = true;
    EXPECT_EQ(RefConverter::formatA1Ref(ref), "$A1");
}

TEST(RefConverterTest, FormatA1RefAbsoluteRow) {
    CellRef ref;
    ref.colIndex = 0;
    ref.rowIndex = 0;
    ref.type = ReferenceType::ROW_ABS;
    ref.valid = true;
    EXPECT_EQ(RefConverter::formatA1Ref(ref), "A$1");
}

TEST(RefConverterTest, FormatA1RefInvalid) {
    CellRef ref;
    ref.valid = false;
    EXPECT_EQ(RefConverter::formatA1Ref(ref), "");
}

// ============================================================================
// Helper to create test sheet with columns/rows
// ============================================================================

std::unique_ptr<Sheet> createTestSheet() {
    auto sheet = std::make_unique<Sheet>(generate_id(), "TestSheet");

    // Create 5 columns (A-E)
    std::vector<ID> colIds;
    for (int i = 0; i < 5; ++i) {
        auto col = std::make_unique<Axis>(generate_id(), true);
        col->position = static_cast<uint32_t>(i);
        colIds.push_back(col->id);
        sheet->addColumn(std::move(col));
    }

    // Create 10 rows (1-10)
    std::vector<ID> rowIds;
    for (int i = 0; i < 10; ++i) {
        auto row = std::make_unique<Axis>(generate_id(), false);
        row->position = static_cast<uint32_t>(i);
        rowIds.push_back(row->id);
        sheet->addRow(std::move(row));
    }

    return sheet;
}

// ============================================================================
// UUID to A1 Conversion Tests
// ============================================================================

TEST(RefConverterTest, UuidRefToA1WithContext) {
    auto sheet = createTestSheet();
    RefConverter converter;
    converter.setContext(*sheet);

    // Get the first column and row IDs
    std::vector<std::pair<uint32_t, ID>> columns;
    for (const auto& pair : sheet->columns) {
        columns.emplace_back(pair.second->position, pair.first);
    }
    std::sort(columns.begin(), columns.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<std::pair<uint32_t, ID>> rows;
    for (const auto& pair : sheet->rows) {
        rows.emplace_back(pair.second->position, pair.first);
    }
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Create a UUID ref for A1
    std::string uuidRef = "$" + columns[0].second.toString() + "$" + rows[0].second.toString();
    EXPECT_EQ(converter.uuidRefToA1(uuidRef), "A1");

    // Create a UUID ref for C5
    std::string uuidRef2 = "$" + columns[2].second.toString() + "$" + rows[4].second.toString();
    EXPECT_EQ(converter.uuidRefToA1(uuidRef2), "C5");
}

TEST(RefConverterTest, UuidRefToA1InvalidRef) {
    RefConverter converter;
    // No context set
    EXPECT_EQ(converter.uuidRefToA1("$xxxxxxxx$yyyyyyyy"), "");
    EXPECT_EQ(converter.uuidRefToA1("invalid"), "");
    EXPECT_EQ(converter.uuidRefToA1(""), "");
}

TEST(RefConverterTest, FormulaToA1Simple) {
    auto sheet = createTestSheet();
    RefConverter converter;
    converter.setContext(*sheet);

    // Get ordered IDs
    std::vector<std::pair<uint32_t, ID>> columns;
    for (const auto& pair : sheet->columns) {
        columns.emplace_back(pair.second->position, pair.first);
    }
    std::sort(columns.begin(), columns.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<std::pair<uint32_t, ID>> rows;
    for (const auto& pair : sheet->rows) {
        rows.emplace_back(pair.second->position, pair.first);
    }
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Create a formula: =A1+B2
    std::string col0 = columns[0].second.toString();
    std::string col1 = columns[1].second.toString();
    std::string row0 = rows[0].second.toString();
    std::string row1 = rows[1].second.toString();

    std::string formula = "$" + col0 + "$" + row0 + "+$" + col1 + "$" + row1;
    std::string result = converter.formulaToA1(formula);
    EXPECT_EQ(result, "A1+B2");
}

TEST(RefConverterTest, FormulaToA1WithFunctions) {
    auto sheet = createTestSheet();
    RefConverter converter;
    converter.setContext(*sheet);

    // Get ordered IDs
    std::vector<std::pair<uint32_t, ID>> columns;
    for (const auto& pair : sheet->columns) {
        columns.emplace_back(pair.second->position, pair.first);
    }
    std::sort(columns.begin(), columns.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<std::pair<uint32_t, ID>> rows;
    for (const auto& pair : sheet->rows) {
        rows.emplace_back(pair.second->position, pair.first);
    }
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Create a formula: =SUM(A1,B2)
    std::string col0 = columns[0].second.toString();
    std::string col1 = columns[1].second.toString();
    std::string row0 = rows[0].second.toString();
    std::string row1 = rows[1].second.toString();

    std::string formula = "SUM($" + col0 + "$" + row0 + ",$" + col1 + "$" + row1 + ")";
    std::string result = converter.formulaToA1(formula);
    EXPECT_EQ(result, "SUM(A1,B2)");
}

TEST(RefConverterTest, FormulaToA1NoRefs) {
    RefConverter converter;
    EXPECT_EQ(converter.formulaToA1("1+2"), "1+2");
    EXPECT_EQ(converter.formulaToA1("SUM(1,2,3)"), "SUM(1,2,3)");
    EXPECT_EQ(converter.formulaToA1(""), "");
}

// ============================================================================
// A1 to UUID Conversion Tests
// ============================================================================

TEST(RefConverterTest, A1RefToUuidWithContext) {
    auto sheet = createTestSheet();
    RefConverter converter;
    converter.setContext(*sheet);

    // Convert A1 to UUID format
    std::string uuidRef = converter.a1RefToUuid("A1");
    EXPECT_FALSE(uuidRef.empty());
    EXPECT_EQ(uuidRef[0], '$');
    EXPECT_EQ(uuidRef[9], '$');
    EXPECT_EQ(uuidRef.size(), 18u);

    // Convert back should give A1
    EXPECT_EQ(converter.uuidRefToA1(uuidRef), "A1");
}

TEST(RefConverterTest, A1RefToUuidOutOfRange) {
    auto sheet = createTestSheet();
    RefConverter converter;
    converter.setContext(*sheet);

    // Our test sheet only has 5 columns (A-E) and 10 rows (1-10)
    EXPECT_EQ(converter.a1RefToUuid("Z1"), "");    // Column out of range
    EXPECT_EQ(converter.a1RefToUuid("A100"), "");  // Row out of range
}

TEST(RefConverterTest, FormulaToUuidSimple) {
    auto sheet = createTestSheet();
    RefConverter converter;
    converter.setContext(*sheet);

    std::string result = converter.formulaToUuid("A1+B2");

    // The result should contain UUID refs
    EXPECT_NE(result.find('$'), std::string::npos);

    // Convert back should give original formula
    EXPECT_EQ(converter.formulaToA1(result), "A1+B2");
}

TEST(RefConverterTest, FormulaToUuidWithFunctions) {
    auto sheet = createTestSheet();
    RefConverter converter;
    converter.setContext(*sheet);

    std::string result = converter.formulaToUuid("SUM(A1,B2,C3)");

    // Convert back should give original formula
    EXPECT_EQ(converter.formulaToA1(result), "SUM(A1,B2,C3)");
}

TEST(RefConverterTest, FormulaToUuidRange) {
    auto sheet = createTestSheet();
    RefConverter converter;
    converter.setContext(*sheet);

    std::string result = converter.formulaToUuid("SUM(A1:C3)");

    // The result should contain a range with UUID refs
    EXPECT_NE(result.find(':'), std::string::npos);

    // Convert back should give original formula
    EXPECT_EQ(converter.formulaToA1(result), "SUM(A1:C3)");
}

TEST(RefConverterTest, FormulaToUuidStringLiteral) {
    auto sheet = createTestSheet();
    RefConverter converter;
    converter.setContext(*sheet);

    // String literals should not be converted
    std::string result = converter.formulaToUuid("\"A1\"+B2");

    // The string literal "A1" should be preserved
    EXPECT_NE(result.find("\"A1\""), std::string::npos);

    // But B2 should be converted
    std::string backToA1 = converter.formulaToA1(result);
    EXPECT_NE(backToA1.find("\"A1\""), std::string::npos);
    EXPECT_NE(backToA1.find("B2"), std::string::npos);
}

TEST(RefConverterTest, FormulaToUuidNoRefs) {
    RefConverter converter;
    EXPECT_EQ(converter.formulaToUuid("1+2"), "1+2");
    EXPECT_EQ(converter.formulaToUuid("SUM(1,2,3)"), "SUM(1,2,3)");
    EXPECT_EQ(converter.formulaToUuid(""), "");
}

TEST(RefConverterTest, FormulaToUuidNotAlphanumPrefix) {
    auto sheet = createTestSheet();
    RefConverter converter;
    converter.setContext(*sheet);

    // "SUM" should not have 'M' extracted as a reference start
    std::string result = converter.formulaToUuid("SUM(A1)");
    std::string backToA1 = converter.formulaToA1(result);
    EXPECT_EQ(backToA1, "SUM(A1)");
}

// ============================================================================
// Context Management Tests
// ============================================================================

TEST(RefConverterTest, ClearContext) {
    auto sheet = createTestSheet();
    RefConverter converter;
    converter.setContext(*sheet);

    // Should work
    EXPECT_FALSE(converter.a1RefToUuid("A1").empty());

    // Clear context
    converter.clearContext();

    // Should fail now
    EXPECT_TRUE(converter.a1RefToUuid("A1").empty());
}

TEST(RefConverterTest, SetContextFromIds) {
    std::vector<ID> columnIds = {generate_id(), generate_id(), generate_id()};
    std::vector<ID> rowIds = {generate_id(), generate_id()};

    RefConverter converter;
    converter.setContext(columnIds, rowIds);

    // Should be able to convert A1
    std::string uuidRef = converter.a1RefToUuid("A1");
    EXPECT_FALSE(uuidRef.empty());

    // Should contain the first column and row IDs
    EXPECT_NE(uuidRef.find(columnIds[0].toString()), std::string::npos);
    EXPECT_NE(uuidRef.find(rowIds[0].toString()), std::string::npos);
}

// ============================================================================
// Roundtrip Tests
// ============================================================================

TEST(RefConverterTest, RoundtripSimpleFormula) {
    auto sheet = createTestSheet();
    RefConverter converter;
    converter.setContext(*sheet);

    std::vector<std::string> formulas = {
        "A1", "B2+C3", "SUM(A1:D10)", "IF(A1>0,B1,C1)", "A1*B1/C1", "(A1+B1)*C1",
    };

    for (const auto& formula : formulas) {
        std::string uuid = converter.formulaToUuid(formula);
        std::string backToA1 = converter.formulaToA1(uuid);
        EXPECT_EQ(backToA1, formula) << "Roundtrip failed for: " << formula;
    }
}

TEST(RefConverterTest, RoundtripComplexFormula) {
    auto sheet = createTestSheet();
    RefConverter converter;
    converter.setContext(*sheet);

    // A more complex formula
    std::string formula = "SUM(A1:C3)+AVERAGE(D1:E10)*2";
    std::string uuid = converter.formulaToUuid(formula);
    std::string backToA1 = converter.formulaToA1(uuid);
    EXPECT_EQ(backToA1, formula);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(RefConverterTest, ExcelMaxColumn) {
    // Excel max column is XFD (16384 columns, index 16383)
    EXPECT_EQ(RefConverter::columnIndexToLetter(16383), "XFD");
    EXPECT_EQ(RefConverter::columnLetterToIndex("XFD"), 16383);
}

TEST(RefConverterTest, FormulaWithNumbers) {
    auto sheet = createTestSheet();
    RefConverter converter;
    converter.setContext(*sheet);

    // Numbers that look like they could be part of refs
    std::string formula = "A1+100+B2";
    std::string uuid = converter.formulaToUuid(formula);
    std::string backToA1 = converter.formulaToA1(uuid);
    EXPECT_EQ(backToA1, formula);
}

}  // namespace
}  // namespace cells
