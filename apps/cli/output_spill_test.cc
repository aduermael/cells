#include "output_spill.h"

#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace cells::cli {
namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.good()) << "failed to open " << path;
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

TEST(OutputSpillTest, SmallPayloadStaysInline) {
    std::string payload = "hello agent";
    SpillResult r = maybe_spill_output(payload, /*threshold=*/100);
    EXPECT_FALSE(r.spilled);
    EXPECT_EQ(r.stdout_text, payload);
    EXPECT_TRUE(r.path.empty());
    EXPECT_EQ(r.bytes, payload.size());
}

TEST(OutputSpillTest, ExactThresholdMinusOneStaysInline) {
    std::string payload(99, 'x');
    SpillResult r = maybe_spill_output(payload, /*threshold=*/100);
    EXPECT_FALSE(r.spilled);
    EXPECT_EQ(r.stdout_text, payload);
}

TEST(OutputSpillTest, LargePayloadSpillsToTempWithJsonPointer) {
    std::string payload(150, 'A');
    payload += "\nline2";
    SpillResult r = maybe_spill_output(payload, /*threshold=*/100);

    ASSERT_TRUE(r.spilled) << r.stdout_text;
    EXPECT_EQ(r.bytes, payload.size());
    EXPECT_FALSE(r.path.empty());
    // Path should live under temp
    EXPECT_NE(r.path.find("cells-out-"), std::string::npos);

    // stdout is JSON pointing at the file
    EXPECT_NE(r.stdout_text.find("\"path\":"), std::string::npos);
    EXPECT_NE(r.stdout_text.find("\"bytes\":"), std::string::npos);
    EXPECT_NE(r.stdout_text.find("\"preview\":"), std::string::npos);
    EXPECT_NE(r.stdout_text.find(r.path), std::string::npos);

    // Full body matches
    EXPECT_EQ(read_file(r.path), payload);

    // Run twice for consistency
    SpillResult r2 = maybe_spill_output(payload, /*threshold=*/100);
    ASSERT_TRUE(r2.spilled);
    EXPECT_EQ(read_file(r2.path), payload);
    EXPECT_NE(r2.path, r.path);  // unique temp files
}

TEST(OutputSpillTest, DefaultThresholdConstant) {
    EXPECT_EQ(kOutputSpillThreshold, 32u * 1024u);
    std::string small(kOutputSpillThreshold - 1, 'z');
    SpillResult r = maybe_spill_output(small);
    EXPECT_FALSE(r.spilled);

    std::string large(kOutputSpillThreshold, 'z');
    SpillResult r2 = maybe_spill_output(large);
    EXPECT_TRUE(r2.spilled);
    EXPECT_EQ(read_file(r2.path), large);
}

TEST(OutputSpillTest, JsonEscapeSpecialChars) {
    EXPECT_EQ(json_escape("a\"b"), "a\\\"b");
    EXPECT_EQ(json_escape("a\\b"), "a\\\\b");
    EXPECT_EQ(json_escape("a\nb"), "a\\nb");
}

}  // namespace
}  // namespace cells::cli
