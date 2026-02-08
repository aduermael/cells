// Linux/native WebSocket implementation using libdatachannel
// Uses rtc::WebSocket from libdatachannel which is already a dependency for WebRTC
// Reference: https://github.com/paullouisageneau/libdatachannel

#if !defined(__EMSCRIPTEN__) && !defined(__APPLE__)

#include <memory>
#include <mutex>
#include <rtc/rtc.hpp>
#include <string>

#include "core/net/include/WSConnection.h"

namespace cells::net {

class LibdcWSConnection : public WSConnection {
public:
    LibdcWSConnection(std::string host, uint16_t port, std::string path, bool secure)
        : WSConnection(std::move(host), port, std::move(path), secure) {
        _init();
    }

    ~LibdcWSConnection() override { _destroy(); }

protected:
    void _init() override {
        // Create WebSocket with default configuration
        ws_ = std::make_shared<rtc::WebSocket>();

        // Set up callbacks
        // Capture weak_ptr to avoid preventing destruction
        std::weak_ptr<rtc::WebSocket> weak_ws = ws_;
        LibdcWSConnection* self = this;

        ws_->onOpen([self, weak_ws]() {
            // Check if WebSocket still exists
            if (weak_ws.expired()) {
                return;
            }
            self->onOpen();
        });

        ws_->onMessage([self, weak_ws](rtc::message_variant data) {
            if (weak_ws.expired()) {
                return;
            }
            Payload payload;
            if (std::holds_alternative<rtc::string>(data)) {
                payload = Payload(std::get<rtc::string>(data));
            } else {
                const auto& binary = std::get<rtc::binary>(data);
                std::vector<uint8_t> bytes(binary.begin(), binary.end());
                payload = Payload(std::move(bytes));
            }
            self->onMessage(payload);
        });

        ws_->onError([self, weak_ws](std::string error) {
            if (weak_ws.expired()) {
                return;
            }
            self->onError(error);
        });

        ws_->onClosed([self, weak_ws]() {
            if (weak_ws.expired()) {
                return;
            }
            // Normal close - code 1000, no reason
            self->onClose(1000, "");
        });
    }

    void _connect() override {
        if (ws_) {
            ws_->open(url_string_);
        }
    }

    void _close() override {
        if (ws_) {
            ws_->close();
        }
    }

    void _send(const Payload& payload) override {
        if (!ws_ || !ws_->isOpen()) {
            return;
        }

        if (payload.isText()) {
            ws_->send(payload.asString());
        } else {
            // Convert vector<uint8_t> to rtc::binary (vector<byte>)
            const auto& data = payload.data();
            rtc::binary binary(data.size());
            std::memcpy(binary.data(), data.data(), data.size());
            ws_->send(binary);
        }
    }

    void _destroy() override {
        if (ws_) {
            // Reset callbacks to prevent any further calls
            ws_->resetCallbacks();

            // Close if still open
            if (ws_->isOpen()) {
                ws_->forceClose();
            }

            ws_.reset();
        }
    }

private:
    std::shared_ptr<rtc::WebSocket> ws_;
};

// Factory implementation for Linux/native platforms
std::unique_ptr<WSConnection> WSConnection::make(const std::string& scheme, const std::string& host,
                                                 uint16_t port, const std::string& path) {
    const bool secure = (scheme == "wss" || scheme == "https");
    return std::make_unique<LibdcWSConnection>(host, port, path, secure);
}

}  // namespace cells::net

#endif  // !__EMSCRIPTEN__ && !__APPLE__
