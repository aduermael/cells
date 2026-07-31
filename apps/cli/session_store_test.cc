#include "session_store.h"

#include <cstdlib>

#include <string>
#include <unistd.h>
#include <vector>

#include "gtest/gtest.h"

namespace cells::cli {
namespace {

std::string make_temp_root() {
    const char* tmp = std::getenv("TMPDIR");
    if (tmp == nullptr || tmp[0] == '\0') {
        tmp = "/tmp";
    }
    std::string tmpl = std::string(tmp) + "/cells-sess-test-XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    char* dir = ::mkdtemp(buf.data());
    EXPECT_NE(dir, nullptr);
    return std::string(dir);
}

TEST(SessionStoreTest, EncodeDecodeMeta) {
    SessionMeta m;
    m.id = "abcd1234";
    m.url = "http://localhost:8081/?room=r1";
    m.room = "r1";
    m.name = "CLI Agent";
    m.idle_minutes = 0.05;
    m.pid = 42;
    m.started_at_ms = 1000;
    std::string json = encode_session_meta(m);
    auto back = decode_session_meta(json);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->id, m.id);
    EXPECT_EQ(back->url, m.url);
    EXPECT_EQ(back->room, m.room);
    EXPECT_EQ(back->name, m.name);
    EXPECT_DOUBLE_EQ(back->idle_minutes, 0.05);
    EXPECT_EQ(back->pid, 42);
    EXPECT_EQ(back->started_at_ms, 1000);
}

TEST(SessionStoreTest, WriteReadListPrune) {
    std::string root = make_temp_root();
    SessionMeta m;
    m.id = "sess0001";
    m.url = "http://x/?room=rr";
    m.room = "rr";
    m.name = "Bot";
    m.idle_minutes = 30;
    m.pid = 999999999;  // not a running process
    m.started_at_ms = now_unix_ms();
    ASSERT_TRUE(create_session_dir(root, m.id));
    ASSERT_TRUE(write_session_meta(root, m));
    ASSERT_TRUE(write_session_pid(root, m.id, m.pid));
    ASSERT_FALSE(process_alive(m.pid));

    auto loaded = read_session_meta(root, m.id);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->room, "rr");

    // prune_dead removes dead pid sessions
    auto listed = list_sessions(root, true);
    EXPECT_TRUE(listed.empty());

    // recreate alive-looking with our own pid
    m.pid = static_cast<std::int64_t>(::getpid());
    ASSERT_TRUE(create_session_dir(root, m.id));
    ASSERT_TRUE(write_session_meta(root, m));
    auto alive = list_sessions(root, true);
    ASSERT_EQ(alive.size(), 1u);
    EXPECT_TRUE(alive[0].alive);
    EXPECT_EQ(alive[0].meta.id, "sess0001");

    remove_session_dir(root, m.id);
    EXPECT_TRUE(list_sessions(root, false).empty());
    ::rmdir(root.c_str());
}

TEST(SessionStoreTest, Paths) {
    EXPECT_EQ(session_dir("/tmp/s", "abc"), "/tmp/s/abc");
    EXPECT_EQ(session_socket_path("/tmp/s", "abc"), "/tmp/s/abc/socket");
    EXPECT_EQ(session_meta_path("/tmp/s", "abc"), "/tmp/s/abc/meta.json");
}

TEST(SessionStoreTest, ProcessAliveSelf) {
    EXPECT_TRUE(process_alive(static_cast<std::int64_t>(::getpid())));
    EXPECT_FALSE(process_alive(-1));
    EXPECT_FALSE(process_alive(0));
}

}  // namespace
}  // namespace cells::cli
