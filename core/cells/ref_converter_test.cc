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
// Helper to create test sheet with columns/rows and cells
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

    // Create cells at each intersection for testing cell UUID format
    for (size_t c = 0; c < colIds.size(); ++c) {
        for (size_t r = 0; r < rowIds.size(); ++r) {
            auto cell = std::make_unique<Cell>(generate_id(), colIds[c], rowIds[r]);
            sheet->addCell(std::move(cell));
        }
    }

    return sheet;
}

// Helper to get cell ID at a given col/row position
std::string getCellIdAt(const Sheet& sheet, size_t colPos, size_t rowPos) {
    // Find col and row IDs by position
    ID colId, rowId;
    for (const auto& pair : sheet.columns) {
        if (pair.second->position == colPos) {
            colId = pair.first;
            break;
        }
    }
    for (const auto& pair : sheet.rows) {
        if (pair.second->position == rowPos) {
            rowId = pair.first;
            break;
        }
    }
    // Find cell at this col/row
    for (const auto& pair : sheet.cells) {
        if (pair.second->colId == colId && pair.second->rowId == rowId) {
            return pair.second->id.toString();
        }
    }
    return "";
}

// ============================================================================
// UUID to A1 Conversion Tests
// ============================================================================

TEST(RefConverterTest, UuidRefToA1WithContext) {
    auto sheet = createTestSheet();
    RefConverter converter;
    converter.setContext(*sheet);

    // Get cell ID at A1 (col 0, row 0)
    std::string cellIdA1 = getCellIdAt(*sheet, 0, 0);
    EXPECT_FALSE(cellIdA1.empty());
    EXPECT_EQ(converter.uuidRefToA1(cellIdA1), "A1");

    // Get cell ID at C5 (col 2, row 4)
    std::string cellIdC5 = getCellIdAt(*sheet, 2, 4);
    EXPECT_FALSE(cellIdC5.empty());
    EXPECT_EQ(converter.uuidRefToA1(cellIdC5), "C5");

    // Test with absolute markers
    EXPECT_EQ(converter.uuidRefToA1("$$" + cellIdA1), "$A$1");
    EXPECT_EQ(converter.uuidRefToA1("$~" + cellIdA1), "$A1");
    EXPECT_EQ(converter.uuidRefToA1("~$" + cellIdA1), "A$1");
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

    // Get cell IDs
    std::string cellIdA1 = getCellIdAt(*sheet, 0, 0);
    std::string cellIdB2 = getCellIdAt(*sheet, 1, 1);

    // Create a formula using cell UUIDs: =A1+B2
    std::string formula = cellIdA1 + "+" + cellIdB2;
    std::string result = converter.formulaToA1(formula);
    EXPECT_EQ(result, "A1+B2");
}

TEST(RefConverterTest, FormulaToA1WithFunctions) {
    auto sheet = createTestSheet();
    RefConverter converter;
    converter.setContext(*sheet);

    // Get cell IDs
    std::string cellIdA1 = getCellIdAt(*sheet, 0, 0);
    std::string cellIdB2 = getCellIdAt(*sheet, 1, 1);

    // Create a formula using cell UUIDs: =SUM(A1,B2)
    std::string formula = "SUM(" + cellIdA1 + "," + cellIdB2 + ")";
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

    // Convert A1 to UUID format (new cell UUID format)
    // For relative refs, output is ~~cellId (10 chars) for consistent lexer parsing
    std::string uuidRef = converter.a1RefToUuid("A1");
    EXPECT_FALSE(uuidRef.empty());
    EXPECT_EQ(uuidRef.size(), 10u);  // New format: ~~cellId (10 chars)
    EXPECT_EQ(uuidRef.substr(0, 2), "~~");
    // Remaining chars should be alphanumeric (base62 ID)
    for (size_t i = 2; i < uuidRef.size(); ++i) {
        EXPECT_TRUE(std::isalnum(static_cast<unsigned char>(uuidRef[i])));
    }

    // Convert back should give A1
    EXPECT_EQ(converter.uuidRefToA1(uuidRef), "A1");

    // Test absolute references
    std::string absRef = converter.a1RefToUuid("$A$1");
    EXPECT_FALSE(absRef.empty());
    EXPECT_EQ(absRef.size(), 10u);  // $$cellId = 10 chars
    EXPECT_EQ(absRef.substr(0, 2), "$$");
    EXPECT_EQ(converter.uuidRefToA1(absRef), "$A$1");

    // Test mixed references
    std::string colAbsRef = converter.a1RefToUuid("$A1");
    EXPECT_EQ(colAbsRef.size(), 10u);  // $~cellId = 10 chars
    EXPECT_EQ(colAbsRef.substr(0, 2), "$~");
    EXPECT_EQ(converter.uuidRefToA1(colAbsRef), "$A1");

    std::string rowAbsRef = converter.a1RefToUuid("A$1");
    EXPECT_EQ(rowAbsRef.size(), 10u);  // ~$cellId = 10 chars
    EXPECT_EQ(rowAbsRef.substr(0, 2), "~$");
    EXPECT_EQ(converter.uuidRefToA1(rowAbsRef), "A$1");
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

    // The result should contain cell UUID refs (~~cellId for relative refs)
    // Format: ~~cellId+~~cellId (10 chars each)
    // Should be 10 + 1 + 10 = 21 chars total
    EXPECT_EQ(result.size(), 21u);
    EXPECT_EQ(result[10], '+');

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
// Sparse Position Tests (positions don't start at 0 or have gaps)
// ============================================================================

// This test catches the bug where indexToColId_/indexToRowId_ were vectors
// indexed by loop counter instead of maps indexed by actual position.
// When positions don't start at 0, vector indexing fails.
TEST(RefConverterTest, SparsePositionsA1ToUuid) {
    auto sheet = std::make_unique<Sheet>(generate_id(), "SparseSheet");

    // Create column at position 0 (like column A)
    auto colA = std::make_unique<Axis>(generate_id(), true);
    colA->position = 0;
    ID colAId = colA->id;
    sheet->addColumn(std::move(colA));

    // Create row at position 1 (like row 2 in A1 notation - NOT position 0!)
    // This is the key: the first row exists at position 1, not 0
    auto row2 = std::make_unique<Axis>(generate_id(), false);
    row2->position = 1;  // Row 2 in A1 notation
    ID row2Id = row2->id;
    sheet->addRow(std::move(row2));

    // Create cell at A2 (col 0, row 1)
    auto cellA2 = std::make_unique<Cell>(generate_id(), colAId, row2Id);
    std::string cellA2Id = cellA2->id.toString();
    sheet->addCell(std::move(cellA2));

    RefConverter converter;
    converter.setContext(*sheet);

    // Converting "A2" should find the cell at (col 0, row 1)
    // Before the fix, this would fail because indexToRowId_ was a vector
    // with one element at index 0, but we needed index 1.
    std::string uuidRef = converter.a1RefToUuid("A2");
    EXPECT_FALSE(uuidRef.empty()) << "a1RefToUuid('A2') should find cell at position (0,1)";
    // Relative refs use ~~ prefix
    EXPECT_EQ(uuidRef, "~~" + cellA2Id) << "UUID ref should be ~~cellId for relative refs";

    // Converting back should give A2
    EXPECT_EQ(converter.uuidRefToA1(uuidRef), "A2");
}

TEST(RefConverterTest, SparsePositionsFormulaRoundtrip) {
    auto sheet = std::make_unique<Sheet>(generate_id(), "SparseSheet");

    // Create columns at positions 0 and 1
    auto colA = std::make_unique<Axis>(generate_id(), true);
    colA->position = 0;
    ID colAId = colA->id;
    sheet->addColumn(std::move(colA));

    auto colB = std::make_unique<Axis>(generate_id(), true);
    colB->position = 1;
    ID colBId = colB->id;
    sheet->addColumn(std::move(colB));

    // Create row at position 1 only (row 2 in A1 notation)
    auto row2 = std::make_unique<Axis>(generate_id(), false);
    row2->position = 1;
    ID row2Id = row2->id;
    sheet->addRow(std::move(row2));

    // Create cells at A2 and B2
    auto cellA2 = std::make_unique<Cell>(generate_id(), colAId, row2Id);
    sheet->addCell(std::move(cellA2));

    auto cellB2 = std::make_unique<Cell>(generate_id(), colBId, row2Id);
    sheet->addCell(std::move(cellB2));

    RefConverter converter;
    converter.setContext(*sheet);

    // Formula =A2*10 should roundtrip correctly
    std::string formula = "=A2*10";
    std::string uuidFormula = converter.formulaToUuid(formula);
    EXPECT_NE(uuidFormula, formula) << "Formula should be converted to UUID format";

    std::string backToA1 = converter.formulaToA1(uuidFormula);
    EXPECT_EQ(backToA1, formula) << "Formula should roundtrip correctly";
}

TEST(RefConverterTest, GappedPositions) {
    auto sheet = std::make_unique<Sheet>(generate_id(), "GappedSheet");

    // Create columns at positions 0 and 5 (gap at 1-4)
    auto colA = std::make_unique<Axis>(generate_id(), true);
    colA->position = 0;
    ID colAId = colA->id;
    sheet->addColumn(std::move(colA));

    auto colF = std::make_unique<Axis>(generate_id(), true);
    colF->position = 5;  // Column F
    ID colFId = colF->id;
    sheet->addColumn(std::move(colF));

    // Create row at position 0
    auto row1 = std::make_unique<Axis>(generate_id(), false);
    row1->position = 0;
    ID row1Id = row1->id;
    sheet->addRow(std::move(row1));

    // Create cells at A1 and F1
    auto cellA1 = std::make_unique<Cell>(generate_id(), colAId, row1Id);
    std::string cellA1Id = cellA1->id.toString();
    sheet->addCell(std::move(cellA1));

    auto cellF1 = std::make_unique<Cell>(generate_id(), colFId, row1Id);
    std::string cellF1Id = cellF1->id.toString();
    sheet->addCell(std::move(cellF1));

    RefConverter converter;
    converter.setContext(*sheet);

    // A1 should convert correctly (relative refs use ~~ prefix)
    EXPECT_EQ(converter.a1RefToUuid("A1"), "~~" + cellA1Id);
    EXPECT_EQ(converter.uuidRefToA1("~~" + cellA1Id), "A1");

    // F1 should convert correctly (column at position 5)
    EXPECT_EQ(converter.a1RefToUuid("F1"), "~~" + cellF1Id);
    EXPECT_EQ(converter.uuidRefToA1("~~" + cellF1Id), "F1");

    // B1 through E1 should fail (no columns at those positions)
    EXPECT_EQ(converter.a1RefToUuid("B1"), "");
    EXPECT_EQ(converter.a1RefToUuid("C1"), "");
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

// ============================================================================
// adjustFormulaReferences Tests
// ============================================================================

TEST(RefConverterTest, AdjustFormulaReferencesBasic) {
    // Basic relative reference adjustment
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=A1", 1, 1), "=B2");
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=A1", 0, 1), "=A2");
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=A1", 1, 0), "=B1");
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=A1", 2, 3), "=C4");
}

TEST(RefConverterTest, AdjustFormulaReferencesAbsolute) {
    // Absolute references should not be adjusted
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=$A$1", 1, 1), "=$A$1");
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=$A$1", 5, 10), "=$A$1");
}

TEST(RefConverterTest, AdjustFormulaReferencesMixed) {
    // Mixed absolute/relative references
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=$A1", 1, 1), "=$A2");  // Col absolute
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=A$1", 1, 1), "=B$1");  // Row absolute
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=$A1+B$2", 1, 1), "=$A2+C$2");
}

TEST(RefConverterTest, AdjustFormulaReferencesComplex) {
    // Complex formulas with multiple references
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=A1+B2", 1, 1), "=B2+C3");
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=SUM(A1,B2,C3)", 1, 0), "=SUM(B1,C2,D3)");
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=IF(A1>0,B1,C1)", 0, 1), "=IF(A2>0,B2,C2)");
}

TEST(RefConverterTest, AdjustFormulaReferencesRange) {
    // Range references
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=SUM(A1:B2)", 1, 1), "=SUM(B2:C3)");
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=SUM($A$1:B2)", 1, 1), "=SUM($A$1:C3)");
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=SUM(A1:$B$2)", 1, 1), "=SUM(B2:$B$2)");
}

TEST(RefConverterTest, AdjustFormulaReferencesNegativeOffset) {
    // Negative offsets (for filling upward/leftward)
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=B2", -1, -1), "=A1");
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=C3", -2, -2), "=A1");
}

TEST(RefConverterTest, AdjustFormulaReferencesInvalidRef) {
    // References that would become invalid (negative index)
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=A1", -1, 0), "=#REF!");
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=A1", 0, -1), "=#REF!");
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=A1+B2", -1, 0), "=#REF!+A2");
    // Absolute refs are preserved even with negative offset
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=$A$1", -1, -1), "=$A$1");
}

TEST(RefConverterTest, AdjustFormulaReferencesPreservesStrings) {
    // String literals should not be modified
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=\"A1\"", 1, 1), "=\"A1\"");
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=CONCAT(\"A1\",B2)", 1, 1),
              "=CONCAT(\"A1\",C3)");
}

TEST(RefConverterTest, AdjustFormulaReferencesEmpty) {
    // Edge cases
    EXPECT_EQ(RefConverter::adjustFormulaReferences("", 1, 1), "");
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=1+2", 1, 1), "=1+2");
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=\"hello\"", 1, 1), "=\"hello\"");
}

TEST(RefConverterTest, AdjustFormulaReferencesLargeOffset) {
    // Large offsets (filling many rows/columns)
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=A1", 0, 99), "=A100");
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=A1", 25, 0), "=Z1");
    EXPECT_EQ(RefConverter::adjustFormulaReferences("=A1", 26, 0), "=AA1");
}

}  // namespace
}  // namespace cells
