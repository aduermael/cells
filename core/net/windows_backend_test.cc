// Structural tests for Windows WinHTTP backends (compile/link free on Linux).
// Proves the shipped sources and Bazel selects exist with the expected OS stack.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

std::string findFile(const std::string& relative) {
    namespace fs = std::filesystem;
    std::vector<std::string> candidates = {
        relative,
        "core/net/" + relative,
        "net/" + relative,
    };
    if (const char* srcdir = std::getenv("TEST_SRCDIR")) {
        if (const char* workspace = std::getenv("TEST_WORKSPACE")) {
            candidates.push_back(std::string(srcdir) + "/" + workspace + "/core/net/" + relative);
        }
        candidates.push_back(std::string(srcdir) + "/_main/core/net/" + relative);
        candidates.push_back(std::string(srcdir) + "/cells/core/net/" + relative);
    }
    for (const auto& path : candidates) {
        if (fs::exists(path)) {
            return path;
        }
    }
    return relative;
}

std::string readAll(const std::string& path) {
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "missing file: " << path;
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

TEST(WindowsBackendTest, HttpRequestSourceUsesWinHttp) {
    const std::string path = findFile("windows/HttpRequest_winhttp.cc");
    const std::string src = readAll(path);
    ASSERT_FALSE(src.empty());

    EXPECT_NE(src.find("#include <winhttp.h>"), std::string::npos);
    EXPECT_NE(src.find("WinHttpOpen"), std::string::npos);
    EXPECT_NE(src.find("WinHttpSendRequest"), std::string::npos);
    EXPECT_NE(src.find("WinHttpReadData"), std::string::npos);
    EXPECT_NE(src.find("_sendAsync"), std::string::npos);
    EXPECT_NE(src.find("_sendAsyncStreaming"), std::string::npos);
    EXPECT_NE(src.find("_cancel"), std::string::npos);
    EXPECT_NE(src.find("HttpRequest::make"), std::string::npos);
    EXPECT_NE(src.find("defined(_WIN32)"), std::string::npos);
    // Must not pull curl
    EXPECT_EQ(src.find("curl/curl.h"), std::string::npos);
}

TEST(WindowsBackendTest, WebSocketSourceUsesWinHttpWebSocket) {
    const std::string path = findFile("windows/WSConnection_winhttp.cc");
    const std::string src = readAll(path);
    ASSERT_FALSE(src.empty());

    EXPECT_NE(src.find("#include <winhttp.h>"), std::string::npos);
    EXPECT_NE(src.find("WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET"), std::string::npos);
    EXPECT_NE(src.find("WinHttpWebSocketCompleteUpgrade"), std::string::npos);
    EXPECT_NE(src.find("WinHttpWebSocketSend"), std::string::npos);
    EXPECT_NE(src.find("WinHttpWebSocketReceive"), std::string::npos);
    EXPECT_NE(src.find("_init"), std::string::npos);
    EXPECT_NE(src.find("_connect"), std::string::npos);
    EXPECT_NE(src.find("_close"), std::string::npos);
    EXPECT_NE(src.find("_send"), std::string::npos);
    EXPECT_NE(src.find("_destroy"), std::string::npos);
    EXPECT_NE(src.find("onOpen"), std::string::npos);
    EXPECT_NE(src.find("onMessage"), std::string::npos);
    EXPECT_NE(src.find("onClose"), std::string::npos);
    EXPECT_NE(src.find("onError"), std::string::npos);
    EXPECT_NE(src.find("WSConnection::make"), std::string::npos);
    // Must not pull libdatachannel
    EXPECT_EQ(src.find("rtc/rtc.hpp"), std::string::npos);
}

TEST(WindowsBackendTest, WebSocketLifecycleAllowsReconnectAndNotifiesAbandon) {
    const std::string path = findFile("windows/WSConnection_winhttp.cc");
    const std::string src = readAll(path);
    ASSERT_FALSE(src.empty());

    // Reconnect: stop/join previous worker before a new connect; use WsWorkerState.
    EXPECT_NE(src.find("ws_worker_state.h"), std::string::npos);
    EXPECT_NE(src.find("stopWorker"), std::string::npos);
    EXPECT_NE(src.find("tryBeginConnect"), std::string::npos);
    EXPECT_NE(src.find("onWorkerFinished"), std::string::npos);
    EXPECT_NE(src.find("worker_.join()"), std::string::npos);

    // Abandoned handshake always notifies (onClose if closing, else onError).
    EXPECT_NE(src.find("notifyAbandoned"), std::string::npos);
    EXPECT_NE(src.find("abandonNotify"), std::string::npos);
}

TEST(WindowsBackendTest, BazelSelectsWinHttpOnWindowsOnly) {
    const std::string path = findFile("BUILD");
    const std::string build = readAll(path);
    ASSERT_FALSE(build.empty());

    EXPECT_NE(build.find("is_windows"), std::string::npos);
    EXPECT_NE(build.find("@platforms//os:windows"), std::string::npos);
    EXPECT_NE(build.find("http_request_winhttp"), std::string::npos);
    EXPECT_NE(build.find("ws_connection_winhttp"), std::string::npos);
    EXPECT_NE(build.find("windows/HttpRequest_winhttp.cc"), std::string::npos);
    EXPECT_NE(build.find("windows/WSConnection_winhttp.cc"), std::string::npos);
    EXPECT_NE(build.find("winhttp.lib"), std::string::npos);

    // Non-Windows backends still present
    EXPECT_NE(build.find("http_request_curl"), std::string::npos);
    EXPECT_NE(build.find("ws_connection_libdc"), std::string::npos);
    EXPECT_NE(build.find("http_request_apple"), std::string::npos);
    EXPECT_NE(build.find("ws_connection_apple"), std::string::npos);
    EXPECT_NE(build.find("http_request_web"), std::string::npos);
    EXPECT_NE(build.find("ws_connection_web"), std::string::npos);
    EXPECT_NE(build.find("http_request_default"), std::string::npos);
    EXPECT_NE(build.find("ws_connection_default"), std::string::npos);

    // WebRTC selection is unchanged (libdc default, not Windows-specific)
    EXPECT_EQ(build.find("rtc_peer_connection_winhttp"), std::string::npos);
    EXPECT_EQ(build.find("rtc_data_channel_winhttp"), std::string::npos);
}
