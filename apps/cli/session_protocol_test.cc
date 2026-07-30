#include "session_protocol.h"

#include "gtest/gtest.h"

namespace cells::cli {
namespace {

TEST(SessionProtocolTest, ParseRoomFromQuery) {
    auto t = parse_room_target("https://example.com/?room=abc123");
    ASSERT_TRUE(t.ok) << t.error;
    EXPECT_EQ(t.room_id, "abc123");
    EXPECT_EQ(t.host, "example.com");
    EXPECT_TRUE(t.secure);
    EXPECT_EQ(t.signaling_ws, "wss://example.com/ws");
}

TEST(SessionProtocolTest, ParseRoomFromPath) {
    auto t = parse_room_target("http://localhost:8081/my-room");
    ASSERT_TRUE(t.ok) << t.error;
    EXPECT_EQ(t.room_id, "my-room");
    EXPECT_EQ(t.port, 8081);
    EXPECT_FALSE(t.secure);
    EXPECT_EQ(t.signaling_ws, "ws://localhost:8081/ws");
}

TEST(SessionProtocolTest, ParseRoomMissingFails) {
    auto t = parse_room_target("https://example.com/");
    EXPECT_FALSE(t.ok);
    EXPECT_FALSE(t.error.empty());
}

TEST(SessionProtocolTest, ParseInvalidScheme) {
    auto t = parse_room_target("ftp://example.com/?room=x");
    EXPECT_FALSE(t.ok);
}

TEST(SessionProtocolTest, IdleExpired) {
    // 1 minute idle
    EXPECT_FALSE(idle_expired(0, 30'000, 1.0));
    EXPECT_TRUE(idle_expired(0, 60'000, 1.0));
    EXPECT_TRUE(idle_expired(0, 60'001, 1.0));
    // fractional minutes: 0.05 min = 3 seconds
    EXPECT_FALSE(idle_expired(0, 2'999, 0.05));
    EXPECT_TRUE(idle_expired(0, 3'000, 0.05));
    // non-positive idle never expires
    EXPECT_FALSE(idle_expired(0, 1'000'000, 0.0));
    EXPECT_FALSE(idle_expired(0, 1'000'000, -1.0));
}

TEST(SessionProtocolTest, IdleMinutesToMs) {
    EXPECT_EQ(idle_minutes_to_ms(1.0), 60'000);
    EXPECT_EQ(idle_minutes_to_ms(0.05), 3'000);
    EXPECT_EQ(idle_minutes_to_ms(0.0), 0);
}

TEST(SessionProtocolTest, ParseIdleMinutes) {
    auto a = parse_idle_minutes("30");
    ASSERT_TRUE(a.has_value());
    EXPECT_DOUBLE_EQ(*a, 30.0);
    auto b = parse_idle_minutes("0.05");
    ASSERT_TRUE(b.has_value());
    EXPECT_DOUBLE_EQ(*b, 0.05);
    EXPECT_FALSE(parse_idle_minutes("nope").has_value());
    EXPECT_FALSE(parse_idle_minutes("-1").has_value());
}

TEST(SessionProtocolTest, ParseRequestOps) {
    auto ping = parse_session_request(R"({"op":"ping"})");
    ASSERT_TRUE(ping.has_value());
    EXPECT_EQ(ping->op, SessionOp::kPing);

    auto exec = parse_session_request(R"json({"op":"exec","code":"setCell(\"A1\",1)"})json");
    ASSERT_TRUE(exec.has_value());
    EXPECT_EQ(exec->op, SessionOp::kExec);
    EXPECT_EQ(exec->code, "setCell(\"A1\",1)");

    auto watch = parse_session_request(R"({"cmd":"watch"})");
    ASSERT_TRUE(watch.has_value());
    EXPECT_EQ(watch->op, SessionOp::kWatch);

    auto stop = parse_session_request(R"({"op":"stop"})");
    ASSERT_TRUE(stop.has_value());
    EXPECT_EQ(stop->op, SessionOp::kStop);

    auto bad = parse_session_request("not-json");
    EXPECT_FALSE(bad.has_value());
}

TEST(SessionProtocolTest, EncodeResponseRoundTripFields) {
    SessionResponse r;
    r.ok = true;
    r.id = "deadbeef";
    r.url = "http://localhost:8081/?room=r1";
    r.room = "r1";
    r.state = "ONLINE";
    r.peers = 2;
    r.idle_minutes = 30;
    r.output = "hello\nworld";
    std::string json = encode_session_response(r);
    EXPECT_NE(json.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(json.find("deadbeef"), std::string::npos);
    EXPECT_NE(json.find("ONLINE"), std::string::npos);
    EXPECT_EQ(json_get_string(json, "id"), "deadbeef");
    EXPECT_EQ(json_get_string(json, "output"), "hello\nworld");
    EXPECT_EQ(json_get_number(json, "peers").value_or(-1), 2);
}

TEST(SessionProtocolTest, EncodeEvent) {
    SessionEvent e;
    e.type = "op";
    e.op_type = "CELL_SET";
    e.peer_id = "p1";
    e.message = "CELL_SET";
    std::string json = encode_session_event(e);
    EXPECT_EQ(json_get_string(json, "type"), "op");
    EXPECT_EQ(json_get_string(json, "op_type"), "CELL_SET");
    EXPECT_EQ(json_get_string(json, "peer_id"), "p1");
}

TEST(SessionProtocolTest, GenerateSessionId) {
    std::string a = generate_session_id();
    std::string b = generate_session_id();
    EXPECT_EQ(a.size(), 8u);
    EXPECT_EQ(b.size(), 8u);
    for (char c : a) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
    // Extremely unlikely to collide
    EXPECT_NE(a, b);
}

}  // namespace
}  // namespace cells::cli
