// Web (Emscripten) WebSocket implementation
// Uses emscripten_websocket API for WebSocket connections

#ifdef __EMSCRIPTEN__

#include <emscripten/websocket.h>

#include "core/net/include/WSConnection.h"

namespace cells::net {

class WebWSConnection : public WSConnection {
public:
    WebWSConnection(std::string host, uint16_t port, std::string path, bool secure)
        : WSConnection(std::move(host), port, std::move(path), secure) {
        _init();
    }

    ~WebWSConnection() override { _destroy(); }

protected:
    void _init() override {
        // Nothing to initialize until connect() is called
    }

    void _connect() override {
        EmscriptenWebSocketCreateAttributes attrs = {
            url_string_.c_str(),
            nullptr,  // protocols
            EM_TRUE   // createOnMainThread
        };

        socket_ = emscripten_websocket_new(&attrs);
        if (socket_ <= 0) {
            onError("Failed to create WebSocket");
            return;
        }

        // Set callbacks
        emscripten_websocket_set_onopen_callback(socket_, this, &WebWSConnection::onOpenCallback);
        emscripten_websocket_set_onclose_callback(socket_, this, &WebWSConnection::onCloseCallback);
        emscripten_websocket_set_onerror_callback(socket_, this, &WebWSConnection::onErrorCallback);
        emscripten_websocket_set_onmessage_callback(socket_, this,
                                                    &WebWSConnection::onMessageCallback);
    }

    void _close() override {
        if (socket_ > 0) {
            emscripten_websocket_close(socket_, 1000, "Normal closure");
            emscripten_websocket_delete(socket_);
            socket_ = 0;
        }
    }

    void _send(const Payload& payload) override {
        if (socket_ <= 0) {
            return;
        }

        if (payload.isText()) {
            emscripten_websocket_send_utf8_text(socket_, payload.asString().c_str());
        } else {
            emscripten_websocket_send_binary(socket_, payload.data().data(), payload.data().size());
        }
    }

    void _destroy() override {
        if (socket_ > 0) {
            emscripten_websocket_delete(socket_);
            socket_ = 0;
        }
    }

private:
    EMSCRIPTEN_WEBSOCKET_T socket_ = 0;

    static EM_BOOL onOpenCallback(int /*eventType*/, const EmscriptenWebSocketOpenEvent* /*event*/,
                                  void* userData) {
        auto* ws = static_cast<WebWSConnection*>(userData);
        if (ws != nullptr) {
            ws->onOpen();
        }
        return EM_TRUE;
    }

    static EM_BOOL onCloseCallback(int /*eventType*/, const EmscriptenWebSocketCloseEvent* event,
                                   void* userData) {
        auto* ws = static_cast<WebWSConnection*>(userData);
        if (ws != nullptr) {
            std::string reason;
            if (event->reason[0] != '\0') {
                reason = event->reason;
            }
            ws->onClose(event->code, reason);
        }
        return EM_TRUE;
    }

    static EM_BOOL onErrorCallback(int /*eventType*/,
                                   const EmscriptenWebSocketErrorEvent* /*event*/, void* userData) {
        auto* ws = static_cast<WebWSConnection*>(userData);
        if (ws != nullptr) {
            ws->onError("WebSocket error");
        }
        return EM_TRUE;
    }

    static EM_BOOL onMessageCallback(int /*eventType*/,
                                     const EmscriptenWebSocketMessageEvent* event, void* userData) {
        auto* ws = static_cast<WebWSConnection*>(userData);
        if (ws == nullptr || event->data == nullptr) {
            return EM_TRUE;
        }

        Payload payload;
        if (event->isText != 0) {
            // Text message
            payload = Payload(std::string(reinterpret_cast<const char*>(event->data),
                                          static_cast<size_t>(event->numBytes)));
        } else {
            // Binary message
            std::vector<uint8_t> data(event->data, event->data + event->numBytes);
            payload = Payload(std::move(data));
        }

        ws->onMessage(payload);
        return EM_TRUE;
    }
};

// Factory implementation for web
std::unique_ptr<WSConnection> WSConnection::make(const std::string& scheme, const std::string& host,
                                                 uint16_t port, const std::string& path) {
    bool secure = (scheme == "wss" || scheme == "https");
    return std::make_unique<WebWSConnection>(host, port, path, secure);
}

}  // namespace cells::net

#endif  // __EMSCRIPTEN__
