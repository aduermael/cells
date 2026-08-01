// Structural tests for Windows WinHTTP backends (compile/link free on Linux).
// Proves the shipped sources and Bazel selects exist with the expected OS stack.

#include <cstdlib>

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

// Resolve a data file for structural tests. On Windows Bazel uses a
// runfiles *manifest* (not a directory tree); look there first.
std::string findFile(const std::string& relative) {
    namespace fs = std::filesystem;

    // Build keys as they appear in runfiles_manifest / runfiles tree.
    std::vector<std::string> keys = {
        relative,
        "core/net/" + relative,
        "_main/core/net/" + relative,
        "_main/" + relative,
    };
    if (relative.find("third_party/") == 0) {
        keys.push_back("_main/" + relative);
    }

    auto tryManifest = [&](const std::string& manifestPath) -> std::string {
        std::ifstream in(manifestPath);
        if (!in.good()) {
            return {};
        }
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) {
                continue;
            }
            const auto sp = line.find(' ');
            if (sp == std::string::npos) {
                continue;
            }
            const std::string key = line.substr(0, sp);
            const std::string path = line.substr(sp + 1);
            for (const auto& want : keys) {
                if (key == want || key.size() >= want.size() &&
                                       key.compare(key.size() - want.size(), want.size(), want) == 0) {
                    if (fs::exists(path)) {
                        return path;
                    }
                }
            }
        }
        return {};
    };

    if (const char* man = std::getenv("RUNFILES_MANIFEST_FILE")) {
        if (auto p = tryManifest(man); !p.empty()) {
            return p;
        }
    }
    // Common adjacent-to-binary layout when env is incomplete.
    if (const char* testBin = std::getenv("TEST_BINARY")) {
        const fs::path bin(testBin);
        const auto man = bin.string() + ".runfiles_manifest";
        if (auto p = tryManifest(man); !p.empty()) {
            return p;
        }
    }

    std::vector<std::string> candidates = {
        relative,
        "core/net/" + relative,
        "net/" + relative,
    };
    if (const char* srcdir = std::getenv("TEST_SRCDIR")) {
        if (const char* workspace = std::getenv("TEST_WORKSPACE")) {
            candidates.push_back(std::string(srcdir) + "/" + workspace + "/core/net/" + relative);
            candidates.push_back(std::string(srcdir) + "/" + workspace + "/" + relative);
        }
        candidates.push_back(std::string(srcdir) + "/_main/core/net/" + relative);
        candidates.push_back(std::string(srcdir) + "/_main/" + relative);
        candidates.push_back(std::string(srcdir) + "/cells/core/net/" + relative);
        candidates.push_back(std::string(srcdir) + "/cells/" + relative);
    }
    if (const char* runfiles = std::getenv("RUNFILES_DIR")) {
        candidates.push_back(std::string(runfiles) + "/_main/core/net/" + relative);
        candidates.push_back(std::string(runfiles) + "/_main/" + relative);
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

    // WebRTC selection is libdatachannel for all non-WASM (including Windows)
    EXPECT_EQ(build.find("rtc_peer_connection_winhttp"), std::string::npos);
    EXPECT_EQ(build.find("rtc_data_channel_winhttp"), std::string::npos);
    EXPECT_NE(build.find("rtc_peer_connection_libdc"), std::string::npos);
    EXPECT_NE(build.find("rtc_data_channel_libdc"), std::string::npos);
    EXPECT_NE(build.find("native/RTCPeerConnection_libdc.cc"), std::string::npos);
    EXPECT_NE(build.find("native/RTCDataChannel_libdc.cc"), std::string::npos);
    // MSVC-safe libdc wiring
    EXPECT_NE(build.find("ws2_32.lib"), std::string::npos);
    EXPECT_NE(build.find("bcrypt.lib"), std::string::npos);
    EXPECT_NE(build.find("advapi32.lib"), std::string::npos);
    EXPECT_NE(build.find("user32.lib"), std::string::npos);
    EXPECT_NE(build.find("_LIBDC_COPTS"), std::string::npos);
    EXPECT_NE(build.find("RTC_STATIC"), std::string::npos);
}

// Pure-Bazel libdatachannel (no rules_foreign_cc cmake) — required for MSVC full CLI.
TEST(WindowsBackendTest, LibdatachannelIsPureBazelWithOpenSsl) {
    const std::string path = findFile("third_party/libdatachannel/BUILD.libdatachannel");
    const std::string build = readAll(path);
    ASSERT_FALSE(build.empty()) << "missing " << path;

    // Must not load/use CMake-in-Bazel (foreign_cc FindOpenSSL fails on Windows).
    // Comments may mention history; assert no Starlark load/call.
    EXPECT_EQ(build.find("load(\"@rules_foreign_cc"), std::string::npos);
    EXPECT_EQ(build.find("load('@rules_foreign_cc"), std::string::npos);
    // cmake( rule call at start of a line or after whitespace (not "cmake()" in comments alone)
    EXPECT_EQ(build.find("\ncmake("), std::string::npos);
    EXPECT_EQ(build.find(" cmake("), std::string::npos);

    // Pure Bazel graph matching Luau-style third_party packaging.
    EXPECT_NE(build.find("cc_library("), std::string::npos);
    EXPECT_NE(build.find("name = \"libdatachannel\""), std::string::npos);
    EXPECT_NE(build.find("name = \"juice\""), std::string::npos);
    EXPECT_NE(build.find("name = \"usrsctp\""), std::string::npos);
    EXPECT_NE(build.find("name = \"plog\""), std::string::npos);
    EXPECT_NE(build.find("@openssl//:ssl"), std::string::npos);
    EXPECT_NE(build.find("@openssl//:crypto"), std::string::npos);
    EXPECT_NE(build.find("RTC_STATIC"), std::string::npos);
    EXPECT_NE(build.find("RTC_ENABLE_MEDIA=0"), std::string::npos);
    EXPECT_NE(build.find("RTC_ENABLE_WEBSOCKET=1"), std::string::npos);
    // Windows system libs remain select()-gated (not the only platform path).
    EXPECT_NE(build.find("@platforms//os:windows"), std::string::npos);
    EXPECT_NE(build.find("ws2_32.lib"), std::string::npos);
}
