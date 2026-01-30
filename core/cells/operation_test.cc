#include "core/cells/operation.h"

#include "gtest/gtest.h"

namespace cells {
namespace {

TEST(OpTypeTest, OpTypeToStringAndBack) {
    // Test all operation types
    EXPECT_STREQ(opTypeToString(OpType::CELL_SET), "CELL_SET");
    EXPECT_STREQ(opTypeToString(OpType::CELL_DELETE), "CELL_DELETE");
    EXPECT_STREQ(opTypeToString(OpType::COL_SET), "COL_SET");
    EXPECT_STREQ(opTypeToString(OpType::COL_DELETE), "COL_DELETE");
    EXPECT_STREQ(opTypeToString(OpType::ROW_SET), "ROW_SET");
    EXPECT_STREQ(opTypeToString(OpType::ROW_DELETE), "ROW_DELETE");
    EXPECT_STREQ(opTypeToString(OpType::SHEET_SET), "SHEET_SET");
    EXPECT_STREQ(opTypeToString(OpType::SHEET_DELETE), "SHEET_DELETE");
    EXPECT_STREQ(opTypeToString(OpType::WORKBOOK_SET), "WORKBOOK_SET");
    EXPECT_STREQ(opTypeToString(OpType::NAMED_RANGE_SET), "NAMED_RANGE_SET");
    EXPECT_STREQ(opTypeToString(OpType::NAMED_RANGE_DELETE), "NAMED_RANGE_DELETE");
    EXPECT_STREQ(opTypeToString(OpType::RANGE_SET), "RANGE_SET");
    EXPECT_STREQ(opTypeToString(OpType::RANGE_DELETE), "RANGE_DELETE");

    // Test roundtrip
    EXPECT_EQ(stringToOpType("CELL_SET"), OpType::CELL_SET);
    EXPECT_EQ(stringToOpType("CELL_DELETE"), OpType::CELL_DELETE);
    EXPECT_EQ(stringToOpType("COL_SET"), OpType::COL_SET);
    EXPECT_EQ(stringToOpType("COL_DELETE"), OpType::COL_DELETE);
    EXPECT_EQ(stringToOpType("ROW_SET"), OpType::ROW_SET);
    EXPECT_EQ(stringToOpType("ROW_DELETE"), OpType::ROW_DELETE);
    EXPECT_EQ(stringToOpType("SHEET_SET"), OpType::SHEET_SET);
    EXPECT_EQ(stringToOpType("SHEET_DELETE"), OpType::SHEET_DELETE);
    EXPECT_EQ(stringToOpType("WORKBOOK_SET"), OpType::WORKBOOK_SET);
    EXPECT_EQ(stringToOpType("NAMED_RANGE_SET"), OpType::NAMED_RANGE_SET);
    EXPECT_EQ(stringToOpType("NAMED_RANGE_DELETE"), OpType::NAMED_RANGE_DELETE);
    EXPECT_EQ(stringToOpType("RANGE_SET"), OpType::RANGE_SET);
    EXPECT_EQ(stringToOpType("RANGE_DELETE"), OpType::RANGE_DELETE);
}

TEST(OpTypeTest, StringToOpTypeDefaultsOnInvalid) {
    EXPECT_EQ(stringToOpType("INVALID"), OpType::CELL_SET);
    EXPECT_EQ(stringToOpType(""), OpType::CELL_SET);
    EXPECT_EQ(stringToOpType("cell_set"), OpType::CELL_SET);  // Case sensitive
}

TEST(OperationTest, DefaultConstructorCreatesNullOperation) {
    Operation op;
    EXPECT_TRUE(op.isNull());
    EXPECT_TRUE(op.hlc.isZero());
    EXPECT_TRUE(op.target_id.isNull());
    EXPECT_EQ(op.type, OpType::CELL_SET);
    EXPECT_TRUE(op.payload.empty());
}

TEST(OperationTest, ParameterizedConstructor) {
    ID node("Kj7mXp2Q");
    HLC hlc(1705312200000, 5, node);
    ID target("nP6kR2mW");

    Operation op(hlc, OpType::CELL_SET, target, R"({"t":"n","v":"42"})");

    EXPECT_FALSE(op.isNull());
    EXPECT_EQ(op.hlc, hlc);
    EXPECT_EQ(op.type, OpType::CELL_SET);
    EXPECT_EQ(op.target_id.toString(), "nP6kR2mW");
    EXPECT_EQ(op.payload, R"({"t":"n","v":"42"})");
}

TEST(OperationTest, ComparisonBasedOnHLC) {
    ID node("Kj7mXp2Q");
    ID target("nP6kR2mW");

    Operation op1(HLC(1000, 0, node), OpType::CELL_SET, target, "{}");
    Operation op2(HLC(2000, 0, node), OpType::CELL_SET, target, "{}");
    Operation op3(HLC(1000, 1, node), OpType::CELL_SET, target, "{}");

    EXPECT_LT(op1, op2);
    EXPECT_LT(op1, op3);
    EXPECT_LT(op3, op2);
}

TEST(OperationTest, ToStringAndFromString) {
    ID node("Kj7mXp2Q");
    HLC hlc(1705312200000, 42, node);
    ID target("nP6kR2mW");

    Operation original(hlc, OpType::CELL_SET, target, R"({"t":"n","v":"42"})");

    std::string str = original.toString();
    // New format includes sheetId field (~ for null ID)
    EXPECT_EQ(str, R"(1705312200000.42.Kj7mXp2Q CELL_SET nP6kR2mW ~ {"t":"n","v":"42"})");

    Operation parsed = Operation::fromString(str);
    EXPECT_EQ(parsed.hlc, original.hlc);
    EXPECT_EQ(parsed.type, original.type);
    EXPECT_EQ(parsed.target_id.toString(), original.target_id.toString());
    EXPECT_EQ(parsed.sheetId.isNull(), original.sheetId.isNull());
    EXPECT_EQ(parsed.payload, original.payload);
}

TEST(OperationTest, FromStringWithAllOpTypes) {
    std::vector<std::pair<std::string, OpType>> test_cases = {
        {"1000.0.Kj7mXp2Q CELL_SET nP6kR2mW {}", OpType::CELL_SET},
        {"1000.0.Kj7mXp2Q CELL_DELETE nP6kR2mW {}", OpType::CELL_DELETE},
        {"1000.0.Kj7mXp2Q COL_SET nP6kR2mW {}", OpType::COL_SET},
        {"1000.0.Kj7mXp2Q COL_DELETE nP6kR2mW {}", OpType::COL_DELETE},
        {"1000.0.Kj7mXp2Q ROW_SET nP6kR2mW {}", OpType::ROW_SET},
        {"1000.0.Kj7mXp2Q ROW_DELETE nP6kR2mW {}", OpType::ROW_DELETE},
        {"1000.0.Kj7mXp2Q SHEET_SET nP6kR2mW {}", OpType::SHEET_SET},
        {"1000.0.Kj7mXp2Q SHEET_DELETE nP6kR2mW {}", OpType::SHEET_DELETE},
        {"1000.0.Kj7mXp2Q WORKBOOK_SET nP6kR2mW {}", OpType::WORKBOOK_SET},
        {"1000.0.Kj7mXp2Q NAMED_RANGE_SET nP6kR2mW {}", OpType::NAMED_RANGE_SET},
        {"1000.0.Kj7mXp2Q NAMED_RANGE_DELETE nP6kR2mW {}", OpType::NAMED_RANGE_DELETE},
        {"1000.0.Kj7mXp2Q RANGE_SET nP6kR2mW {}", OpType::RANGE_SET},
        {"1000.0.Kj7mXp2Q RANGE_DELETE nP6kR2mW {}", OpType::RANGE_DELETE},
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
    Operation op2 = Operation::fromString("1000.0.Kj7mXp2Q CELL_SET");
    EXPECT_TRUE(op2.isNull());

    // Missing payload
    Operation op3 = Operation::fromString("1000.0.Kj7mXp2Q CELL_SET nP6kR2mW");
    EXPECT_TRUE(op3.isNull());

    // Empty string
    Operation op4 = Operation::fromString("");
    EXPECT_TRUE(op4.isNull());
}

TEST(OperationTest, ToJSONAndFromJSON) {
    ID node("Kj7mXp2Q");
    HLC hlc(1705312200000, 42, node);
    ID target("nP6kR2mW");

    Operation original(hlc, OpType::CELL_SET, target, R"({"t":"n","v":"42"})");

    std::string json = original.toJSON();

    // Verify JSON structure
    EXPECT_NE(json.find("\"hlc\":\"1705312200000.42.Kj7mXp2Q\""), std::string::npos);
    EXPECT_NE(json.find("\"op\":\"CELL_SET\""), std::string::npos);
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
    std::string payload = R"({"v":"Hello \"World\"\nNew line"})";
    Operation op(hlc, OpType::CELL_SET, target, payload);

    std::string str = op.toString();
    Operation parsed = Operation::fromString(str);

    EXPECT_EQ(parsed.payload, payload);
}

TEST(OperationTest, EqualityOperator) {
    ID node("Kj7mXp2Q");
    HLC hlc(1000, 0, node);
    ID target("nP6kR2mW");

    Operation op1(hlc, OpType::CELL_SET, target, "{}");
    Operation op2(hlc, OpType::CELL_SET, target, "{}");
    Operation op3(hlc, OpType::CELL_DELETE, target, "{}");  // Different type

    EXPECT_EQ(op1, op2);
    EXPECT_FALSE(op1 == op3);
}

TEST(OperationTest, RoundtripPreservesAllFields) {
    std::vector<Operation> test_ops;

    ID node("Kj7mXp2Q");
    ID target("nP6kR2mW");

    // Various operations
    test_ops.push_back(
        Operation(HLC(1000, 0, node), OpType::CELL_SET, target, R"({"t":"n","v":42})"));
    test_ops.push_back(
        Operation(HLC(2000, 5, node), OpType::CELL_DELETE, target, R"({"reason":"user"})"));
    test_ops.push_back(
        Operation(HLC(3000, 10, node), OpType::SHEET_SET, target, R"({"name":"Sheet2"})"));

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
