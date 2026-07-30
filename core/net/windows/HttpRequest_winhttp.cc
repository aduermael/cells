// Windows HTTP request implementation using WinHTTP (OS stack)
// Async work runs on a dedicated worker thread (same shape as curl backend).

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#include <winhttp.h>

// winnt.h defines DELETE as an access mask; collides with HttpMethod::DELETE.
#ifdef DELETE
#undef DELETE
#endif

#include "core/net/include/HttpRequest.h"

#pragma comment(lib, "winhttp.lib")

namespace cells::net {
namespace {

std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int needed =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(),
                        needed);
    return wide;
}

std::string wideToUtf8(const wchar_t* wide, size_t len) {
    if (wide == nullptr || len == 0) {
        return {};
    }
    const int needed =
        WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string utf8(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(len), utf8.data(), needed, nullptr,
                        nullptr);
    return utf8;
}

std::string lastErrorString(DWORD err) {
    return "WinHTTP error " + std::to_string(err);
}

}  // namespace

class WinHttpRequest : public HttpRequest {
public:
    WinHttpRequest(HttpMethod method, std::string host, uint16_t port, std::string path,
                   bool secure)
        : HttpRequest(method, std::move(host), port, std::move(path), secure) {}

    ~WinHttpRequest() override {
        cancelled_.store(true);
        closeHandles();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

protected:
    void _sendAsync() override { startRequest(false); }

    void _sendAsyncStreaming() override { startRequest(true); }

    void _cancel() override {
        cancelled_.store(true);
        closeHandles();
    }

private:
    std::thread thread_;
    std::atomic<bool> cancelled_{false};
    std::mutex handles_mutex_;
    HINTERNET session_ = nullptr;
    HINTERNET connect_ = nullptr;
    HINTERNET request_ = nullptr;

    void startRequest(bool streaming) {
        thread_ = std::thread([this, streaming]() { runRequest(streaming); });
    }

    std::string buildPathAndQuery() const {
        std::ostringstream out;
        out << path_;
        if (!query_params_.empty()) {
            out << "?";
            bool first = true;
            for (const auto& [key, value] : query_params_) {
                if (!first) {
                    out << "&";
                }
                first = false;
                out << key << "=" << value;
            }
        }
        return out.str();
    }

    // Snapshot request handle without holding the lock across WinHTTP I/O.
    HINTERNET requestHandle() {
        std::lock_guard<std::mutex> lock(handles_mutex_);
        return request_;
    }

    void closeHandles() {
        std::lock_guard<std::mutex> lock(handles_mutex_);
        if (request_ != nullptr) {
            WinHttpCloseHandle(request_);
            request_ = nullptr;
        }
        if (connect_ != nullptr) {
            WinHttpCloseHandle(connect_);
            connect_ = nullptr;
        }
        if (session_ != nullptr) {
            WinHttpCloseHandle(session_);
            session_ = nullptr;
        }
    }

    bool openHandles(const std::wstring& host_w, INTERNET_PORT port, const std::wstring& path_w,
                     const std::wstring& method_w, DWORD flags) {
        std::lock_guard<std::mutex> lock(handles_mutex_);
        session_ = WinHttpOpen(L"cells/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session_ == nullptr) {
            return false;
        }

        WinHttpSetTimeouts(session_, 0, static_cast<int>(timeout_ms_),
                           static_cast<int>(timeout_ms_), static_cast<int>(timeout_ms_));

        connect_ = WinHttpConnect(session_, host_w.c_str(), port, 0);
        if (connect_ == nullptr) {
            return false;
        }

        request_ = WinHttpOpenRequest(connect_, method_w.c_str(), path_w.c_str(), nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        return request_ != nullptr;
    }

    void runRequest(bool streaming) {
        const std::wstring host_w = utf8ToWide(host_);
        const std::wstring path_w = utf8ToWide(buildPathAndQuery());
        const std::wstring method_w = utf8ToWide(httpMethodToString(method_));
        if (host_w.empty() || path_w.empty() || method_w.empty()) {
            completeWithError("Invalid request URL or method encoding");
            return;
        }

        const DWORD flags = secure_ ? WINHTTP_FLAG_SECURE : 0;
        if (!openHandles(host_w, static_cast<INTERNET_PORT>(port_), path_w, method_w, flags)) {
            const DWORD err = GetLastError();
            closeHandles();
            if (!cancelled_.load()) {
                completeWithError(lastErrorString(err));
            }
            return;
        }

        HINTERNET req = requestHandle();
        if (req == nullptr) {
            return;
        }

        // Extra headers
        {
            std::ostringstream header_stream;
            for (const auto& [key, value] : headers_) {
                header_stream << key << ": " << value << "\r\n";
            }
            const std::string header_bytes = header_stream.str();
            if (!header_bytes.empty()) {
                const std::wstring header_w = utf8ToWide(header_bytes);
                if (!WinHttpAddRequestHeaders(
                        req, header_w.c_str(), static_cast<DWORD>(header_w.size()),
                        WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
                    const DWORD err = GetLastError();
                    closeHandles();
                    if (!cancelled_.load()) {
                        completeWithError(lastErrorString(err));
                    }
                    return;
                }
            }
        }

        LPVOID body_ptr = body_.empty() ? WINHTTP_NO_REQUEST_DATA : body_.data();
        const DWORD body_len = static_cast<DWORD>(body_.size());
        if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, body_ptr, body_len, body_len,
                                0)) {
            const DWORD err = GetLastError();
            closeHandles();
            if (!cancelled_.load()) {
                completeWithError(lastErrorString(err));
            }
            return;
        }

        if (!WinHttpReceiveResponse(req, nullptr)) {
            const DWORD err = GetLastError();
            closeHandles();
            if (!cancelled_.load()) {
                completeWithError(lastErrorString(err));
            }
            return;
        }

        if (cancelled_.load()) {
            closeHandles();
            return;
        }

        // Status code
        DWORD status_code = 0;
        DWORD status_size = sizeof(status_code);
        if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size,
                                 WINHTTP_NO_HEADER_INDEX)) {
            const DWORD err = GetLastError();
            closeHandles();
            if (!cancelled_.load()) {
                completeWithError(lastErrorString(err));
            }
            return;
        }
        response_.setStatusCode(static_cast<int>(status_code));

        // Raw headers (skip status line)
        DWORD header_bytes = 0;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                            WINHTTP_NO_OUTPUT_BUFFER, &header_bytes, WINHTTP_NO_HEADER_INDEX);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && header_bytes > 0) {
            std::vector<wchar_t> header_buf(header_bytes / sizeof(wchar_t) + 1, L'\0');
            if (WinHttpQueryHeaders(req, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                    WINHTTP_HEADER_NAME_BY_INDEX, header_buf.data(), &header_bytes,
                                    WINHTTP_NO_HEADER_INDEX)) {
                parseRawHeaders(header_buf.data(), header_bytes / sizeof(wchar_t));
            }
        }

        // Body
        std::vector<uint8_t> chunk(8192);
        for (;;) {
            if (cancelled_.load()) {
                closeHandles();
                return;
            }

            req = requestHandle();
            if (req == nullptr) {
                return;
            }

            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(req, &available)) {
                const DWORD err = GetLastError();
                closeHandles();
                if (!cancelled_.load()) {
                    completeWithError(lastErrorString(err));
                }
                return;
            }
            if (available == 0) {
                break;
            }

            if (chunk.size() < available) {
                chunk.resize(available);
            }

            DWORD read = 0;
            if (!WinHttpReadData(req, chunk.data(), available, &read)) {
                const DWORD err = GetLastError();
                closeHandles();
                if (!cancelled_.load()) {
                    completeWithError(lastErrorString(err));
                }
                return;
            }
            if (read == 0) {
                break;
            }

            if (streaming) {
                onStreamData(chunk.data(), static_cast<size_t>(read));
            } else {
                response_.appendBytes(chunk.data(), static_cast<size_t>(read));
            }
        }

        closeHandles();

        if (cancelled_.load()) {
            return;
        }

        if (streaming) {
            onStreamEnd();
        } else {
            completeWithSuccess();
        }
    }

    void parseRawHeaders(const wchar_t* raw, size_t wchar_count) {
        size_t line_start = 0;
        bool first = true;
        for (size_t i = 0; i < wchar_count; ++i) {
            if (raw[i] != L'\n') {
                continue;
            }
            size_t line_end = i;
            if (line_end > line_start && raw[line_end - 1] == L'\r') {
                --line_end;
            }
            if (!first && line_end > line_start) {
                const std::wstring line(raw + line_start, line_end - line_start);
                const size_t colon = line.find(L':');
                if (colon != std::wstring::npos) {
                    std::string key = wideToUtf8(line.data(), colon);
                    size_t value_start = colon + 1;
                    while (value_start < line.size() &&
                           (line[value_start] == L' ' || line[value_start] == L'\t')) {
                        ++value_start;
                    }
                    std::string value =
                        wideToUtf8(line.data() + value_start, line.size() - value_start);
                    response_.setHeader(key, value);
                }
            }
            first = false;
            line_start = i + 1;
        }
    }
};

std::unique_ptr<HttpRequest> HttpRequest::make(HttpMethod method, const std::string& host,
                                               uint16_t port, const std::string& path,
                                               bool secure) {
    return std::make_unique<WinHttpRequest>(method, host, port, path, secure);
}

}  // namespace cells::net

#endif  // _WIN32 && !__EMSCRIPTEN__
