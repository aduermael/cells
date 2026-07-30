// Windows WebSocket implementation using WinHTTP (OS stack, Windows 8+)
// Connect + receive loop on a worker thread; send is synchronized with handles.
// Worker is joinable/reusable: close/reset can reconnect (see WsWorkerState).

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <winhttp.h>

#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/net/include/WSConnection.h"
#include "core/net/windows/ws_worker_state.h"

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

std::string lastErrorString(DWORD err) {
    return "WinHTTP error " + std::to_string(err);
}

}  // namespace

class WinHttpWSConnection : public WSConnection {
public:
    WinHttpWSConnection(std::string host, uint16_t port, std::string path, bool secure)
        : WSConnection(std::move(host), port, std::move(path), secure) {
        _init();
    }

    ~WinHttpWSConnection() override { _destroy(); }

protected:
    void _init() override {}

    void _connect() override {
        // Finish any previous worker so we can start a new one (reconnect after reset/close).
        stopWorker();

        if (!state_.tryBeginConnect()) {
            // Another worker still active (e.g. re-entrant connect from worker thread).
            return;
        }

        {
            std::lock_guard<std::mutex> lock(error_mutex_);
            pending_send_error_.clear();
        }

        worker_ = std::thread([this]() { runConnectAndReceive(); });
    }

    void _close() override {
        state_.requestClose();
        abortIo();
        stopWorker();
    }

    void _send(const Payload& payload) override {
        HINTERNET ws = websocketHandle();
        if (ws == nullptr || state_.isClosing()) {
            return;
        }

        const WINHTTP_WEB_SOCKET_BUFFER_TYPE type =
            payload.isText() ? WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE
                             : WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;

        const auto& data = payload.data();
        const DWORD hr = WinHttpWebSocketSend(
            ws, type,
            data.empty() ? nullptr : reinterpret_cast<PVOID>(const_cast<uint8_t*>(data.data())),
            static_cast<DWORD>(data.size()));
        if (hr != ERROR_SUCCESS && !state_.isClosing()) {
            std::lock_guard<std::mutex> lock(error_mutex_);
            pending_send_error_ = lastErrorString(hr);
        }
    }

    void _destroy() override { _close(); }

private:
    WsWorkerState state_;
    std::thread worker_;
    std::mutex handles_mutex_;
    std::mutex error_mutex_;
    HINTERNET session_ = nullptr;
    HINTERNET connect_ = nullptr;
    HINTERNET request_ = nullptr;
    HINTERNET websocket_ = nullptr;
    std::string pending_send_error_;

    HINTERNET websocketHandle() {
        std::lock_guard<std::mutex> lock(handles_mutex_);
        return websocket_;
    }

    void abortIo() {
        HINTERNET ws = websocketHandle();
        if (ws != nullptr) {
            WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        }
        closeHandles();
    }

    void closeHandles() {
        std::lock_guard<std::mutex> lock(handles_mutex_);
        closeHandlesUnlocked();
    }

    void closeHandlesUnlocked() {
        if (websocket_ != nullptr) {
            WinHttpCloseHandle(websocket_);
            websocket_ = nullptr;
        }
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

    // Join worker if running (no-op if already joined or called from worker thread).
    void stopWorker() {
        if (!worker_.joinable()) {
            state_.onWorkerFinished();
            return;
        }
        if (std::this_thread::get_id() == worker_.get_id()) {
            // Worker is tearing down via _close/_destroy from its own stack; join later.
            return;
        }
        worker_.join();
        state_.onWorkerFinished();
    }

    // Notify once for an abandoned attempt (never reached onOpen, or local close).
    void notifyAbandoned(bool& notified, const std::string& error_if_not_closing) {
        if (notified) {
            return;
        }
        notified = true;
        if (WsWorkerState::abandonNotify(state_.isClosing()) == WsWorkerState::AbandonNotify::Close) {
            onClose(1000, "");
        } else {
            onError(error_if_not_closing);
        }
    }

    void runConnectAndReceive() {
        bool notified = false;

        // Ensure flags clear when this thread exits, even if join races with stopWorker.
        struct FinishGuard {
            WsWorkerState& state;
            ~FinishGuard() { state.onWorkerFinished(); }
        } finish_guard{state_};

        const std::wstring host_w = utf8ToWide(host_);
        const std::wstring path_w = utf8ToWide(path_.empty() ? "/" : path_);
        if (host_w.empty() || path_w.empty()) {
            notifyAbandoned(notified, "Invalid WebSocket host or path encoding");
            return;
        }

        DWORD setup_err = ERROR_SUCCESS;
        {
            std::lock_guard<std::mutex> lock(handles_mutex_);
            session_ = WinHttpOpen(L"cells/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (session_ == nullptr) {
                setup_err = GetLastError();
            } else {
                connect_ =
                    WinHttpConnect(session_, host_w.c_str(), static_cast<INTERNET_PORT>(port_), 0);
                if (connect_ == nullptr) {
                    setup_err = GetLastError();
                    closeHandlesUnlocked();
                } else {
                    const DWORD flags = secure_ ? WINHTTP_FLAG_SECURE : 0;
                    request_ =
                        WinHttpOpenRequest(connect_, L"GET", path_w.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
                    if (request_ == nullptr) {
                        setup_err = GetLastError();
                        closeHandlesUnlocked();
                    } else if (!WinHttpSetOption(request_, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
                                                 nullptr, 0)) {
                        setup_err = GetLastError();
                        closeHandlesUnlocked();
                    }
                }
            }
        }
        if (setup_err != ERROR_SUCCESS) {
            notifyAbandoned(notified, lastErrorString(setup_err));
            return;
        }
        if (state_.isClosing()) {
            closeHandles();
            notifyAbandoned(notified, "");
            return;
        }

        HINTERNET req = nullptr;
        {
            std::lock_guard<std::mutex> lock(handles_mutex_);
            req = request_;
        }
        if (req == nullptr) {
            // Handles aborted during setup (close from another thread).
            notifyAbandoned(notified, "WebSocket request aborted");
            return;
        }

        if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                                0)) {
            const DWORD err = GetLastError();
            closeHandles();
            notifyAbandoned(notified, lastErrorString(err));
            return;
        }

        if (!WinHttpReceiveResponse(req, nullptr)) {
            const DWORD err = GetLastError();
            closeHandles();
            notifyAbandoned(notified, lastErrorString(err));
            return;
        }

        DWORD upgrade_err = ERROR_SUCCESS;
        {
            std::lock_guard<std::mutex> lock(handles_mutex_);
            if (request_ == nullptr) {
                upgrade_err = ERROR_INVALID_HANDLE;
            } else {
                websocket_ = WinHttpWebSocketCompleteUpgrade(request_, 0);
                if (websocket_ == nullptr) {
                    upgrade_err = GetLastError();
                    closeHandlesUnlocked();
                } else {
                    WinHttpCloseHandle(request_);
                    request_ = nullptr;
                }
            }
        }
        if (upgrade_err != ERROR_SUCCESS) {
            notifyAbandoned(notified, lastErrorString(upgrade_err));
            return;
        }

        if (state_.isClosing()) {
            closeHandles();
            notifyAbandoned(notified, "");
            return;
        }

        onOpen();
        notified = true;  // open established; later close/error use onClose/onError directly

        std::vector<uint8_t> buffer(8192);
        std::vector<uint8_t> message;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE message_type = WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;

        while (!state_.isClosing()) {
            {
                std::lock_guard<std::mutex> lock(error_mutex_);
                if (!pending_send_error_.empty()) {
                    const std::string err = std::move(pending_send_error_);
                    pending_send_error_.clear();
                    onError(err);
                    closeHandles();
                    return;
                }
            }

            HINTERNET ws = websocketHandle();
            if (ws == nullptr) {
                break;
            }

            DWORD bytes_read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE buffer_type;
            const DWORD hr = WinHttpWebSocketReceive(ws, buffer.data(),
                                                     static_cast<DWORD>(buffer.size()), &bytes_read,
                                                     &buffer_type);

            if (hr != ERROR_SUCCESS) {
                if (state_.isClosing()) {
                    onClose(1000, "");
                } else {
                    onError(lastErrorString(hr));
                }
                closeHandles();
                return;
            }

            switch (buffer_type) {
                case WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE:
                case WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE:
                    if (message.empty()) {
                        message_type = (buffer_type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE)
                                           ? WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE
                                           : WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
                    }
                    message.insert(message.end(), buffer.begin(),
                                   buffer.begin() + static_cast<std::ptrdiff_t>(bytes_read));
                    break;

                case WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE:
                case WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE: {
                    if (!message.empty()) {
                        message.insert(message.end(), buffer.begin(),
                                       buffer.begin() + static_cast<std::ptrdiff_t>(bytes_read));
                        deliverMessage(message_type, message);
                        message.clear();
                    } else {
                        std::vector<uint8_t> single(
                            buffer.begin(),
                            buffer.begin() + static_cast<std::ptrdiff_t>(bytes_read));
                        deliverMessage(buffer_type, single);
                    }
                    break;
                }

                case WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE: {
                    USHORT status = 0;
                    BYTE reason[123] = {};
                    DWORD reason_len = 0;
                    HINTERNET close_ws = websocketHandle();
                    if (close_ws != nullptr) {
                        WinHttpWebSocketQueryCloseStatus(close_ws, &status, reason, sizeof(reason),
                                                         &reason_len);
                    }
                    std::string reason_str;
                    if (reason_len > 0) {
                        reason_str.assign(reinterpret_cast<char*>(reason), reason_len);
                    }
                    onClose(status, reason_str);
                    closeHandles();
                    return;
                }

                default:
                    break;
            }
        }

        // Local close while in receive loop (or handles gone).
        onClose(1000, "");
        closeHandles();
    }

    void deliverMessage(WINHTTP_WEB_SOCKET_BUFFER_TYPE type, const std::vector<uint8_t>& data) {
        if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
            onMessage(Payload(std::string(data.begin(), data.end())));
        } else {
            onMessage(Payload(data));
        }
    }
};

std::unique_ptr<WSConnection> WSConnection::make(const std::string& scheme, const std::string& host,
                                                 uint16_t port, const std::string& path) {
    const bool secure = (scheme == "wss" || scheme == "https");
    return std::make_unique<WinHttpWSConnection>(host, port, path, secure);
}

}  // namespace cells::net

#endif  // _WIN32 && !__EMSCRIPTEN__
