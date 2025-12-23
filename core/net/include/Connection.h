// Base connection class for real-time communication
// Abstract interface for WebSocket and other connection types

#ifndef CELLS_NET_CONNECTION_H
#define CELLS_NET_CONNECTION_H

#include <cstdint>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cells::net {

// Connection status
enum class ConnectionStatus : std::uint8_t {
    IDLE,             // Not connected
    CONNECTING,       // Connection in progress
    OK,               // Connected and ready
    CLOSED_ON_ERROR,  // Closed due to error
    CLOSED            // Closed normally
};

// Payload for framed messages (can carry metadata)
class Payload {
public:
    Payload() = default;
    explicit Payload(std::vector<uint8_t> data) : data_(std::move(data)) {}
    explicit Payload(const std::string& text) : data_(text.begin(), text.end()), is_text_(true) {}

    // Data access
    [[nodiscard]] const std::vector<uint8_t>& data() const { return data_; }
    [[nodiscard]] std::vector<uint8_t>& data() { return data_; }

    // Text/binary flag
    [[nodiscard]] bool isText() const { return is_text_; }
    void setIsText(bool is_text) { is_text_ = is_text; }

    // Convenience for text payloads
    [[nodiscard]] std::string asString() const { return {data_.begin(), data_.end()}; }

    // Size
    [[nodiscard]] size_t size() const { return data_.size(); }
    [[nodiscard]] bool empty() const { return data_.empty(); }

private:
    std::vector<uint8_t> data_;
    bool is_text_ = false;  // WebSocket text vs binary frame
};

// Forward declaration
class Connection;

// Delegate interface for connection events
class ConnectionDelegate {
public:
    virtual ~ConnectionDelegate() = default;

    // Connection established successfully
    virtual void connectionDidEstablish(Connection& connection) = 0;

    // Received data from remote
    virtual void connectionDidReceive(Connection& connection, const Payload& payload) = 0;

    // Connection closed (check connection.getStatus() for reason)
    virtual void connectionDidClose(Connection& connection) = 0;

    // Optional: Connection error (called before close on error)
    virtual void connectionDidError(Connection& connection, const std::string& error) {
        (void)connection;
        (void)error;
    }
};

// Base class for real-time connections
// Subclasses implement specific protocols (WebSocket, etc.)
class Connection {
public:
    virtual ~Connection() = default;

    // Connection lifecycle
    virtual void connect() = 0;
    virtual void close() = 0;
    virtual void reset() = 0;  // Close and prepare for reconnection

    // Send data
    virtual void send(const Payload& payload) = 0;
    void send(const std::string& text) { send(Payload(text)); }
    void sendBinary(const std::vector<uint8_t>& data) { send(Payload(data)); }

    // Delegate for events
    void setDelegate(ConnectionDelegate* delegate) { delegate_ = delegate; }
    [[nodiscard]] ConnectionDelegate* getDelegate() const { return delegate_; }

    // Status
    [[nodiscard]] ConnectionStatus getStatus() const { return status_; }
    [[nodiscard]] bool isConnected() const { return status_ == ConnectionStatus::OK; }
    [[nodiscard]] bool isConnecting() const { return status_ == ConnectionStatus::CONNECTING; }

    // Error information (if status is CLOSED_ON_ERROR)
    [[nodiscard]] const std::string& getError() const { return error_; }

protected:
    Connection() = default;

    // Called by subclasses to notify delegate
    void notifyEstablished();
    void notifyReceived(const Payload& payload);
    void notifyClosed();
    void notifyError(const std::string& error);

    // Status management
    void setStatus(ConnectionStatus status) { status_ = status; }
    void setError(const std::string& error) { error_ = error; }

    ConnectionDelegate* delegate_ = nullptr;
    ConnectionStatus status_ = ConnectionStatus::IDLE;
    std::string error_;
};

// Convert ConnectionStatus to string (for logging/debugging)
const char* connectionStatusToString(ConnectionStatus status);

}  // namespace cells::net

#endif  // CELLS_NET_CONNECTION_H
