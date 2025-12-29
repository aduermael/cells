#include "core/cells/axis_index.h"

#include <random>
#include <vector>

#include "core/cells/id.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

// Test fixture with helper methods
class AxisIndexTest : public ::testing::Test {
protected:
    // Helper to generate unique IDs
    ID makeId(int /* n */) { return generate_id(); }

    // Helper to verify tree invariants
    void verifyIndex(const AxisIndex& index) {
        EXPECT_TRUE(index.verify()) << "AxisIndex invariants violated";
    }
};

// ============================================================================
// Basic operations
// ============================================================================

TEST_F(AxisIndexTest, EmptyIndex) {
    AxisIndex index;
    EXPECT_EQ(index.count(), 0u);
    EXPECT_EQ(index.totalSize(), 0u);
    EXPECT_TRUE(index.empty());
    verifyIndex(index);
}

TEST_F(AxisIndexTest, AppendSingleAxis) {
    AxisIndex index;
    ID id = makeId(1);

    EXPECT_TRUE(index.append(id, 100));

    EXPECT_EQ(index.count(), 1u);
    EXPECT_EQ(index.totalSize(), 100u);
    EXPECT_FALSE(index.empty());
    EXPECT_TRUE(index.contains(id));

    auto size = index.getSize(id);
    ASSERT_TRUE(size.has_value());
    EXPECT_EQ(*size, 100u);

    verifyIndex(index);
}

TEST_F(AxisIndexTest, AppendMultipleAxes) {
    AxisIndex index;
    std::vector<ID> ids;

    for (int i = 0; i < 10; i++) {
        ID id = makeId(i);
        ids.push_back(id);
        EXPECT_TRUE(index.append(id, static_cast<uint32_t>((i + 1) * 10)));
    }

    EXPECT_EQ(index.count(), 10u);
    // Total: 10+20+30+40+50+60+70+80+90+100 = 550
    EXPECT_EQ(index.totalSize(), 550u);

    // Verify all axes exist
    for (const ID& id : ids) {
        EXPECT_TRUE(index.contains(id));
    }

    verifyIndex(index);
}

TEST_F(AxisIndexTest, InsertAtBeginning) {
    AxisIndex index;

    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);

    // Insert in reverse order at position 0
    EXPECT_TRUE(index.insert(id3, 0, 30));
    EXPECT_TRUE(index.insert(id2, 0, 20));
    EXPECT_TRUE(index.insert(id1, 0, 10));

    EXPECT_EQ(index.count(), 3u);
    EXPECT_EQ(index.totalSize(), 60u);

    // Verify order: id1, id2, id3
    auto axis0 = index.getAxisAt(0);
    auto axis1 = index.getAxisAt(1);
    auto axis2 = index.getAxisAt(2);

    ASSERT_TRUE(axis0.has_value());
    ASSERT_TRUE(axis1.has_value());
    ASSERT_TRUE(axis2.has_value());

    EXPECT_EQ(*axis0, id1);
    EXPECT_EQ(*axis1, id2);
    EXPECT_EQ(*axis2, id3);

    verifyIndex(index);
}

TEST_F(AxisIndexTest, InsertAtMiddle) {
    AxisIndex index;

    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);

    index.append(id1, 10);
    index.append(id3, 30);
    EXPECT_TRUE(index.insert(id2, 1, 20));  // Insert between id1 and id3

    EXPECT_EQ(index.count(), 3u);

    auto axis0 = index.getAxisAt(0);
    auto axis1 = index.getAxisAt(1);
    auto axis2 = index.getAxisAt(2);

    EXPECT_EQ(*axis0, id1);
    EXPECT_EQ(*axis1, id2);
    EXPECT_EQ(*axis2, id3);

    verifyIndex(index);
}

TEST_F(AxisIndexTest, InsertAtInvalidPosition) {
    AxisIndex index;
    index.append(makeId(1), 10);

    // Position 5 is invalid (only 0, 1 are valid)
    EXPECT_FALSE(index.insert(makeId(2), 5, 20));
    EXPECT_EQ(index.count(), 1u);

    verifyIndex(index);
}

// ============================================================================
// Removal operations
// ============================================================================

TEST_F(AxisIndexTest, RemoveOnlyAxis) {
    AxisIndex index;
    ID id = makeId(1);
    index.append(id, 100);

    EXPECT_TRUE(index.remove(id));
    EXPECT_EQ(index.count(), 0u);
    EXPECT_TRUE(index.empty());
    EXPECT_FALSE(index.contains(id));

    verifyIndex(index);
}

TEST_F(AxisIndexTest, RemoveFirst) {
    AxisIndex index;
    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);

    index.append(id1, 10);
    index.append(id2, 20);
    index.append(id3, 30);

    EXPECT_TRUE(index.remove(id1));
    EXPECT_EQ(index.count(), 2u);
    EXPECT_EQ(index.totalSize(), 50u);
    EXPECT_FALSE(index.contains(id1));

    auto axis0 = index.getAxisAt(0);
    EXPECT_EQ(*axis0, id2);

    verifyIndex(index);
}

TEST_F(AxisIndexTest, RemoveMiddle) {
    AxisIndex index;
    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);

    index.append(id1, 10);
    index.append(id2, 20);
    index.append(id3, 30);

    EXPECT_TRUE(index.remove(id2));
    EXPECT_EQ(index.count(), 2u);
    EXPECT_EQ(index.totalSize(), 40u);

    auto axis0 = index.getAxisAt(0);
    auto axis1 = index.getAxisAt(1);

    EXPECT_EQ(*axis0, id1);
    EXPECT_EQ(*axis1, id3);

    verifyIndex(index);
}

TEST_F(AxisIndexTest, RemoveLast) {
    AxisIndex index;
    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);

    index.append(id1, 10);
    index.append(id2, 20);
    index.append(id3, 30);

    EXPECT_TRUE(index.remove(id3));
    EXPECT_EQ(index.count(), 2u);
    EXPECT_EQ(index.totalSize(), 30u);

    auto axis1 = index.getAxisAt(1);
    EXPECT_EQ(*axis1, id2);

    verifyIndex(index);
}

TEST_F(AxisIndexTest, RemoveNonexistent) {
    AxisIndex index;
    index.append(makeId(1), 10);

    EXPECT_FALSE(index.remove(makeId(999)));
    EXPECT_EQ(index.count(), 1u);

    verifyIndex(index);
}

// ============================================================================
// pixelToAxis tests
// ============================================================================

TEST_F(AxisIndexTest, PixelToAxisSimple) {
    AxisIndex index;

    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);

    // Create axes: [100, 50, 75] pixels
    index.append(id1, 100);
    index.append(id2, 50);
    index.append(id3, 75);

    // Offset 0 -> first axis, offset 0 within
    auto r0 = index.pixelToAxis(0);
    ASSERT_TRUE(r0.has_value());
    EXPECT_EQ(r0->axisId, id1);
    EXPECT_EQ(r0->offsetInAxis, 0u);
    EXPECT_EQ(r0->position, 0u);

    // Offset 50 -> first axis, offset 50 within
    auto r50 = index.pixelToAxis(50);
    ASSERT_TRUE(r50.has_value());
    EXPECT_EQ(r50->axisId, id1);
    EXPECT_EQ(r50->offsetInAxis, 50u);
    EXPECT_EQ(r50->position, 0u);

    // Offset 100 -> second axis, offset 0 within
    auto r100 = index.pixelToAxis(100);
    ASSERT_TRUE(r100.has_value());
    EXPECT_EQ(r100->axisId, id2);
    EXPECT_EQ(r100->offsetInAxis, 0u);
    EXPECT_EQ(r100->position, 1u);

    // Offset 120 -> second axis, offset 20 within
    auto r120 = index.pixelToAxis(120);
    ASSERT_TRUE(r120.has_value());
    EXPECT_EQ(r120->axisId, id2);
    EXPECT_EQ(r120->offsetInAxis, 20u);
    EXPECT_EQ(r120->position, 1u);

    // Offset 150 -> third axis, offset 0 within
    auto r150 = index.pixelToAxis(150);
    ASSERT_TRUE(r150.has_value());
    EXPECT_EQ(r150->axisId, id3);
    EXPECT_EQ(r150->offsetInAxis, 0u);
    EXPECT_EQ(r150->position, 2u);

    // Offset 224 -> third axis, offset 74 within (last valid)
    auto r224 = index.pixelToAxis(224);
    ASSERT_TRUE(r224.has_value());
    EXPECT_EQ(r224->axisId, id3);
    EXPECT_EQ(r224->offsetInAxis, 74u);
    EXPECT_EQ(r224->position, 2u);

    // Offset 225 -> out of range
    auto r225 = index.pixelToAxis(225);
    EXPECT_FALSE(r225.has_value());
}

TEST_F(AxisIndexTest, PixelToAxisManyNodes) {
    AxisIndex index;
    std::vector<ID> ids;

    // Create 100 axes, each 10 pixels wide
    for (int i = 0; i < 100; i++) {
        ID id = makeId(i);
        ids.push_back(id);
        index.append(id, 10);
    }

    EXPECT_EQ(index.totalSize(), 1000u);

    // Test various offsets
    for (uint32_t offset = 0; offset < 1000; offset += 37) {
        auto result = index.pixelToAxis(offset);
        ASSERT_TRUE(result.has_value()) << "Offset " << offset;

        size_t expectedPosition = offset / 10;
        uint32_t expectedOffsetInAxis = offset % 10;

        EXPECT_EQ(result->axisId, ids[expectedPosition]) << "Offset " << offset;
        EXPECT_EQ(result->position, expectedPosition) << "Offset " << offset;
        EXPECT_EQ(result->offsetInAxis, expectedOffsetInAxis) << "Offset " << offset;
    }
}

TEST_F(AxisIndexTest, PixelToAxisEmptyIndex) {
    AxisIndex index;
    auto result = index.pixelToAxis(0);
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// axisToPixel tests
// ============================================================================

TEST_F(AxisIndexTest, AxisToPixel) {
    AxisIndex index;

    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);
    ID id4 = makeId(4);

    // Create axes: [100, 50, 75, 25] pixels
    index.append(id1, 100);
    index.append(id2, 50);
    index.append(id3, 75);
    index.append(id4, 25);

    auto offset1 = index.axisToPixel(id1);
    auto offset2 = index.axisToPixel(id2);
    auto offset3 = index.axisToPixel(id3);
    auto offset4 = index.axisToPixel(id4);

    ASSERT_TRUE(offset1.has_value());
    ASSERT_TRUE(offset2.has_value());
    ASSERT_TRUE(offset3.has_value());
    ASSERT_TRUE(offset4.has_value());

    EXPECT_EQ(*offset1, 0u);
    EXPECT_EQ(*offset2, 100u);
    EXPECT_EQ(*offset3, 150u);
    EXPECT_EQ(*offset4, 225u);
}

TEST_F(AxisIndexTest, AxisToPixelNotFound) {
    AxisIndex index;
    index.append(makeId(1), 100);

    auto offset = index.axisToPixel(makeId(999));
    EXPECT_FALSE(offset.has_value());
}

// ============================================================================
// getPosition tests
// ============================================================================

TEST_F(AxisIndexTest, GetPosition) {
    AxisIndex index;
    std::vector<ID> ids;

    for (int i = 0; i < 20; i++) {
        ID id = makeId(i);
        ids.push_back(id);
        index.append(id, 10);
    }

    for (size_t i = 0; i < ids.size(); i++) {
        auto pos = index.getPosition(ids[i]);
        ASSERT_TRUE(pos.has_value());
        EXPECT_EQ(*pos, i);
    }

    auto notFound = index.getPosition(makeId(999));
    EXPECT_FALSE(notFound.has_value());
}

// ============================================================================
// getSize tests
// ============================================================================

TEST_F(AxisIndexTest, GetSize) {
    AxisIndex index;
    ID id1 = makeId(1);
    ID id2 = makeId(2);

    index.append(id1, 100);
    index.append(id2, 200);

    auto size1 = index.getSize(id1);
    auto size2 = index.getSize(id2);

    ASSERT_TRUE(size1.has_value());
    ASSERT_TRUE(size2.has_value());

    EXPECT_EQ(*size1, 100u);
    EXPECT_EQ(*size2, 200u);

    auto notFound = index.getSize(makeId(999));
    EXPECT_FALSE(notFound.has_value());
}

// ============================================================================
// getAxisAt tests
// ============================================================================

TEST_F(AxisIndexTest, GetAxisAt) {
    AxisIndex index;
    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);

    index.append(id1, 10);
    index.append(id2, 20);
    index.append(id3, 30);

    auto axis0 = index.getAxisAt(0);
    auto axis1 = index.getAxisAt(1);
    auto axis2 = index.getAxisAt(2);
    auto axis3 = index.getAxisAt(3);  // Out of range

    ASSERT_TRUE(axis0.has_value());
    ASSERT_TRUE(axis1.has_value());
    ASSERT_TRUE(axis2.has_value());
    EXPECT_FALSE(axis3.has_value());

    EXPECT_EQ(*axis0, id1);
    EXPECT_EQ(*axis1, id2);
    EXPECT_EQ(*axis2, id3);
}

// ============================================================================
// resize tests
// ============================================================================

TEST_F(AxisIndexTest, Resize) {
    AxisIndex index;

    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);

    index.append(id1, 100);
    index.append(id2, 50);
    index.append(id3, 75);

    EXPECT_EQ(index.totalSize(), 225u);

    // Resize second axis from 50 to 200
    EXPECT_TRUE(index.resize(id2, 200));

    auto size = index.getSize(id2);
    ASSERT_TRUE(size.has_value());
    EXPECT_EQ(*size, 200u);
    EXPECT_EQ(index.totalSize(), 375u);

    // Verify pixel offsets are correct
    auto offset1 = index.axisToPixel(id1);
    auto offset2 = index.axisToPixel(id2);
    auto offset3 = index.axisToPixel(id3);

    EXPECT_EQ(*offset1, 0u);
    EXPECT_EQ(*offset2, 100u);
    EXPECT_EQ(*offset3, 300u);

    verifyIndex(index);
}

TEST_F(AxisIndexTest, ResizeNonexistent) {
    AxisIndex index;
    index.append(makeId(1), 100);

    EXPECT_FALSE(index.resize(makeId(999), 200));
    EXPECT_EQ(index.totalSize(), 100u);
}

TEST_F(AxisIndexTest, ResizeToZero) {
    AxisIndex index;
    ID id1 = makeId(1);
    ID id2 = makeId(2);

    index.append(id1, 100);
    index.append(id2, 50);

    EXPECT_TRUE(index.resize(id1, 0));

    EXPECT_EQ(index.totalSize(), 50u);
    EXPECT_EQ(*index.axisToPixel(id1), 0u);
    EXPECT_EQ(*index.axisToPixel(id2), 0u);  // Starts at 0 now

    verifyIndex(index);
}

// ============================================================================
// move tests
// ============================================================================

TEST_F(AxisIndexTest, MoveForward) {
    AxisIndex index;

    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);
    ID id4 = makeId(4);

    index.append(id1, 10);
    index.append(id2, 20);
    index.append(id3, 30);
    index.append(id4, 40);

    // Move first axis to position 2 (after id3)
    EXPECT_TRUE(index.move(id1, 2));

    // New order: id2, id3, id1, id4
    EXPECT_EQ(*index.getAxisAt(0), id2);
    EXPECT_EQ(*index.getAxisAt(1), id3);
    EXPECT_EQ(*index.getAxisAt(2), id1);
    EXPECT_EQ(*index.getAxisAt(3), id4);

    EXPECT_EQ(index.totalSize(), 100u);
    verifyIndex(index);
}

TEST_F(AxisIndexTest, MoveBackward) {
    AxisIndex index;

    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);
    ID id4 = makeId(4);

    index.append(id1, 10);
    index.append(id2, 20);
    index.append(id3, 30);
    index.append(id4, 40);

    // Move last axis to position 1 (before id2)
    EXPECT_TRUE(index.move(id4, 1));

    // New order: id1, id4, id2, id3
    EXPECT_EQ(*index.getAxisAt(0), id1);
    EXPECT_EQ(*index.getAxisAt(1), id4);
    EXPECT_EQ(*index.getAxisAt(2), id2);
    EXPECT_EQ(*index.getAxisAt(3), id3);

    verifyIndex(index);
}

TEST_F(AxisIndexTest, MoveToSamePosition) {
    AxisIndex index;

    ID id1 = makeId(1);
    ID id2 = makeId(2);

    index.append(id1, 10);
    index.append(id2, 20);

    EXPECT_TRUE(index.move(id1, 0));  // Already at position 0

    EXPECT_EQ(*index.getAxisAt(0), id1);
    EXPECT_EQ(*index.getAxisAt(1), id2);

    verifyIndex(index);
}

TEST_F(AxisIndexTest, MoveNonexistent) {
    AxisIndex index;
    index.append(makeId(1), 10);
    index.append(makeId(2), 20);

    EXPECT_FALSE(index.move(makeId(999), 0));
}

// ============================================================================
// forEach tests
// ============================================================================

TEST_F(AxisIndexTest, ForEach) {
    AxisIndex index;
    std::vector<ID> ids;

    for (int i = 0; i < 10; i++) {
        ID id = makeId(i);
        ids.push_back(id);
        index.append(id, static_cast<uint32_t>((i + 1) * 10));
    }

    std::vector<ID> visitedIds;
    std::vector<size_t> positions;
    std::vector<uint32_t> sizes;
    std::vector<uint32_t> offsets;

    index.forEach([&](const ID& id, size_t pos, uint32_t size, uint32_t offset) {
        visitedIds.push_back(id);
        positions.push_back(pos);
        sizes.push_back(size);
        offsets.push_back(offset);
    });

    EXPECT_EQ(visitedIds.size(), 10u);

    uint32_t expectedOffset = 0;
    for (size_t i = 0; i < 10; i++) {
        EXPECT_EQ(visitedIds[i], ids[i]);
        EXPECT_EQ(positions[i], i);
        EXPECT_EQ(sizes[i], static_cast<uint32_t>((i + 1) * 10));
        EXPECT_EQ(offsets[i], expectedOffset);
        expectedOffset += sizes[i];
    }
}

// ============================================================================
// clear tests
// ============================================================================

TEST_F(AxisIndexTest, Clear) {
    AxisIndex index;

    for (int i = 0; i < 100; i++) {
        index.append(makeId(i), 10);
    }

    EXPECT_EQ(index.count(), 100u);

    index.clear();

    EXPECT_EQ(index.count(), 0u);
    EXPECT_TRUE(index.empty());
    EXPECT_EQ(index.totalSize(), 0u);

    verifyIndex(index);
}

// ============================================================================
// Stress tests
// ============================================================================

TEST_F(AxisIndexTest, ManyInsertions) {
    AxisIndex index;

    for (int i = 0; i < 1000; i++) {
        index.append(makeId(i), 100);
    }

    EXPECT_EQ(index.count(), 1000u);
    EXPECT_EQ(index.totalSize(), 100000u);
    verifyIndex(index);
}

TEST_F(AxisIndexTest, ManyDeletions) {
    AxisIndex index;
    std::vector<ID> ids;

    for (int i = 0; i < 100; i++) {
        ID id = makeId(i);
        ids.push_back(id);
        index.append(id, 10);
    }

    // Remove in random order
    std::mt19937 rng(42);
    std::shuffle(ids.begin(), ids.end(), rng);

    for (const ID& id : ids) {
        EXPECT_TRUE(index.remove(id));
        verifyIndex(index);
    }

    EXPECT_TRUE(index.empty());
}

TEST_F(AxisIndexTest, RandomOperations) {
    AxisIndex index;
    std::vector<ID> ids;
    std::mt19937 rng(12345);

    for (int i = 0; i < 500; i++) {
        int op = std::uniform_int_distribution<>(0, 2)(rng);

        if (op == 0 || ids.empty()) {
            // Insert
            ID id = makeId(i);
            ids.push_back(id);
            uint32_t size = std::uniform_int_distribution<uint32_t>(1, 1000)(rng);
            index.append(id, size);
        } else if (op == 1 && !ids.empty()) {
            // Remove random
            size_t idx = std::uniform_int_distribution<size_t>(0, ids.size() - 1)(rng);
            EXPECT_TRUE(index.remove(ids[idx]));
            ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(idx));
        } else if (!ids.empty()) {
            // Resize
            size_t idx = std::uniform_int_distribution<size_t>(0, ids.size() - 1)(rng);
            uint32_t newSize = std::uniform_int_distribution<uint32_t>(1, 1000)(rng);
            EXPECT_TRUE(index.resize(ids[idx], newSize));
        }

        verifyIndex(index);
    }

    EXPECT_EQ(index.count(), ids.size());
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_F(AxisIndexTest, ZeroSizeAxes) {
    AxisIndex index;

    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);

    index.append(id1, 0);
    index.append(id2, 100);
    index.append(id3, 0);

    EXPECT_EQ(index.count(), 3u);
    EXPECT_EQ(index.totalSize(), 100u);

    // pixelToAxis should skip zero-size axes
    auto r0 = index.pixelToAxis(0);
    ASSERT_TRUE(r0.has_value());
    EXPECT_EQ(r0->axisId, id2);  // Skip zero-size first axis
    EXPECT_EQ(r0->offsetInAxis, 0u);
    EXPECT_EQ(r0->position, 1u);

    verifyIndex(index);
}

TEST_F(AxisIndexTest, LargeSizes) {
    AxisIndex index;

    ID id1 = makeId(1);
    ID id2 = makeId(2);
    ID id3 = makeId(3);

    // Use sizes that might overflow 16-bit
    index.append(id1, 100000);
    index.append(id2, 200000);
    index.append(id3, 300000);

    EXPECT_EQ(index.totalSize(), 600000u);

    auto r = index.pixelToAxis(350000);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->axisId, id3);
    EXPECT_EQ(r->offsetInAxis, 50000u);
    EXPECT_EQ(r->position, 2u);

    verifyIndex(index);
}

// ============================================================================
// Bidirectional conversion tests
// ============================================================================

TEST_F(AxisIndexTest, BidirectionalConversion) {
    AxisIndex index;
    std::vector<ID> ids;

    // Create axes with various sizes
    std::vector<uint32_t> sizes = {50, 100, 25, 75, 200, 10, 150};
    for (size_t i = 0; i < sizes.size(); i++) {
        ID id = makeId(static_cast<int>(i));
        ids.push_back(id);
        index.append(id, sizes[i]);
    }

    // For each axis, verify axisToPixel and pixelToAxis are consistent
    for (size_t i = 0; i < ids.size(); i++) {
        auto offset = index.axisToPixel(ids[i]);
        ASSERT_TRUE(offset.has_value());

        auto result = index.pixelToAxis(*offset);
        ASSERT_TRUE(result.has_value());

        EXPECT_EQ(result->axisId, ids[i]) << "Axis " << i;
        EXPECT_EQ(result->offsetInAxis, 0u) << "Axis " << i;
        EXPECT_EQ(result->position, i) << "Axis " << i;
    }
}

}  // namespace
}  // namespace cells
