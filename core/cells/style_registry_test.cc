// =============================================================================
// Style Registry Tests
// =============================================================================

#include "core/cells/style_registry.h"

#include <gtest/gtest.h>

#include "core/cells/id.h"
#include "core/cells/style_types.h"

namespace cells {
namespace {

// Test M10: duplicate style returns same ID
TEST(StyleRegistryTest, RegisterDuplicateReturnsExistingId) {
    StyleRegistry registry;

    CellStyle style1;
    style1.bold = true;
    style1.bgColor = "#FF0000";

    CellStyle style2;
    style2.bold = true;
    style2.bgColor = "#FF0000";

    ID id1 = registry.registerStyle(style1);
    ID id2 = registry.registerStyle(style2);

    // Same style content should return same ID
    EXPECT_EQ(id1, id2);
    EXPECT_EQ(registry.size(), 1);
}

TEST(StyleRegistryTest, RegisterDifferentStylesGetDifferentIds) {
    StyleRegistry registry;

    CellStyle style1;
    style1.bold = true;

    CellStyle style2;
    style2.italic = true;

    ID id1 = registry.registerStyle(style1);
    ID id2 = registry.registerStyle(style2);

    // Different styles should get different IDs
    EXPECT_NE(id1, id2);
    EXPECT_EQ(registry.size(), 2);
}

TEST(StyleRegistryTest, RegisterWithProposedId) {
    StyleRegistry registry;
    ID proposedId = generate_id();

    CellStyle style;
    style.bold = true;

    ID id = registry.registerStyle(style, proposedId);

    EXPECT_EQ(id, proposedId);
    EXPECT_TRUE(registry.hasStyle(proposedId));
}

TEST(StyleRegistryTest, GetStyle) {
    StyleRegistry registry;

    CellStyle style;
    style.bold = true;
    style.bgColor = "#00FF00";

    ID id = registry.registerStyle(style);

    const CellStyle* retrieved = registry.getStyle(id);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_TRUE(retrieved->bold);
    EXPECT_EQ(retrieved->bgColor, "#00FF00");
}

TEST(StyleRegistryTest, GetStyleNotFound) {
    StyleRegistry registry;
    ID fakeId = generate_id();

    const CellStyle* retrieved = registry.getStyle(fakeId);
    EXPECT_EQ(retrieved, nullptr);
}

// Test M11: reference counting
TEST(StyleRegistryTest, ReferenceCountingBasic) {
    StyleRegistry registry;

    CellStyle style;
    style.bold = true;

    ID id = registry.registerStyle(style);

    // Initial refcount is 0
    EXPECT_EQ(registry.getRefCount(id), 0);

    // addRef increases count
    registry.addRef(id);
    EXPECT_EQ(registry.getRefCount(id), 1);

    registry.addRef(id);
    EXPECT_EQ(registry.getRefCount(id), 2);

    // release decreases count
    registry.release(id);
    EXPECT_EQ(registry.getRefCount(id), 1);

    registry.release(id);
    // Count hits 0 - style should be garbage collected
    EXPECT_FALSE(registry.hasStyle(id));
    EXPECT_EQ(registry.size(), 0);
}

TEST(StyleRegistryTest, ReleaseNonExistentId) {
    StyleRegistry registry;
    ID fakeId = generate_id();

    // Should not crash
    registry.release(fakeId);
    EXPECT_EQ(registry.getRefCount(fakeId), 0);
}

TEST(StyleRegistryTest, AddRefNonExistentId) {
    StyleRegistry registry;
    ID fakeId = generate_id();

    // Should not crash
    registry.addRef(fakeId);
    EXPECT_EQ(registry.getRefCount(fakeId), 0);
}

TEST(StyleRegistryTest, ReleaseNullId) {
    StyleRegistry registry;
    ID nullId;

    // Should not crash
    registry.release(nullId);
}

// Test M12: modifying shared style clones it
TEST(StyleRegistryTest, ModifySharedStyleClonesIt) {
    StyleRegistry registry;

    CellStyle style1;
    style1.bold = true;

    ID id1 = registry.registerStyle(style1);

    // Make it shared (refcount > 1)
    registry.addRef(id1);
    registry.addRef(id1);
    EXPECT_EQ(registry.getRefCount(id1), 2);

    // Modify the style
    CellStyle style2;
    style2.bold = true;
    style2.italic = true;

    ID id2 = registry.getOrCloneForModification(id1, style2);

    // Should get a new ID since style was shared
    EXPECT_NE(id1, id2);

    // Original style unchanged
    const CellStyle* original = registry.getStyle(id1);
    ASSERT_NE(original, nullptr);
    EXPECT_TRUE(original->bold);
    EXPECT_FALSE(original->italic);

    // New style has modifications
    const CellStyle* modified = registry.getStyle(id2);
    ASSERT_NE(modified, nullptr);
    EXPECT_TRUE(modified->bold);
    EXPECT_TRUE(modified->italic);
}

TEST(StyleRegistryTest, ModifyUnsharedStyleInPlace) {
    StyleRegistry registry;

    CellStyle style1;
    style1.bold = true;

    ID id1 = registry.registerStyle(style1);

    // Not shared (refcount <= 1)
    registry.addRef(id1);
    EXPECT_EQ(registry.getRefCount(id1), 1);

    // Modify the style
    CellStyle style2;
    style2.bold = true;
    style2.italic = true;

    ID id2 = registry.getOrCloneForModification(id1, style2);

    // Should return same ID since not shared
    EXPECT_EQ(id1, id2);

    // Style modified in place
    const CellStyle* modified = registry.getStyle(id1);
    ASSERT_NE(modified, nullptr);
    EXPECT_TRUE(modified->bold);
    EXPECT_TRUE(modified->italic);
}

TEST(StyleRegistryTest, ModifyToExistingStyleReusesId) {
    StyleRegistry registry;

    CellStyle styleA;
    styleA.bold = true;
    ID idA = registry.registerStyle(styleA);
    registry.addRef(idA);

    CellStyle styleB;
    styleB.italic = true;
    ID idB = registry.registerStyle(styleB);
    registry.addRef(idB);
    registry.addRef(idB);  // Make B shared

    // Modify B to match A
    CellStyle styleMatchesA;
    styleMatchesA.bold = true;

    ID result = registry.getOrCloneForModification(idB, styleMatchesA);

    // Should reuse idA since that style already exists
    EXPECT_EQ(result, idA);
}

TEST(StyleRegistryTest, RegisterStyleDirect) {
    StyleRegistry registry;
    ID specificId = generate_id();

    CellStyle style;
    style.bold = true;

    bool isNew = registry.registerStyleDirect(specificId, style);
    EXPECT_TRUE(isNew);
    EXPECT_TRUE(registry.hasStyle(specificId));

    // Register again with same ID - should update
    CellStyle style2;
    style2.italic = true;

    bool isNew2 = registry.registerStyleDirect(specificId, style2);
    EXPECT_FALSE(isNew2);

    // Style should be updated
    const CellStyle* retrieved = registry.getStyle(specificId);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_FALSE(retrieved->bold);
    EXPECT_TRUE(retrieved->italic);
}

TEST(StyleRegistryTest, Clear) {
    StyleRegistry registry;

    CellStyle style;
    style.bold = true;

    ID id = registry.registerStyle(style);
    registry.addRef(id);

    EXPECT_EQ(registry.size(), 1);

    registry.clear();

    EXPECT_EQ(registry.size(), 0);
    EXPECT_FALSE(registry.hasStyle(id));
    EXPECT_EQ(registry.getRefCount(id), 0);
}

// Hash function tests
TEST(CellStyleHashTest, EmptyStyleHashIsZero) {
    CellStyle style;
    EXPECT_EQ(style.hash(), 0);
}

TEST(CellStyleHashTest, DifferentStylesHaveDifferentHashes) {
    CellStyle style1;
    style1.bold = true;

    CellStyle style2;
    style2.italic = true;

    // Different styles should (usually) have different hashes
    EXPECT_NE(style1.hash(), style2.hash());
}

TEST(CellStyleHashTest, IdenticalStylesHaveSameHash) {
    CellStyle style1;
    style1.bold = true;
    style1.bgColor = "#FF0000";
    style1.fontSize = 12;

    CellStyle style2;
    style2.bold = true;
    style2.bgColor = "#FF0000";
    style2.fontSize = 12;

    EXPECT_EQ(style1.hash(), style2.hash());
}

TEST(CellStyleHashTest, BorderStylesAffectHash) {
    CellStyle style1;
    style1.border.top.style = BorderStyle::THIN;

    CellStyle style2;
    style2.border.top.style = BorderStyle::THICK;

    EXPECT_NE(style1.hash(), style2.hash());
}

TEST(CellStyleHashTest, AllPropertiesAffectHash) {
    CellStyle base;

    // Test each property changes the hash
    CellStyle withBold = base;
    withBold.bold = true;
    EXPECT_NE(base.hash(), withBold.hash());

    CellStyle withItalic = base;
    withItalic.italic = true;
    EXPECT_NE(base.hash(), withItalic.hash());

    CellStyle withUnderline = base;
    withUnderline.underline = true;
    EXPECT_NE(base.hash(), withUnderline.hash());

    CellStyle withBgColor = base;
    withBgColor.bgColor = "#000000";
    EXPECT_NE(base.hash(), withBgColor.hash());

    CellStyle withTextColor = base;
    withTextColor.textColor = "#FFFFFF";
    EXPECT_NE(base.hash(), withTextColor.hash());

    CellStyle withFontFamily = base;
    withFontFamily.fontFamily = "Arial";
    EXPECT_NE(base.hash(), withFontFamily.hash());

    CellStyle withFontSize = base;
    withFontSize.fontSize = 14;
    EXPECT_NE(base.hash(), withFontSize.hash());

    CellStyle withHAlign = base;
    withHAlign.hAlign = TextAlign::CENTER;
    EXPECT_NE(base.hash(), withHAlign.hash());

    CellStyle withVAlign = base;
    withVAlign.vAlign = VerticalAlign::TOP;
    EXPECT_NE(base.hash(), withVAlign.hash());
}

}  // namespace
}  // namespace cells
