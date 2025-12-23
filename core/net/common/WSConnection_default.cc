// Default (stub) WebSocket implementation
// Used for platforms without native WebSocket support
// Note: Apple has a native implementation but it requires objc_library, so for now
// we fall back to stub on macOS as well when building with cc_library

#if !defined(__EMSCRIPTEN__)

#include "core/net/include/WSConnection.h"

namespace cells::net {

class DefaultWSConnection : public WSConnection {
public:
    DefaultWSConnection(std::string host, uint16_t port, std::string path, bool secure)
        : WSConnection(std::move(host), port, std::move(path), secure) {}

protected:
    void _init() override {}

    void _connect() override {
        // Stub: immediately report error
        onError("WebSocket not supported on this platform");
    }

    void _close() override {}

    void _send(const Payload& /*payload*/) override {}

    void _destroy() override {}
};

// Factory implementation for unsupported platforms
std::unique_ptr<WSConnection> WSConnection::make(const std::string& scheme, const std::string& host,
                                                 uint16_t port, const std::string& path) {
    const bool secure = (scheme == "wss" || scheme == "https");
    return std::make_unique<DefaultWSConnection>(host, port, path, secure);
}

}  // namespace cells::net

#endif  // !__EMSCRIPTEN__
