#include "core/cells/hlc.h"

#include <thread>

#include "gtest/gtest.h"

namespace cells {
namespace {

TEST(HLCTest, DefaultConstructorCreatesZeroHLC) {
    HLC hlc;
    EXPECT_EQ(hlc.wall_time, 0);
    EXPECT_EQ(hlc.logical, 0);
    EXPECT_TRUE(hlc.node_id.isNull());
    EXPECT_TRUE(hlc.isZero());
}

TEST(HLCTest, ParameterizedConstructor) {
    ID node("Kj7mXp2Q");
    HLC hlc(1705312200000, 5, node);

    EXPECT_EQ(hlc.wall_time, 1705312200000);
    EXPECT_EQ(hlc.logical, 5);
    EXPECT_EQ(hlc.node_id.toString(), "Kj7mXp2Q");
    EXPECT_FALSE(hlc.isZero());
}

TEST(HLCTest, CompareWallTimeTakesPrecedence) {
    ID node("Kj7mXp2Q");
    HLC hlc1(1000, 10, node);
    HLC hlc2(2000, 5, node);

    EXPECT_LT(hlc1, hlc2);
    EXPECT_GT(hlc2, hlc1);
    EXPECT_EQ(hlc1.compare(hlc2), -1);
    EXPECT_EQ(hlc2.compare(hlc1), 1);
}

TEST(HLCTest, CompareLogicalWhenWallTimeEqual) {
    ID node("Kj7mXp2Q");
    HLC hlc1(1000, 5, node);
    HLC hlc2(1000, 10, node);

    EXPECT_LT(hlc1, hlc2);
    EXPECT_GT(hlc2, hlc1);
}

TEST(HLCTest, CompareNodeIdWhenBothEqual) {
    ID node1("AAAAAAAA");
    ID node2("ZZZZZZZZ");
    HLC hlc1(1000, 5, node1);
    HLC hlc2(1000, 5, node2);

    EXPECT_LT(hlc1, hlc2);
    EXPECT_GT(hlc2, hlc1);
}

TEST(HLCTest, EqualHLCs) {
    ID node("Kj7mXp2Q");
    HLC hlc1(1000, 5, node);
    HLC hlc2(1000, 5, node);

    EXPECT_EQ(hlc1, hlc2);
    EXPECT_EQ(hlc1.compare(hlc2), 0);
    EXPECT_TRUE(hlc1 <= hlc2);
    EXPECT_TRUE(hlc1 >= hlc2);
    EXPECT_FALSE(hlc1 < hlc2);
    EXPECT_FALSE(hlc1 > hlc2);
}

TEST(HLCTest, ToStringAndFromString) {
    ID node("Kj7mXp2Q");
    HLC hlc(1705312200000, 42, node);

    std::string str = hlc.toString();
    EXPECT_EQ(str, "1705312200000.42.Kj7mXp2Q");

    HLC parsed = HLC::fromString(str);
    EXPECT_EQ(parsed.wall_time, 1705312200000);
    EXPECT_EQ(parsed.logical, 42);
    EXPECT_EQ(parsed.node_id.toString(), "Kj7mXp2Q");
    EXPECT_EQ(hlc, parsed);
}

TEST(HLCTest, FromStringWithZeroLogical) {
    HLC hlc = HLC::fromString("1705312200000.0.N3f8hJ2w");

    EXPECT_EQ(hlc.wall_time, 1705312200000);
    EXPECT_EQ(hlc.logical, 0);
    EXPECT_EQ(hlc.node_id.toString(), "N3f8hJ2w");
}

TEST(HLCTest, FromStringInvalidFormats) {
    // Missing dots
    HLC hlc1 = HLC::fromString("1705312200000");
    EXPECT_TRUE(hlc1.isZero());

    // Only one dot
    HLC hlc2 = HLC::fromString("1705312200000.42");
    EXPECT_TRUE(hlc2.isZero());

    // Empty string
    HLC hlc3 = HLC::fromString("");
    EXPECT_TRUE(hlc3.isZero());
}

TEST(HLCTest, CurrentTimeIsReasonable) {
    int64_t now = current_time_ms();

    // Should be after 2024-01-01 (1704067200000 ms)
    EXPECT_GT(now, 1704067200000);

    // Should be before 2100-01-01 (4102444800000 ms)
    EXPECT_LT(now, 4102444800000);
}

TEST(HLCTest, GenerateInitialHLC) {
    ID node("Kj7mXp2Q");
    HLC hlc = generate_initial_hlc(node);

    EXPECT_GT(hlc.wall_time, 0);
    EXPECT_EQ(hlc.logical, 0);
    EXPECT_EQ(hlc.node_id.toString(), "Kj7mXp2Q");
}

TEST(HLCTest, GenerateHLCIncrementsLogicalWhenTimeSame) {
    ID node("Kj7mXp2Q");

    // Create an HLC with far-future timestamp
    HLC future(current_time_ms() + 1000000, 5, node);

    // Generate new HLC - should increment logical since wall time can't advance
    HLC next = generate_hlc(future, node);

    EXPECT_EQ(next.wall_time, future.wall_time);
    EXPECT_EQ(next.logical, 6);
    EXPECT_GT(next, future);
}

TEST(HLCTest, GenerateHLCResetsLogicalWhenTimeAdvances) {
    ID node("Kj7mXp2Q");

    // Create an HLC with past timestamp
    HLC past(1000, 100, node);

    // Generate new HLC - should use current time with logical 0
    HLC next = generate_hlc(past, node);

    EXPECT_GT(next.wall_time, past.wall_time);
    EXPECT_EQ(next.logical, 0);
    EXPECT_GT(next, past);
}

TEST(HLCTest, GenerateHLCAlwaysIncreases) {
    ID node("Kj7mXp2Q");
    HLC last = generate_initial_hlc(node);

    // Generate many HLCs and verify each is greater than the last
    for (int i = 0; i < 100; i++) {
        HLC next = generate_hlc(last, node);
        EXPECT_GT(next, last) << "HLC did not increase on iteration " << i;
        last = next;
    }
}

TEST(HLCTest, UpdateHLCWithOlderReceived) {
    ID local_node("LOCALnod");
    ID remote_node("REMOTEno");

    HLC local(2000, 5, local_node);
    HLC received(1000, 10, remote_node);

    HLC updated = update_hlc(local, received, local_node);

    // Should be greater than both
    EXPECT_GT(updated, local);
    EXPECT_GT(updated, received);
    EXPECT_EQ(updated.node_id.toString(), "LOCALnod");
}

TEST(HLCTest, UpdateHLCWithNewerReceived) {
    ID local_node("LOCALnod");
    ID remote_node("REMOTEno");

    HLC local(1000, 5, local_node);
    HLC received(2000, 10, remote_node);

    HLC updated = update_hlc(local, received, local_node);

    // Should be greater than both
    EXPECT_GT(updated, local);
    EXPECT_GT(updated, received);
    EXPECT_EQ(updated.node_id.toString(), "LOCALnod");
}

TEST(HLCTest, UpdateHLCWithSameWallTime) {
    ID local_node("LOCALnod");
    ID remote_node("REMOTEno");

    // Both at same wall time, local has lower logical
    int64_t wall = current_time_ms() + 100000;  // Future to ensure it's used
    HLC local(wall, 5, local_node);
    HLC received(wall, 10, remote_node);

    HLC updated = update_hlc(local, received, local_node);

    // Logical should be max(5, 10) + 1 = 11
    EXPECT_EQ(updated.wall_time, wall);
    EXPECT_EQ(updated.logical, 11);
    EXPECT_GT(updated, local);
    EXPECT_GT(updated, received);
}

TEST(HLCTest, HLCRoundtripPreservesValues) {
    // Test with various values
    std::vector<HLC> test_cases = {
        HLC(0, 0, ID()),
        HLC(1705312200000, 0, ID("N3f8hJ2w")),
        HLC(1705312200000, 42, ID("Kj7mXp2Q")),
        HLC(9999999999999, 4294967295, ID("zzzzzzzz")),
    };

    for (const auto& original : test_cases) {
        std::string str = original.toString();
        HLC parsed = HLC::fromString(str);

        EXPECT_EQ(original.wall_time, parsed.wall_time);
        EXPECT_EQ(original.logical, parsed.logical);
        EXPECT_EQ(original.node_id.toString(), parsed.node_id.toString());
    }
}

}  // namespace
}  // namespace cells
