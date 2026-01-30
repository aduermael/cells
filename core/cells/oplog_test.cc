#include "core/cells/oplog.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

class OpLogTest : public ::testing::Test {
protected:
    ID node{"Kj7mXp2Q"};
    ID target1{"nP6kR2mW"};
    ID target2{"mQ4sT8wK"};

    Operation makeOp(int64_t wall, uint32_t logical, const ID& target, const std::string& payload) {
        HLC hlc(wall, logical, node);
        return Operation(hlc, OpType::CELL_SET, target, payload);
    }
};

TEST_F(OpLogTest, EmptyLog) {
    OpLog log;
    EXPECT_TRUE(log.empty());
    EXPECT_EQ(log.size(), 0);
    EXPECT_TRUE(log.getCurrentHLC().isZero());
}

TEST_F(OpLogTest, AddSingleOperation) {
    OpLog log;
    Operation op = makeOp(1000, 0, target1, "{}");

    EXPECT_TRUE(log.addOperation(op));
    EXPECT_FALSE(log.empty());
    EXPECT_EQ(log.size(), 1);
    EXPECT_EQ(log.getCurrentHLC(), op.hlc);
}

TEST_F(OpLogTest, RejectsDuplicateHLC) {
    OpLog log;
    Operation op1 = makeOp(1000, 0, target1, "{}");
    Operation op2 = makeOp(1000, 0, target1, R"({"different":"payload"})");

    EXPECT_TRUE(log.addOperation(op1));
    EXPECT_FALSE(log.addOperation(op2));  // Same HLC, rejected
    EXPECT_EQ(log.size(), 1);
}

TEST_F(OpLogTest, MaintainsSortedOrder) {
    OpLog log;

    // Add out of order
    log.addOperation(makeOp(3000, 0, target1, "{}"));
    log.addOperation(makeOp(1000, 0, target1, "{}"));
    log.addOperation(makeOp(2000, 0, target1, "{}"));

    const auto& ops = log.getAllOperations();
    EXPECT_EQ(ops.size(), 3);
    EXPECT_EQ(ops[0].hlc.wall_time, 1000);
    EXPECT_EQ(ops[1].hlc.wall_time, 2000);
    EXPECT_EQ(ops[2].hlc.wall_time, 3000);
}

TEST_F(OpLogTest, GetOperationsSince) {
    OpLog log;
    log.addOperation(makeOp(1000, 0, target1, "{}"));
    log.addOperation(makeOp(2000, 0, target1, "{}"));
    log.addOperation(makeOp(3000, 0, target1, "{}"));

    // Get operations since HLC(1500, 0, node)
    HLC since(1500, 0, node);
    auto ops = log.getOperationsSince(since);

    EXPECT_EQ(ops.size(), 2);
    EXPECT_EQ(ops[0].hlc.wall_time, 2000);
    EXPECT_EQ(ops[1].hlc.wall_time, 3000);
}

TEST_F(OpLogTest, GetOperationsSinceZero) {
    OpLog log;
    log.addOperation(makeOp(1000, 0, target1, "{}"));
    log.addOperation(makeOp(2000, 0, target1, "{}"));

    HLC since;  // Zero HLC
    auto ops = log.getOperationsSince(since);

    EXPECT_EQ(ops.size(), 2);  // All operations
}

TEST_F(OpLogTest, GetOperationsForEntity) {
    OpLog log;
    log.addOperation(makeOp(1000, 0, target1, R"({"v":1})"));
    log.addOperation(makeOp(2000, 0, target2, R"({"v":2})"));
    log.addOperation(makeOp(3000, 0, target1, R"({"v":3})"));
    log.addOperation(makeOp(4000, 0, target2, R"({"v":4})"));

    auto ops1 = log.getOperationsForEntity(target1);
    EXPECT_EQ(ops1.size(), 2);
    EXPECT_EQ(ops1[0].payload, R"({"v":1})");
    EXPECT_EQ(ops1[1].payload, R"({"v":3})");

    auto ops2 = log.getOperationsForEntity(target2);
    EXPECT_EQ(ops2.size(), 2);
    EXPECT_EQ(ops2[0].payload, R"({"v":2})");
    EXPECT_EQ(ops2[1].payload, R"({"v":4})");
}

TEST_F(OpLogTest, GetOperationsForNonexistentEntity) {
    OpLog log;
    log.addOperation(makeOp(1000, 0, target1, "{}"));

    ID unknown("XXXXXXXX");
    auto ops = log.getOperationsForEntity(unknown);
    EXPECT_TRUE(ops.empty());
}

TEST_F(OpLogTest, GetLatestOperationForEntity) {
    OpLog log;
    log.addOperation(makeOp(1000, 0, target1, R"({"v":1})"));
    log.addOperation(makeOp(2000, 0, target1, R"({"v":2})"));
    log.addOperation(makeOp(3000, 0, target1, R"({"v":3})"));

    Operation latest = log.getLatestOperationForEntity(target1);
    EXPECT_FALSE(latest.isNull());
    EXPECT_EQ(latest.payload, R"({"v":3})");
}

TEST_F(OpLogTest, GetLatestOperationForNonexistentEntity) {
    OpLog log;
    log.addOperation(makeOp(1000, 0, target1, "{}"));

    ID unknown("XXXXXXXX");
    Operation latest = log.getLatestOperationForEntity(unknown);
    EXPECT_TRUE(latest.isNull());
}

TEST_F(OpLogTest, HasOperation) {
    OpLog log;
    HLC hlc(1000, 0, node);
    Operation op(hlc, OpType::CELL_SET, target1, "{}");

    EXPECT_FALSE(log.hasOperation(hlc));
    log.addOperation(op);
    EXPECT_TRUE(log.hasOperation(hlc));
}

TEST_F(OpLogTest, Clear) {
    OpLog log;
    log.addOperation(makeOp(1000, 0, target1, "{}"));
    log.addOperation(makeOp(2000, 0, target2, "{}"));

    EXPECT_EQ(log.size(), 2);
    log.clear();
    EXPECT_TRUE(log.empty());
    EXPECT_EQ(log.size(), 0);
    EXPECT_TRUE(log.getCurrentHLC().isZero());
}

TEST_F(OpLogTest, LogicalCounterOrdering) {
    OpLog log;

    // Same wall time, different logical counters
    log.addOperation(makeOp(1000, 2, target1, "{}"));
    log.addOperation(makeOp(1000, 0, target1, "{}"));
    log.addOperation(makeOp(1000, 1, target1, "{}"));

    const auto& ops = log.getAllOperations();
    EXPECT_EQ(ops.size(), 3);
    EXPECT_EQ(ops[0].hlc.logical, 0);
    EXPECT_EQ(ops[1].hlc.logical, 1);
    EXPECT_EQ(ops[2].hlc.logical, 2);
}

TEST_F(OpLogTest, MixedEntityOperationsOrdered) {
    OpLog log;

    // Add operations for different entities out of order
    log.addOperation(makeOp(3000, 0, target1, "{}"));
    log.addOperation(makeOp(1000, 0, target2, "{}"));
    log.addOperation(makeOp(4000, 0, target2, "{}"));
    log.addOperation(makeOp(2000, 0, target1, "{}"));

    // All operations should be globally ordered
    const auto& all = log.getAllOperations();
    EXPECT_EQ(all.size(), 4);
    EXPECT_EQ(all[0].hlc.wall_time, 1000);
    EXPECT_EQ(all[1].hlc.wall_time, 2000);
    EXPECT_EQ(all[2].hlc.wall_time, 3000);
    EXPECT_EQ(all[3].hlc.wall_time, 4000);

    // Per-entity operations should also be ordered
    auto t1_ops = log.getOperationsForEntity(target1);
    EXPECT_EQ(t1_ops.size(), 2);
    EXPECT_EQ(t1_ops[0].hlc.wall_time, 2000);
    EXPECT_EQ(t1_ops[1].hlc.wall_time, 3000);
}

TEST_F(OpLogTest, LargeLogPerformance) {
    OpLog log;

    // Add 1000 operations (wall times: 0, 100, 200, ..., 99900)
    for (int i = 0; i < 1000; i++) {
        log.addOperation(makeOp(i * 100, 0, target1, "{}"));
    }

    EXPECT_EQ(log.size(), 1000);

    // Query should be fast
    // since=50000, so we get operations with wall_time > 50000
    // That's 50100, 50200, ..., 99900 = (999 - 501 + 1) = 499 operations
    HLC since(50000, 0, node);
    auto ops = log.getOperationsSince(since);
    EXPECT_EQ(ops.size(), 499);
}

}  // namespace
}  // namespace cells
