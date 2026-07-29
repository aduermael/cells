#include "sync_args.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace cells::cli {
namespace {

// Build a mutable argv-style vector from string literals for parse_sync_args.
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

TEST(SyncArgsTest, NotSyncCommand) {
    Argv args({"cells", "-i", "in.csv", "out.zcd"});
    auto r = parse_sync_args(args.argc(), args.argv());
    EXPECT_FALSE(r.is_sync);
}

TEST(SyncArgsTest, PositionalUrl) {
    Argv args({"cells", "sync", "https://example.com/?room=r1"});
    auto r = parse_sync_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_sync);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.options.url, "https://example.com/?room=r1");
}

TEST(SyncArgsTest, ServerFlag) {
    Argv args({"cells", "sync", "--server", "https://cells-app.fly.dev/?room=abc"});
    auto r = parse_sync_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_sync);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.options.url, "https://cells-app.fly.dev/?room=abc");
    EXPECT_FALSE(r.options.apply);
    EXPECT_FALSE(r.options.send);
}

TEST(SyncArgsTest, UrlFlagAlias) {
    Argv args({"cells", "sync", "--url", "http://localhost:8081/?room=x"});
    auto r = parse_sync_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_sync);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.options.url, "http://localhost:8081/?room=x");
}

TEST(SyncArgsTest, ServerWithApplyAndFlags) {
    Argv args({"cells", "sync", "--server", "https://ex.com/?room=1", "--apply",
               "book.zcd", "-q", "--ops-only"});
    auto r = parse_sync_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_sync);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.options.url, "https://ex.com/?room=1");
    EXPECT_TRUE(r.options.apply);
    EXPECT_EQ(r.options.workbook_file, "book.zcd");
    EXPECT_TRUE(r.options.quiet);
    EXPECT_TRUE(r.options.ops_only);
}

TEST(SyncArgsTest, MissingUrlIsStillSync) {
    Argv args({"cells", "sync", "--ops-only"});
    auto r = parse_sync_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_sync);
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(r.options.url.empty());
}

TEST(SyncArgsTest, UnknownOption) {
    Argv args({"cells", "sync", "--nope"});
    auto r = parse_sync_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_sync);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

TEST(SyncArgsTest, DuplicateUrlError) {
    Argv args({"cells", "sync", "https://a.example/", "https://b.example/"});
    auto r = parse_sync_args(args.argc(), args.argv());
    ASSERT_TRUE(r.is_sync);
    EXPECT_FALSE(r.ok);
}

}  // namespace
}  // namespace cells::cli
