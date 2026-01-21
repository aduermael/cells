// =============================================================================
// CRDT Range Operations Tests
// =============================================================================

#include <gtest/gtest.h>
#include <memory>

#include "core/cells/crdt.h"
#include "core/cells/id.h"
#include "core/cells/model.h"
#include "core/cells/range.h"
#include "core/cells/style_buffer.h"

namespace cells {
namespace {

class CRDTRangeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create workbook with a sheet
        workbook = std::make_unique<Workbook>(generate_id(), "TestWorkbook");
        workbook->setNodeId(generate_id());

        auto sheet = std::make_unique<Sheet>(sheetId, "Sheet1");
        workbook->addSheet(std::move(sheet));

        // Create some columns and rows
        Sheet* s = workbook->getSheet(sheetId);
        for (int i = 0; i < 5; i++) {
            auto col = std::make_unique<Axis>(colIds[i], true);
            col->position = i;
            col->size = 100;
            s->addColumn(std::move(col));

            auto row = std::make_unique<Axis>(rowIds[i], false);
            row->position = i;
            row->size = 25;
            s->addRow(std::move(row));
        }

        // Start collaboration to enable CRDT operations
        workbook->startCollaboration();
    }

    std::unique_ptr<Workbook> workbook;
    const ID sheetId = generate_id();
    const ID colIds[5] = {generate_id(), generate_id(), generate_id(), generate_id(),
                          generate_id()};
    const ID rowIds[5] = {generate_id(), generate_id(), generate_id(), generate_id(),
                          generate_id()};
};

// =============================================================================
// RANGE_ADD Tests
// =============================================================================

TEST_F(CRDTRangeTest, AddRangeCreatesRange) {
    const ID rangeId = generate_id();

    // Build payload
    std::string payload = "{\"sheet_id\":\"" + sheetId.toString() + "\",";
    payload += "\"start_col_id\":\"" + colIds[0].toString() + "\",";
    payload += "\"start_row_id\":\"" + rowIds[0].toString() + "\",";
    payload += "\"end_col_id\":\"" + colIds[2].toString() + "\",";
    payload += "\"end_row_id\":\"" + rowIds[2].toString() + "\",";
    payload += "\"flags\":1}";  // RANGE_MERGE flag

    Operation op = makeRangeAddOp(*workbook, rangeId, payload);
    ApplyResult result = applyOperation(*workbook, op);

    EXPECT_EQ(result, ApplyResult::SUCCESS);

    // Verify range was created
    Sheet* sheet = workbook->getSheet(sheetId);
    ASSERT_NE(sheet, nullptr);

    const Range* range = sheet->getRange(rangeId);
    ASSERT_NE(range, nullptr);

    EXPECT_EQ(range->id, rangeId);
    EXPECT_EQ(range->startColId, colIds[0]);
    EXPECT_EQ(range->startRowId, rowIds[0]);
    EXPECT_EQ(range->endColId, colIds[2]);
    EXPECT_EQ(range->endRowId, rowIds[2]);
    EXPECT_TRUE(range->hasFlag(RangeFlags::MERGE));
}

TEST_F(CRDTRangeTest, AddRangeDuplicateIdReturnsAlreadyApplied) {
    const ID rangeId = generate_id();

    std::string payload = "{\"sheet_id\":\"" + sheetId.toString() + "\",";
    payload += "\"start_col_id\":\"" + colIds[0].toString() + "\",";
    payload += "\"start_row_id\":\"" + rowIds[0].toString() + "\",";
    payload += "\"end_col_id\":\"" + colIds[1].toString() + "\",";
    payload += "\"end_row_id\":\"" + rowIds[1].toString() + "\",";
    payload += "\"flags\":0}";

    // First add should succeed
    Operation op1 = makeRangeAddOp(*workbook, rangeId, payload);
    ApplyResult result1 = applyOperation(*workbook, op1);
    EXPECT_EQ(result1, ApplyResult::SUCCESS);

    // Second add with same range ID should return ALREADY_APPLIED
    // (Note: In real CRDT, same HLC means same operation, so we test duplicate detection)
    Operation op2 = makeRangeAddOp(*workbook, rangeId, payload);
    ApplyResult result2 = applyOperation(*workbook, op2);
    // The range already exists in the sheet, so it should return ALREADY_APPLIED
    EXPECT_EQ(result2, ApplyResult::ALREADY_APPLIED);
}

TEST_F(CRDTRangeTest, AddRangeInvalidColumnReturnsInvalidTarget) {
    const ID rangeId = generate_id();
    const ID fakeColId = generate_id();  // Column doesn't exist

    // Use fake column ID - sheet cannot be derived since column doesn't exist
    std::string payload = "{\"start_col_id\":\"" + fakeColId.toString() + "\",";
    payload += "\"start_row_id\":\"" + rowIds[0].toString() + "\",";
    payload += "\"end_col_id\":\"" + colIds[1].toString() + "\",";
    payload += "\"end_row_id\":\"" + rowIds[1].toString() + "\",";
    payload += "\"flags\":0}";

    Operation op = makeRangeAddOp(*workbook, rangeId, payload);
    ApplyResult result = applyOperation(*workbook, op);

    EXPECT_EQ(result, ApplyResult::INVALID_TARGET);
}

TEST_F(CRDTRangeTest, AddRangeIndexesInRTree) {
    const ID rangeId = generate_id();

    std::string payload = "{\"sheet_id\":\"" + sheetId.toString() + "\",";
    payload += "\"start_col_id\":\"" + colIds[0].toString() + "\",";
    payload += "\"start_row_id\":\"" + rowIds[0].toString() + "\",";
    payload += "\"end_col_id\":\"" + colIds[2].toString() + "\",";
    payload += "\"end_row_id\":\"" + rowIds[2].toString() + "\",";
    payload += "\"flags\":1}";  // MERGE flag

    Operation op = makeRangeAddOp(*workbook, rangeId, payload);
    applyOperation(*workbook, op);

    Sheet* sheet = workbook->getSheet(sheetId);

    // Query for ranges at position (1, 1) - should find the range
    auto ranges = sheet->getRangesAt(1, 1);
    ASSERT_EQ(ranges.size(), 1);
    EXPECT_EQ(ranges[0]->id, rangeId);

    // Query for ranges at position (3, 3) - outside the range
    auto rangesOutside = sheet->getRangesAt(3, 3);
    EXPECT_TRUE(rangesOutside.empty());

    // Query with flag filter
    auto mergeRanges = sheet->getRangesAt(1, 1, RangeFlags::MERGE);
    ASSERT_EQ(mergeRanges.size(), 1);

    auto styleRanges = sheet->getRangesAt(1, 1, RangeFlags::STYLE);
    EXPECT_TRUE(styleRanges.empty());
}

// =============================================================================
// RANGE_REMOVE Tests
// =============================================================================

TEST_F(CRDTRangeTest, RemoveRangeDeletesRange) {
    const ID rangeId = generate_id();

    // First add a range
    std::string addPayload = "{\"sheet_id\":\"" + sheetId.toString() + "\",";
    addPayload += "\"start_col_id\":\"" + colIds[0].toString() + "\",";
    addPayload += "\"start_row_id\":\"" + rowIds[0].toString() + "\",";
    addPayload += "\"end_col_id\":\"" + colIds[1].toString() + "\",";
    addPayload += "\"end_row_id\":\"" + rowIds[1].toString() + "\",";
    addPayload += "\"flags\":0}";

    Operation addOp = makeRangeAddOp(*workbook, rangeId, addPayload);
    applyOperation(*workbook, addOp);

    Sheet* sheet = workbook->getSheet(sheetId);
    ASSERT_NE(sheet->getRange(rangeId), nullptr);

    // Now remove it
    std::string removePayload = "{\"sheet_id\":\"" + sheetId.toString() + "\"}";
    Operation removeOp = makeRangeRemoveOp(*workbook, rangeId, removePayload);
    ApplyResult result = applyOperation(*workbook, removeOp);

    EXPECT_EQ(result, ApplyResult::SUCCESS);
    EXPECT_EQ(sheet->getRange(rangeId), nullptr);
}

TEST_F(CRDTRangeTest, RemoveRangeIdempotent) {
    const ID rangeId = generate_id();

    // Try to remove a range that doesn't exist
    std::string payload = "{\"sheet_id\":\"" + sheetId.toString() + "\"}";
    Operation op = makeRangeRemoveOp(*workbook, rangeId, payload);
    ApplyResult result = applyOperation(*workbook, op);

    // Should succeed (idempotent)
    EXPECT_EQ(result, ApplyResult::SUCCESS);
}

// =============================================================================
// RANGE_UPDATE_CORNERS Tests
// =============================================================================

TEST_F(CRDTRangeTest, UpdateCornersResizesRange) {
    const ID rangeId = generate_id();

    // Add a range A1:B2
    std::string addPayload = "{\"sheet_id\":\"" + sheetId.toString() + "\",";
    addPayload += "\"start_col_id\":\"" + colIds[0].toString() + "\",";
    addPayload += "\"start_row_id\":\"" + rowIds[0].toString() + "\",";
    addPayload += "\"end_col_id\":\"" + colIds[1].toString() + "\",";
    addPayload += "\"end_row_id\":\"" + rowIds[1].toString() + "\",";
    addPayload += "\"flags\":0}";

    Operation addOp = makeRangeAddOp(*workbook, rangeId, addPayload);
    applyOperation(*workbook, addOp);

    // Update corners to A1:D4
    std::string updatePayload = "{\"sheet_id\":\"" + sheetId.toString() + "\",";
    updatePayload += "\"start_col_id\":\"" + colIds[0].toString() + "\",";
    updatePayload += "\"start_row_id\":\"" + rowIds[0].toString() + "\",";
    updatePayload += "\"end_col_id\":\"" + colIds[3].toString() + "\",";
    updatePayload += "\"end_row_id\":\"" + rowIds[3].toString() + "\"}";

    Operation updateOp = makeRangeUpdateCornersOp(*workbook, rangeId, updatePayload);
    ApplyResult result = applyOperation(*workbook, updateOp);

    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Sheet* sheet = workbook->getSheet(sheetId);
    const Range* range = sheet->getRange(rangeId);
    ASSERT_NE(range, nullptr);

    EXPECT_EQ(range->endColId, colIds[3]);
    EXPECT_EQ(range->endRowId, rowIds[3]);

    // Verify index was updated - should now include position (3, 3)
    auto ranges = sheet->getRangesAt(3, 3);
    ASSERT_EQ(ranges.size(), 1);
    EXPECT_EQ(ranges[0]->id, rangeId);
}

TEST_F(CRDTRangeTest, UpdateCornersResurrectsDeletedRange) {
    const ID rangeId = generate_id();

    // Update corners on a non-existent range should resurrect it
    std::string payload = "{\"sheet_id\":\"" + sheetId.toString() + "\",";
    payload += "\"start_col_id\":\"" + colIds[0].toString() + "\",";
    payload += "\"start_row_id\":\"" + rowIds[0].toString() + "\",";
    payload += "\"end_col_id\":\"" + colIds[2].toString() + "\",";
    payload += "\"end_row_id\":\"" + rowIds[2].toString() + "\"}";

    Operation op = makeRangeUpdateCornersOp(*workbook, rangeId, payload);
    ApplyResult result = applyOperation(*workbook, op);

    EXPECT_EQ(result, ApplyResult::RESURRECTED);

    Sheet* sheet = workbook->getSheet(sheetId);
    const Range* range = sheet->getRange(rangeId);
    ASSERT_NE(range, nullptr);

    EXPECT_EQ(range->startColId, colIds[0]);
    EXPECT_EQ(range->endColId, colIds[2]);
}

// =============================================================================
// RANGE_UPDATE_FLAGS Tests
// =============================================================================

TEST_F(CRDTRangeTest, UpdateFlagsChangesFlags) {
    const ID rangeId = generate_id();

    // Add a range with MERGE flag
    std::string addPayload = "{\"sheet_id\":\"" + sheetId.toString() + "\",";
    addPayload += "\"start_col_id\":\"" + colIds[0].toString() + "\",";
    addPayload += "\"start_row_id\":\"" + rowIds[0].toString() + "\",";
    addPayload += "\"end_col_id\":\"" + colIds[1].toString() + "\",";
    addPayload += "\"end_row_id\":\"" + rowIds[1].toString() + "\",";
    addPayload += "\"flags\":1}";  // MERGE

    Operation addOp = makeRangeAddOp(*workbook, rangeId, addPayload);
    applyOperation(*workbook, addOp);

    // Update to MERGE | STYLE
    std::string updatePayload = "{\"sheet_id\":\"" + sheetId.toString() + "\",";
    updatePayload += "\"flags\":3}";  // MERGE | STYLE = 1 | 2 = 3

    Operation updateOp = makeRangeUpdateFlagsOp(*workbook, rangeId, updatePayload);
    ApplyResult result = applyOperation(*workbook, updateOp);

    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Sheet* sheet = workbook->getSheet(sheetId);
    const Range* range = sheet->getRange(rangeId);
    ASSERT_NE(range, nullptr);

    EXPECT_TRUE(range->hasFlag(RangeFlags::MERGE));
    EXPECT_TRUE(range->hasFlag(RangeFlags::STYLE));
}

TEST_F(CRDTRangeTest, UpdateFlagsOnNonExistentRangeReturnsInvalidTarget) {
    const ID rangeId = generate_id();

    std::string payload = "{\"sheet_id\":\"" + sheetId.toString() + "\",";
    payload += "\"flags\":1}";

    Operation op = makeRangeUpdateFlagsOp(*workbook, rangeId, payload);
    ApplyResult result = applyOperation(*workbook, op);

    EXPECT_EQ(result, ApplyResult::INVALID_TARGET);
}

// =============================================================================
// RANGE_SET_STYLE Tests
// =============================================================================

TEST_F(CRDTRangeTest, SetStyleAddsStyleFlag) {
    const ID rangeId = generate_id();

    // Add a range without STYLE flag
    std::string addPayload = "{\"sheet_id\":\"" + sheetId.toString() + "\",";
    addPayload += "\"start_col_id\":\"" + colIds[0].toString() + "\",";
    addPayload += "\"start_row_id\":\"" + rowIds[0].toString() + "\",";
    addPayload += "\"end_col_id\":\"" + colIds[1].toString() + "\",";
    addPayload += "\"end_row_id\":\"" + rowIds[1].toString() + "\",";
    addPayload += "\"flags\":0}";

    Operation addOp = makeRangeAddOp(*workbook, rangeId, addPayload);
    applyOperation(*workbook, addOp);

    Sheet* sheet = workbook->getSheet(sheetId);
    const Range* range = sheet->getRange(rangeId);
    ASSERT_FALSE(range->hasFlag(RangeFlags::STYLE));

    // Set a style using content-addressed StyleBuffer
    StyleBuffer styleBuf;
    styleBuf.setBgColorHex("#ff0000");

    Operation styleOp = makeRangeSetStyleOp(*workbook, rangeId, styleBuf);
    ApplyResult result = applyOperation(*workbook, styleOp);

    EXPECT_EQ(result, ApplyResult::SUCCESS);
    EXPECT_TRUE(range->hasFlag(RangeFlags::STYLE));
}

TEST_F(CRDTRangeTest, SetStyleEmptyRemovesStyleFlag) {
    const ID rangeId = generate_id();

    // Add a range with STYLE flag
    std::string addPayload = "{\"sheet_id\":\"" + sheetId.toString() + "\",";
    addPayload += "\"start_col_id\":\"" + colIds[0].toString() + "\",";
    addPayload += "\"start_row_id\":\"" + rowIds[0].toString() + "\",";
    addPayload += "\"end_col_id\":\"" + colIds[1].toString() + "\",";
    addPayload += "\"end_row_id\":\"" + rowIds[1].toString() + "\",";
    addPayload += "\"flags\":2}";  // STYLE flag

    Operation addOp = makeRangeAddOp(*workbook, rangeId, addPayload);
    applyOperation(*workbook, addOp);

    // Clear the style using empty style payload
    std::string clearPayload = R"({"style":""})";

    Operation clearOp = makeRangeSetStyleOp(*workbook, rangeId, clearPayload);
    ApplyResult result = applyOperation(*workbook, clearOp);

    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Sheet* sheet = workbook->getSheet(sheetId);
    const Range* range = sheet->getRange(rangeId);
    EXPECT_FALSE(range->hasFlag(RangeFlags::STYLE));
}

// =============================================================================
// Multiple Ranges Tests
// =============================================================================

TEST_F(CRDTRangeTest, MultipleOverlappingRangesAtSamePosition) {
    // Create two overlapping ranges
    const ID range1Id = generate_id();
    const ID range2Id = generate_id();

    // Range 1: A1:C3 with MERGE flag
    std::string payload1 = "{\"sheet_id\":\"" + sheetId.toString() + "\",";
    payload1 += "\"start_col_id\":\"" + colIds[0].toString() + "\",";
    payload1 += "\"start_row_id\":\"" + rowIds[0].toString() + "\",";
    payload1 += "\"end_col_id\":\"" + colIds[2].toString() + "\",";
    payload1 += "\"end_row_id\":\"" + rowIds[2].toString() + "\",";
    payload1 += "\"flags\":1}";

    // Range 2: B2:D4 with STYLE flag
    std::string payload2 = "{\"sheet_id\":\"" + sheetId.toString() + "\",";
    payload2 += "\"start_col_id\":\"" + colIds[1].toString() + "\",";
    payload2 += "\"start_row_id\":\"" + rowIds[1].toString() + "\",";
    payload2 += "\"end_col_id\":\"" + colIds[3].toString() + "\",";
    payload2 += "\"end_row_id\":\"" + rowIds[3].toString() + "\",";
    payload2 += "\"flags\":2}";

    Operation op1 = makeRangeAddOp(*workbook, range1Id, payload1);
    Operation op2 = makeRangeAddOp(*workbook, range2Id, payload2);
    applyOperation(*workbook, op1);
    applyOperation(*workbook, op2);

    Sheet* sheet = workbook->getSheet(sheetId);

    // Position (1, 1) = B2 should have both ranges
    auto rangesAtB2 = sheet->getRangesAt(1, 1);
    EXPECT_EQ(rangesAtB2.size(), 2);

    // Position (0, 0) = A1 should have only range1
    auto rangesAtA1 = sheet->getRangesAt(0, 0);
    ASSERT_EQ(rangesAtA1.size(), 1);
    EXPECT_EQ(rangesAtA1[0]->id, range1Id);

    // Position (3, 3) = D4 should have only range2
    auto rangesAtD4 = sheet->getRangesAt(3, 3);
    ASSERT_EQ(rangesAtD4.size(), 1);
    EXPECT_EQ(rangesAtD4[0]->id, range2Id);

    // Query with MERGE flag at B2 should return only range1
    auto mergeRangesAtB2 = sheet->getRangesAt(1, 1, RangeFlags::MERGE);
    ASSERT_EQ(mergeRangesAtB2.size(), 1);
    EXPECT_EQ(mergeRangesAtB2[0]->id, range1Id);

    // Query with STYLE flag at B2 should return only range2
    auto styleRangesAtB2 = sheet->getRangesAt(1, 1, RangeFlags::STYLE);
    ASSERT_EQ(styleRangesAtB2.size(), 1);
    EXPECT_EQ(styleRangesAtB2[0]->id, range2Id);
}

// =============================================================================
// Content-Addressed Style Tests (new format)
// =============================================================================

TEST_F(CRDTRangeTest, SetStyleWithStyleBuffer) {
    const ID rangeId = generate_id();

    // Add a range (no STYLE flag yet)
    std::string addPayload = "{\"sheet_id\":\"" + sheetId.toString() + "\",";
    addPayload += "\"start_col_id\":\"" + colIds[0].toString() + "\",";
    addPayload += "\"start_row_id\":\"" + rowIds[0].toString() + "\",";
    addPayload += "\"end_col_id\":\"" + colIds[1].toString() + "\",";
    addPayload += "\"end_row_id\":\"" + rowIds[1].toString() + "\",";
    addPayload += "\"flags\":0}";

    Operation addOp = makeRangeAddOp(*workbook, rangeId, addPayload);
    applyOperation(*workbook, addOp);

    Sheet* sheet = workbook->getSheet(sheetId);
    Range* range = sheet->getRange(rangeId);
    ASSERT_FALSE(range->hasFlag(RangeFlags::STYLE));

    // Set a style using StyleBuffer
    StyleBuffer style;
    style.setBold(true);
    style.setBgColorHex("#FBBF24");

    Operation styleOp = makeRangeSetStyleOp(*workbook, rangeId, style);
    ApplyResult result = applyOperation(*workbook, styleOp);

    EXPECT_EQ(result, ApplyResult::SUCCESS);
    EXPECT_TRUE(range->hasFlag(RangeFlags::STYLE));
    EXPECT_TRUE(range->hasStyle());

    // Verify style content
    const StyleBuffer* retrievedStyle = range->getStyle();
    ASSERT_NE(retrievedStyle, nullptr);
    EXPECT_TRUE(retrievedStyle->hasBold());
    EXPECT_TRUE(retrievedStyle->getBold());
    EXPECT_TRUE(retrievedStyle->hasBgColor());
    EXPECT_EQ(retrievedStyle->getBgColorHex(), "#FBBF24");
}

TEST_F(CRDTRangeTest, SetStyleWithStyleBufferBase64Payload) {
    const ID rangeId = generate_id();

    // Add a range
    std::string addPayload = "{\"sheet_id\":\"" + sheetId.toString() + "\",";
    addPayload += "\"start_col_id\":\"" + colIds[0].toString() + "\",";
    addPayload += "\"start_row_id\":\"" + rowIds[0].toString() + "\",";
    addPayload += "\"end_col_id\":\"" + colIds[1].toString() + "\",";
    addPayload += "\"end_row_id\":\"" + rowIds[1].toString() + "\",";
    addPayload += "\"flags\":0}";

    Operation addOp = makeRangeAddOp(*workbook, rangeId, addPayload);
    applyOperation(*workbook, addOp);

    // Create a style and encode manually
    StyleBuffer style;
    style.setItalic(true);
    style.setFontSize(14);
    std::string base64 = style.toBase64();

    // Use the new payload format directly
    std::string stylePayload = "{\"style\":\"" + base64 + "\"}";
    Operation styleOp = makeRangeSetStyleOp(*workbook, rangeId, stylePayload);
    ApplyResult result = applyOperation(*workbook, styleOp);

    EXPECT_EQ(result, ApplyResult::SUCCESS);

    Sheet* sheet = workbook->getSheet(sheetId);
    Range* range = sheet->getRange(rangeId);
    EXPECT_TRUE(range->hasStyle());

    const StyleBuffer* retrievedStyle = range->getStyle();
    ASSERT_NE(retrievedStyle, nullptr);
    EXPECT_TRUE(retrievedStyle->hasItalic());
    EXPECT_TRUE(retrievedStyle->getItalic());
    EXPECT_TRUE(retrievedStyle->hasFontSize());
    EXPECT_EQ(retrievedStyle->getFontSize(), 14);
}

TEST_F(CRDTRangeTest, ClearStyleWithNewFormat) {
    const ID rangeId = generate_id();

    // Add a range with a style
    std::string addPayload = "{\"sheet_id\":\"" + sheetId.toString() + "\",";
    addPayload += "\"start_col_id\":\"" + colIds[0].toString() + "\",";
    addPayload += "\"start_row_id\":\"" + rowIds[0].toString() + "\",";
    addPayload += "\"end_col_id\":\"" + colIds[1].toString() + "\",";
    addPayload += "\"end_row_id\":\"" + rowIds[1].toString() + "\",";
    addPayload += "\"flags\":2}";  // STYLE flag

    Operation addOp = makeRangeAddOp(*workbook, rangeId, addPayload);
    applyOperation(*workbook, addOp);

    // Set a style first
    StyleBuffer style;
    style.setBold(true);
    Operation setOp = makeRangeSetStyleOp(*workbook, rangeId, style);
    applyOperation(*workbook, setOp);

    Sheet* sheet = workbook->getSheet(sheetId);
    Range* range = sheet->getRange(rangeId);
    ASSERT_TRUE(range->hasStyle());

    // Clear the style using makeClearStyleOp
    Operation clearOp = makeRangeClearStyleOp(*workbook, rangeId);
    ApplyResult result = applyOperation(*workbook, clearOp);

    EXPECT_EQ(result, ApplyResult::SUCCESS);
    EXPECT_FALSE(range->hasFlag(RangeFlags::STYLE));
    EXPECT_FALSE(range->hasStyle());
    EXPECT_EQ(range->getStyle(), nullptr);
}

TEST_F(CRDTRangeTest, InvalidBase64ReturnsInvalidPayload) {
    const ID rangeId = generate_id();

    // Add a range
    std::string addPayload = "{\"sheet_id\":\"" + sheetId.toString() + "\",";
    addPayload += "\"start_col_id\":\"" + colIds[0].toString() + "\",";
    addPayload += "\"start_row_id\":\"" + rowIds[0].toString() + "\",";
    addPayload += "\"end_col_id\":\"" + colIds[1].toString() + "\",";
    addPayload += "\"end_row_id\":\"" + rowIds[1].toString() + "\",";
    addPayload += "\"flags\":0}";

    Operation addOp = makeRangeAddOp(*workbook, rangeId, addPayload);
    applyOperation(*workbook, addOp);

    // Try to set with invalid base64
    std::string invalidPayload = "{\"style\":\"not-valid-base64!!!\"}";
    Operation styleOp = makeRangeSetStyleOp(*workbook, rangeId, invalidPayload);
    ApplyResult result = applyOperation(*workbook, styleOp);

    EXPECT_EQ(result, ApplyResult::INVALID_PAYLOAD);
}

}  // namespace
}  // namespace cells
