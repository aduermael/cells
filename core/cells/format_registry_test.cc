// =============================================================================
// Format Registry Tests
// =============================================================================

#include "core/cells/format_registry.h"

#include <gtest/gtest.h>

#include "core/cells/id.h"

namespace cells {
namespace {

// Test: duplicate format code returns same ID
TEST(FormatRegistryTest, FindOrRegisterDuplicateReturnsExistingId) {
    FormatRegistry registry;

    bool wasCreated1 = false;
    ID id1 = registry.findOrRegisterFormat("#,##0.00", ID(), &wasCreated1);
    EXPECT_TRUE(wasCreated1);

    bool wasCreated2 = false;
    ID id2 = registry.findOrRegisterFormat("#,##0.00", ID(), &wasCreated2);
    EXPECT_FALSE(wasCreated2);

    // Same format code should return same ID
    EXPECT_EQ(id1, id2);
    EXPECT_EQ(registry.size(), 1);
}

TEST(FormatRegistryTest, FindOrRegisterDifferentCodesGetDifferentIds) {
    FormatRegistry registry;

    ID id1 = registry.findOrRegisterFormat("#,##0.00");
    ID id2 = registry.findOrRegisterFormat("0.00%");

    // Different format codes should get different IDs
    EXPECT_NE(id1, id2);
    EXPECT_EQ(registry.size(), 2);
}

TEST(FormatRegistryTest, RegisterWithProposedId) {
    FormatRegistry registry;
    ID proposedId = generate_id();

    bool wasCreated = false;
    ID id = registry.findOrRegisterFormat("#,##0.00", proposedId, &wasCreated);

    EXPECT_EQ(id, proposedId);
    EXPECT_TRUE(wasCreated);
    EXPECT_TRUE(registry.hasFormat(proposedId));
}

TEST(FormatRegistryTest, RegisterFormatDirect) {
    FormatRegistry registry;
    ID formatId = generate_id();

    bool isNew = registry.registerFormat(formatId, "#,##0.00");
    EXPECT_TRUE(isNew);
    EXPECT_TRUE(registry.hasFormat(formatId));
    EXPECT_EQ(registry.getFormatCode(formatId), "#,##0.00");

    // Register again with same ID - should update
    bool isNew2 = registry.registerFormat(formatId, "0.00%");
    EXPECT_FALSE(isNew2);

    // Format should be updated
    EXPECT_EQ(registry.getFormatCode(formatId), "0.00%");
}

TEST(FormatRegistryTest, GetFormatCode) {
    FormatRegistry registry;

    ID id = registry.findOrRegisterFormat("#,##0.00");

    std::string code = registry.getFormatCode(id);
    EXPECT_EQ(code, "#,##0.00");
}

TEST(FormatRegistryTest, GetFormatCodeNotFound) {
    FormatRegistry registry;
    ID fakeId = generate_id();

    std::string code = registry.getFormatCode(fakeId);
    EXPECT_EQ(code, "");
}

// Test: reference counting
TEST(FormatRegistryTest, ReferenceCountingBasic) {
    FormatRegistry registry;

    ID id = registry.findOrRegisterFormat("#,##0.00");

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
    // Count hits 0 - format should be garbage collected
    EXPECT_FALSE(registry.hasFormat(id));
    EXPECT_EQ(registry.size(), 0);
}

TEST(FormatRegistryTest, ReleaseNonExistentId) {
    FormatRegistry registry;
    ID fakeId = generate_id();

    // Should not crash
    registry.release(fakeId);
    EXPECT_EQ(registry.getRefCount(fakeId), 0);
}

TEST(FormatRegistryTest, AddRefNonExistentId) {
    FormatRegistry registry;
    ID fakeId = generate_id();

    // Should not crash
    registry.addRef(fakeId);
    EXPECT_EQ(registry.getRefCount(fakeId), 0);
}

TEST(FormatRegistryTest, ReleaseNullId) {
    FormatRegistry registry;
    ID nullId;

    // Should not crash
    registry.release(nullId);
}

TEST(FormatRegistryTest, Clear) {
    FormatRegistry registry;

    ID id = registry.findOrRegisterFormat("#,##0.00");
    registry.addRef(id);

    EXPECT_EQ(registry.size(), 1);

    registry.clear();

    EXPECT_EQ(registry.size(), 0);
    EXPECT_FALSE(registry.hasFormat(id));
    EXPECT_EQ(registry.getRefCount(id), 0);
}

// Test: deduplication after GC
TEST(FormatRegistryTest, DeduplicationAfterGarbageCollection) {
    FormatRegistry registry;

    // Register a format and add a reference
    ID id1 = registry.findOrRegisterFormat("#,##0.00");
    registry.addRef(id1);

    // Release it - should be garbage collected
    registry.release(id1);
    EXPECT_FALSE(registry.hasFormat(id1));

    // Register the same format code again - should get a new ID
    ID id2 = registry.findOrRegisterFormat("#,##0.00");

    // IDs should be different because the original was GC'd
    EXPECT_NE(id1, id2);
    EXPECT_TRUE(registry.hasFormat(id2));
}

// Test: multiple references prevent GC
TEST(FormatRegistryTest, MultipleReferencesPreventGC) {
    FormatRegistry registry;

    ID id = registry.findOrRegisterFormat("#,##0.00");
    registry.addRef(id);
    registry.addRef(id);
    registry.addRef(id);  // refcount = 3

    registry.release(id);  // refcount = 2
    EXPECT_TRUE(registry.hasFormat(id));

    registry.release(id);  // refcount = 1
    EXPECT_TRUE(registry.hasFormat(id));

    registry.release(id);  // refcount = 0, GC
    EXPECT_FALSE(registry.hasFormat(id));
}

// Test: GetFormats returns all formats
TEST(FormatRegistryTest, GetFormats) {
    FormatRegistry registry;

    ID id1 = registry.findOrRegisterFormat("#,##0.00");
    ID id2 = registry.findOrRegisterFormat("0.00%");
    ID id3 = registry.findOrRegisterFormat("$#,##0.00");

    const auto& formats = registry.getFormats();
    EXPECT_EQ(formats.size(), 3);
    EXPECT_EQ(formats.at(id1), "#,##0.00");
    EXPECT_EQ(formats.at(id2), "0.00%");
    EXPECT_EQ(formats.at(id3), "$#,##0.00");
}

// Test: register with existing code reuses ID
TEST(FormatRegistryTest, RegisterWithExistingCodeReusesId) {
    FormatRegistry registry;

    ID id1 = registry.findOrRegisterFormat("#,##0.00");

    // Propose a different ID but same code - should return existing ID
    ID proposedId = generate_id();
    bool wasCreated = false;
    ID id2 = registry.findOrRegisterFormat("#,##0.00", proposedId, &wasCreated);

    EXPECT_EQ(id1, id2);
    EXPECT_FALSE(wasCreated);
    EXPECT_EQ(registry.size(), 1);
}

}  // namespace
}  // namespace cells
