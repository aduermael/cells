#include "core/cells/operation.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

TEST(OpTypeTest, OpTypeToStringAndBack) {
    // Test all operation types
    EXPECT_STREQ(opTypeToString(OpType::CELL_SET_VALUE), "CELL_SET_VALUE");
    EXPECT_STREQ(opTypeToString(OpType::CELL_CLEAR), "CELL_CLEAR");
    EXPECT_STREQ(opTypeToString(OpType::CELL_SET_STYLE), "CELL_SET_STYLE");
    EXPECT_STREQ(opTypeToString(OpType::DIM_INSERT_AXIS), "DIM_INSERT_AXIS");
    EXPECT_STREQ(opTypeToString(OpType::DIM_DELETE_AXIS), "DIM_DELETE_AXIS");
    EXPECT_STREQ(opTypeToString(OpType::DIM_MOVE_AXIS), "DIM_MOVE_AXIS");
    EXPECT_STREQ(opTypeToString(OpType::DIM_RESIZE_AXIS), "DIM_RESIZE_AXIS");
    EXPECT_STREQ(opTypeToString(OpType::DIM_RENAME_AXIS), "DIM_RENAME_AXIS");
    EXPECT_STREQ(opTypeToString(OpType::SHEET_CREATE), "SHEET_CREATE");
    EXPECT_STREQ(opTypeToString(OpType::SHEET_DELETE), "SHEET_DELETE");
    EXPECT_STREQ(opTypeToString(OpType::SHEET_RENAME), "SHEET_RENAME");

    // Test roundtrip
    EXPECT_EQ(stringToOpType("CELL_SET_VALUE"), OpType::CELL_SET_VALUE);
    EXPECT_EQ(stringToOpType("CELL_CLEAR"), OpType::CELL_CLEAR);
    EXPECT_EQ(stringToOpType("CELL_SET_STYLE"), OpType::CELL_SET_STYLE);
    EXPECT_EQ(stringToOpType("DIM_INSERT_AXIS"), OpType::DIM_INSERT_AXIS);
    EXPECT_EQ(stringToOpType("DIM_DELETE_AXIS"), OpType::DIM_DELETE_AXIS);
    EXPECT_EQ(stringToOpType("DIM_MOVE_AXIS"), OpType::DIM_MOVE_AXIS);
    EXPECT_EQ(stringToOpType("DIM_RESIZE_AXIS"), OpType::DIM_RESIZE_AXIS);
    EXPECT_EQ(stringToOpType("DIM_RENAME_AXIS"), OpType::DIM_RENAME_AXIS);
    EXPECT_EQ(stringToOpType("SHEET_CREATE"), OpType::SHEET_CREATE);
    EXPECT_EQ(stringToOpType("SHEET_DELETE"), OpType::SHEET_DELETE);
    EXPECT_EQ(stringToOpType("SHEET_RENAME"), OpType::SHEET_RENAME);
}

TEST(OpTypeTest, StringToOpTypeDefaultsOnInvalid) {
    EXPECT_EQ(stringToOpType("INVALID"), OpType::CELL_SET_VALUE);
    EXPECT_EQ(stringToOpType(""), OpType::CELL_SET_VALUE);
    EXPECT_EQ(stringToOpType("cell_set_value"), OpType::CELL_SET_VALUE);  // Case sensitive
}

TEST(OperationTest, DefaultConstructorCreatesNullOperation) {
    Operation op;
    EXPECT_TRUE(op.isNull());
    EXPECT_TRUE(op.hlc.isZero());
    EXPECT_TRUE(op.target_id.isNull());
    EXPECT_EQ(op.type, OpType::CELL_SET_VALUE);
    EXPECT_TRUE(op.payload.empty());
}

TEST(OperationTest, ParameterizedConstructor) {
    ID node("Kj7mXp2Q");
    HLC hlc(1705312200000, 5, node);
    ID target("nP6kR2mW");

    Operation op(hlc, OpType::CELL_SET_VALUE, target, R"({"type":"n","value":"42"})");

    EXPECT_FALSE(op.isNull());
    EXPECT_EQ(op.hlc, hlc);
    EXPECT_EQ(op.type, OpType::CELL_SET_VALUE);
    EXPECT_EQ(op.target_id.toString(), "nP6kR2mW");
    EXPECT_EQ(op.payload, R"({"type":"n","value":"42"})");
}

TEST(OperationTest, ComparisonBasedOnHLC) {
    ID node("Kj7mXp2Q");
    ID target("nP6kR2mW");

    Operation op1(HLC(1000, 0, node), OpType::CELL_SET_VALUE, target, "{}");
    Operation op2(HLC(2000, 0, node), OpType::CELL_SET_VALUE, target, "{}");
    Operation op3(HLC(1000, 1, node), OpType::CELL_SET_VALUE, target, "{}");

    EXPECT_LT(op1, op2);
    EXPECT_LT(op1, op3);
    EXPECT_LT(op3, op2);
}

TEST(OperationTest, ToStringAndFromString) {
    ID node("Kj7mXp2Q");
    HLC hlc(1705312200000, 42, node);
    ID target("nP6kR2mW");

    Operation original(hlc, OpType::CELL_SET_VALUE, target, R"({"type":"n","value":"42"})");

    std::string str = original.toString();
    EXPECT_EQ(str,
              R"(1705312200000.42.Kj7mXp2Q CELL_SET_VALUE nP6kR2mW {"type":"n","value":"42"})");

    Operation parsed = Operation::fromString(str);
    EXPECT_EQ(parsed.hlc, original.hlc);
    EXPECT_EQ(parsed.type, original.type);
    EXPECT_EQ(parsed.target_id.toString(), original.target_id.toString());
    EXPECT_EQ(parsed.payload, original.payload);
}

TEST(OperationTest, FromStringWithAllOpTypes) {
    std::vector<std::pair<std::string, OpType>> test_cases = {
        {"1000.0.Kj7mXp2Q CELL_SET_VALUE nP6kR2mW {}", OpType::CELL_SET_VALUE},
        {"1000.0.Kj7mXp2Q CELL_CLEAR nP6kR2mW {}", OpType::CELL_CLEAR},
        {"1000.0.Kj7mXp2Q CELL_SET_STYLE nP6kR2mW {}", OpType::CELL_SET_STYLE},
        {"1000.0.Kj7mXp2Q DIM_INSERT_AXIS nP6kR2mW {}", OpType::DIM_INSERT_AXIS},
        {"1000.0.Kj7mXp2Q DIM_DELETE_AXIS nP6kR2mW {}", OpType::DIM_DELETE_AXIS},
        {"1000.0.Kj7mXp2Q DIM_MOVE_AXIS nP6kR2mW {}", OpType::DIM_MOVE_AXIS},
        {"1000.0.Kj7mXp2Q DIM_RESIZE_AXIS nP6kR2mW {}", OpType::DIM_RESIZE_AXIS},
        {"1000.0.Kj7mXp2Q DIM_RENAME_AXIS nP6kR2mW {}", OpType::DIM_RENAME_AXIS},
        {"1000.0.Kj7mXp2Q SHEET_CREATE nP6kR2mW {}", OpType::SHEET_CREATE},
        {"1000.0.Kj7mXp2Q SHEET_DELETE nP6kR2mW {}", OpType::SHEET_DELETE},
        {"1000.0.Kj7mXp2Q SHEET_RENAME nP6kR2mW {}", OpType::SHEET_RENAME},
    };

    for (const auto& [str, expected_type] : test_cases) {
        Operation op = Operation::fromString(str);
        EXPECT_EQ(op.type, expected_type) << "Failed for: " << str;
    }
}

TEST(OperationTest, FromStringInvalidFormats) {
    // Missing spaces
    Operation op1 = Operation::fromString("1000.0.Kj7mXp2Q");
    EXPECT_TRUE(op1.isNull());

    // Missing target
    Operation op2 = Operation::fromString("1000.0.Kj7mXp2Q CELL_SET_VALUE");
    EXPECT_TRUE(op2.isNull());

    // Missing payload
    Operation op3 = Operation::fromString("1000.0.Kj7mXp2Q CELL_SET_VALUE nP6kR2mW");
    EXPECT_TRUE(op3.isNull());

    // Empty string
    Operation op4 = Operation::fromString("");
    EXPECT_TRUE(op4.isNull());
}

TEST(OperationTest, ToJSONAndFromJSON) {
    ID node("Kj7mXp2Q");
    HLC hlc(1705312200000, 42, node);
    ID target("nP6kR2mW");

    Operation original(hlc, OpType::CELL_SET_VALUE, target, R"({"type":"n","value":"42"})");

    std::string json = original.toJSON();

    // Verify JSON structure
    EXPECT_NE(json.find("\"hlc\":\"1705312200000.42.Kj7mXp2Q\""), std::string::npos);
    EXPECT_NE(json.find("\"op\":\"CELL_SET_VALUE\""), std::string::npos);
    EXPECT_NE(json.find("\"target\":\"nP6kR2mW\""), std::string::npos);
    EXPECT_NE(json.find("\"payload\":"), std::string::npos);

    // Parse back
    Operation parsed = Operation::fromJSON(json);
    EXPECT_EQ(parsed.hlc, original.hlc);
    EXPECT_EQ(parsed.type, original.type);
    EXPECT_EQ(parsed.target_id.toString(), original.target_id.toString());
}

TEST(OperationTest, FromJSONInvalidFormats) {
    // Missing required fields
    Operation op1 = Operation::fromJSON("{}");
    EXPECT_TRUE(op1.isNull());

    Operation op2 = Operation::fromJSON(R"({"hlc":"1000.0.Kj7mXp2Q"})");
    EXPECT_TRUE(op2.isNull());

    // Empty string
    Operation op3 = Operation::fromJSON("");
    EXPECT_TRUE(op3.isNull());
}

TEST(OperationTest, PayloadWithSpecialCharacters) {
    ID node("Kj7mXp2Q");
    HLC hlc(1000, 0, node);
    ID target("nP6kR2mW");

    // Test payload with quotes and special characters
    std::string payload = R"({"value":"Hello \"World\"\nNew line"})";
    Operation op(hlc, OpType::CELL_SET_VALUE, target, payload);

    std::string str = op.toString();
    Operation parsed = Operation::fromString(str);

    EXPECT_EQ(parsed.payload, payload);
}

TEST(OperationTest, EqualityOperator) {
    ID node("Kj7mXp2Q");
    HLC hlc(1000, 0, node);
    ID target("nP6kR2mW");

    Operation op1(hlc, OpType::CELL_SET_VALUE, target, "{}");
    Operation op2(hlc, OpType::CELL_SET_VALUE, target, "{}");
    Operation op3(hlc, OpType::CELL_CLEAR, target, "{}");  // Different type

    EXPECT_EQ(op1, op2);
    EXPECT_FALSE(op1 == op3);
}

TEST(OperationTest, RoundtripPreservesAllFields) {
    std::vector<Operation> test_ops;

    ID node("Kj7mXp2Q");
    ID target("nP6kR2mW");

    // Various operations
    test_ops.push_back(
        Operation(HLC(1000, 0, node), OpType::CELL_SET_VALUE, target, R"({"type":"n","v":42})"));
    test_ops.push_back(
        Operation(HLC(2000, 5, node), OpType::CELL_CLEAR, target, R"({"reason":"user"})"));
    test_ops.push_back(
        Operation(HLC(3000, 10, node), OpType::SHEET_RENAME, target, R"({"name":"Sheet2"})"));

    for (const auto& original : test_ops) {
        // Test toString/fromString roundtrip
        std::string str = original.toString();
        Operation parsed_str = Operation::fromString(str);
        EXPECT_EQ(original.hlc, parsed_str.hlc);
        EXPECT_EQ(original.type, parsed_str.type);
        EXPECT_EQ(original.target_id.toString(), parsed_str.target_id.toString());
        EXPECT_EQ(original.payload, parsed_str.payload);

        // Test toJSON/fromJSON roundtrip
        std::string json = original.toJSON();
        Operation parsed_json = Operation::fromJSON(json);
        EXPECT_EQ(original.hlc, parsed_json.hlc);
        EXPECT_EQ(original.type, parsed_json.type);
        EXPECT_EQ(original.target_id.toString(), parsed_json.target_id.toString());
    }
}

}  // namespace
}  // namespace cells
