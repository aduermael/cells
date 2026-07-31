#include "session_args.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace cells::cli {
namespace {

class Argv {
public:
    explicit Argv(std::initializer_list<const char*> args) {
        storage_.reserve(args.size());
        pointers_.reserve(args.size());
        for (const char* a : args) {
            storage_.emplace_back(a);
        }
        for (auto& s : storage_) {
            pointers_.push_back(s.data());
        }
    }
    int argc() const { return static_cast<int>(pointers_.size()); }
    char** argv() { return pointers_.data(); }

private:
    std::vector<std::string> storage_;
    std::vector<char*> pointers_;
};

TEST(SessionArgsTest, NotSession) {
    Argv args({"cells", "sync", "http://x/?room=1"});
    auto r = parse_session_args(args.argc(), args.argv());
    EXPECT_FALSE(r.is_session);
}

TEST(SessionArgsTest, StartWithUrlAndIdle) {
    Argv args({"cells", "session", "start", "http://localhost:8081/?room=r1", "--idle-minutes",
               "0.05", "--name", "Bot"});
    auto r = parse_session_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_session);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.options.kind, SessionCommandKind::kStart);
    EXPECT_EQ(r.options.url, "http://localhost:8081/?room=r1");
    EXPECT_DOUBLE_EQ(r.options.idle_minutes, 0.05);
    EXPECT_EQ(r.options.name, "Bot");
    validate_session_options(r);
    EXPECT_TRUE(r.ok);
}

TEST(SessionArgsTest, StartMissingUrlFailsValidation) {
    Argv args({"cells", "session", "start"});
    auto r = parse_session_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_session);
    validate_session_options(r);
    EXPECT_FALSE(r.ok);
}

TEST(SessionArgsTest, List) {
    Argv args({"cells", "session", "list"});
    auto r = parse_session_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_session);
    EXPECT_EQ(r.options.kind, SessionCommandKind::kList);
}

TEST(SessionArgsTest, StopById) {
    Argv args({"cells", "session", "stop", "deadbeef"});
    auto r = parse_session_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_session);
    EXPECT_EQ(r.options.kind, SessionCommandKind::kStop);
    EXPECT_EQ(r.options.session_id, "deadbeef");
    validate_session_options(r);
    EXPECT_TRUE(r.ok);
}

TEST(SessionArgsTest, ExecWithInline) {
    Argv args({"cells", "session", "exec", "abc123", "-e", "print(1)"});
    auto r = parse_session_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_session);
    EXPECT_EQ(r.options.kind, SessionCommandKind::kExec);
    EXPECT_EQ(r.options.session_id, "abc123");
    EXPECT_EQ(r.options.script_inline, "print(1)");
    validate_session_options(r);
    EXPECT_TRUE(r.ok);
}

TEST(SessionArgsTest, ExecMissingScriptFails) {
    Argv args({"cells", "session", "exec", "abc123"});
    auto r = parse_session_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_session);
    validate_session_options(r);
    EXPECT_FALSE(r.ok);
}

TEST(SessionArgsTest, WatchWithDuration) {
    Argv args({"cells", "session", "watch", "sid1", "--duration", "2.5"});
    auto r = parse_session_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_session);
    EXPECT_EQ(r.options.kind, SessionCommandKind::kWatch);
    EXPECT_DOUBLE_EQ(r.options.watch_duration_sec, 2.5);
    validate_session_options(r);
    EXPECT_TRUE(r.ok);
}

TEST(SessionArgsTest, Status) {
    Argv args({"cells", "session", "status", "--id", "xyz"});
    auto r = parse_session_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_session);
    EXPECT_EQ(r.options.kind, SessionCommandKind::kStatus);
    EXPECT_EQ(r.options.session_id, "xyz");
}

TEST(SessionArgsTest, UnknownSubcommand) {
    Argv args({"cells", "session", "explode"});
    auto r = parse_session_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_session);
    EXPECT_FALSE(r.ok);
}

TEST(SessionArgsTest, UsageMentionsIdleAndCommands) {
    std::string u = session_usage("cells");
    EXPECT_NE(u.find("session start"), std::string::npos);
    EXPECT_NE(u.find("session list"), std::string::npos);
    EXPECT_NE(u.find("session stop"), std::string::npos);
    EXPECT_NE(u.find("session exec"), std::string::npos);
    EXPECT_NE(u.find("session watch"), std::string::npos);
    EXPECT_NE(u.find("session export"), std::string::npos);
    EXPECT_NE(u.find("idle-minutes"), std::string::npos);
    EXPECT_NE(u.find("wait-seconds"), std::string::npos);
}

TEST(SessionArgsTest, HelpFlag) {
    Argv args({"cells", "session", "--help"});
    auto r = parse_session_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_session);
    EXPECT_EQ(r.options.kind, SessionCommandKind::kHelp);
}

TEST(SessionArgsTest, ExportArgs) {
    Argv args({"cells", "session", "export", "sid1", "/tmp/out.xlsx"});
    auto r = parse_session_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_session);
    EXPECT_EQ(r.options.kind, SessionCommandKind::kExport);
    EXPECT_EQ(r.options.session_id, "sid1");
    EXPECT_EQ(r.options.export_path, "/tmp/out.xlsx");
    validate_session_options(r);
    EXPECT_TRUE(r.ok);
}

TEST(SessionArgsTest, StartWaitSeconds) {
    Argv args({"cells", "session", "start", "http://x/?room=1", "--wait-seconds", "5"});
    auto r = parse_session_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_session);
    EXPECT_DOUBLE_EQ(r.options.wait_seconds, 5.0);
}

TEST(SessionArgsTest, ExecForce) {
    Argv args({"cells", "session", "exec", "sid", "-e", "print(1)", "--force"});
    auto r = parse_session_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_session);
    EXPECT_TRUE(r.options.force);
}

}  // namespace
}  // namespace cells::cli
