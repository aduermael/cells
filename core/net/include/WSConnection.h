// WebSocket connection abstraction with platform-specific implementations
// Follows xptools pattern: common interface with platform hooks

#ifndef CELLS_NET_WS_CONNECTION_H
#define CELLS_NET_WS_CONNECTION_H

#include <cstdint>

#include <memory>
#include <string>

#include "core/net/include/Connection.h"
#include "core/net/include/URL.h"

namespace cells::net {

// WebSocket connection for real-time bidirectional communication
// Platform-specific implementations in web/WSConnection_web.cc and
// apple/WSConnection.mm
class WSConnection : public Connection {
public:
    ~WSConnection() override = default;

    // Factory methods - create platform-specific implementation
    static std::unique_ptr<WSConnection> make(const std::string& url);
    static std::unique_ptr<WSConnection> make(const URL& url);
    static std::unique_ptr<WSConnection> make(const std::string& scheme, const std::string& host,
                                              uint16_t port, const std::string& path);

    // Connection interface
    void connect() override;
    void close() override;
    void reset() override;
    void send(const Payload& payload) override;

    // URL information
    [[nodiscard]] const std::string& getURLString() const { return url_string_; }
    [[nodiscard]] const std::string& getHost() const { return host_; }
    [[nodiscard]] uint16_t getPort() const { return port_; }
    [[nodiscard]] const std::string& getPath() const { return path_; }
    [[nodiscard]] bool isSecure() const { return secure_; }

protected:
    WSConnection(std::string host, uint16_t port, std::string path, bool secure);

    // Platform-specific hooks (implemented per platform)
    virtual void _init() = 0;                        // Initialize platform-specific resources
    virtual void _connect() = 0;                     // Start connection
    virtual void _close() = 0;                       // Close connection
    virtual void _send(const Payload& payload) = 0;  // Send data
    virtual void _destroy() = 0;                     // Clean up platform-specific resources

    // Called by platform implementation
    void onOpen();
    void onMessage(const Payload& payload);
    void onClose(uint16_t code, const std::string& reason);
    void onError(const std::string& error);

    // Connection parameters
    std::string url_string_;
    std::string host_;
    uint16_t port_;
    std::string path_;
    bool secure_;
};

}  // namespace cells::net

#endif  // CELLS_NET_WS_CONNECTION_H
