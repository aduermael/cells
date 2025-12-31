// Tests for SSE (Server-Sent Events) parser

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "core/net/include/SSEParser.h"

namespace cells::net {
namespace {

struct Event {
    std::string type;
    std::string data;
};

class SSEParserTest : public ::testing::Test {
protected:
    std::vector<Event> events_;

    SSEParser makeParser() {
        return SSEParser([this](const std::string& type, const std::string& data) {
            events_.push_back({type, data});
        });
    }
};

TEST_F(SSEParserTest, SimpleDataEvent) {
    auto parser = makeParser();
    parser.feed("data: hello\n\n");

    ASSERT_EQ(events_.size(), 1);
    EXPECT_EQ(events_[0].type, "");
    EXPECT_EQ(events_[0].data, "hello");
}

TEST_F(SSEParserTest, EventWithType) {
    auto parser = makeParser();
    parser.feed("event: message\ndata: hello\n\n");

    ASSERT_EQ(events_.size(), 1);
    EXPECT_EQ(events_[0].type, "message");
    EXPECT_EQ(events_[0].data, "hello");
}

TEST_F(SSEParserTest, MultilineData) {
    auto parser = makeParser();
    parser.feed("data: line1\ndata: line2\ndata: line3\n\n");

    ASSERT_EQ(events_.size(), 1);
    EXPECT_EQ(events_[0].type, "");
    EXPECT_EQ(events_[0].data, "line1\nline2\nline3");
}

TEST_F(SSEParserTest, MultipleEvents) {
    auto parser = makeParser();
    parser.feed("event: first\ndata: one\n\nevent: second\ndata: two\n\n");

    ASSERT_EQ(events_.size(), 2);
    EXPECT_EQ(events_[0].type, "first");
    EXPECT_EQ(events_[0].data, "one");
    EXPECT_EQ(events_[1].type, "second");
    EXPECT_EQ(events_[1].data, "two");
}

TEST_F(SSEParserTest, ChunkedData) {
    auto parser = makeParser();

    // Feed data in chunks (simulating streaming)
    parser.feed("event: mess");
    EXPECT_EQ(events_.size(), 0);

    parser.feed("age\ndata: hel");
    EXPECT_EQ(events_.size(), 0);

    parser.feed("lo\n\n");
    ASSERT_EQ(events_.size(), 1);
    EXPECT_EQ(events_[0].type, "message");
    EXPECT_EQ(events_[0].data, "hello");
}

TEST_F(SSEParserTest, CommentLines) {
    auto parser = makeParser();
    parser.feed(": this is a comment\ndata: actual data\n\n");

    ASSERT_EQ(events_.size(), 1);
    EXPECT_EQ(events_[0].data, "actual data");
}

TEST_F(SSEParserTest, EmptyDataNotDispatched) {
    auto parser = makeParser();
    parser.feed("event: test\n\n");

    // No event should be dispatched if data is empty
    EXPECT_EQ(events_.size(), 0);
}

TEST_F(SSEParserTest, CRLFLineEndings) {
    auto parser = makeParser();
    parser.feed("event: test\r\ndata: hello\r\n\r\n");

    ASSERT_EQ(events_.size(), 1);
    EXPECT_EQ(events_[0].type, "test");
    EXPECT_EQ(events_[0].data, "hello");
}

TEST_F(SSEParserTest, NoSpaceAfterColon) {
    auto parser = makeParser();
    parser.feed("data:nospace\n\n");

    ASSERT_EQ(events_.size(), 1);
    EXPECT_EQ(events_[0].data, "nospace");
}

TEST_F(SSEParserTest, JsonData) {
    auto parser = makeParser();
    parser.feed(R"(event: message_start
data: {"type":"message_start","message":{"id":"msg_123"}}

)");

    ASSERT_EQ(events_.size(), 1);
    EXPECT_EQ(events_[0].type, "message_start");
    EXPECT_EQ(events_[0].data, R"({"type":"message_start","message":{"id":"msg_123"}})");
}

TEST_F(SSEParserTest, AnthropicStreamFormat) {
    auto parser = makeParser();

    // Simulate Anthropic API SSE stream
    parser.feed("event: message_start\n");
    parser.feed(R"(data: {"type":"message_start"})");
    parser.feed("\n\n");

    parser.feed("event: content_block_delta\n");
    parser.feed(R"(data: {"type":"content_block_delta","delta":{"text":"Hello"}})");
    parser.feed("\n\n");

    parser.feed("event: message_stop\n");
    parser.feed(R"(data: {"type":"message_stop"})");
    parser.feed("\n\n");

    ASSERT_EQ(events_.size(), 3);
    EXPECT_EQ(events_[0].type, "message_start");
    EXPECT_EQ(events_[1].type, "content_block_delta");
    EXPECT_EQ(events_[2].type, "message_stop");
}

TEST_F(SSEParserTest, Reset) {
    auto parser = makeParser();

    parser.feed("event: test\ndata: partial");
    EXPECT_EQ(events_.size(), 0);

    parser.reset();
    parser.feed("event: new\ndata: fresh\n\n");

    ASSERT_EQ(events_.size(), 1);
    EXPECT_EQ(events_[0].type, "new");
    EXPECT_EQ(events_[0].data, "fresh");
}

}  // namespace
}  // namespace cells::net
