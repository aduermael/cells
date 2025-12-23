// RTCDataChannel abstraction for peer-to-peer data communication
// Platform-specific implementations in web/ and apple/ directories

#ifndef CELLS_NET_RTC_DATA_CHANNEL_H
#define CELLS_NET_RTC_DATA_CHANNEL_H

#include <cstdint>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace cells::net {

// Forward declaration
class RTCDataChannel;

// DataChannel ready state
enum class DataChannelState : std::uint8_t {
    CONNECTING,  // Channel is being established
    OPEN,        // Channel is open and ready for communication
    CLOSING,     // Channel is in the process of closing
    CLOSED       // Channel is closed
};

// Configuration for creating a DataChannel
struct DataChannelConfig {
    bool ordered = true;            // Whether messages are delivered in order
    int max_retransmits = -1;       // Max retransmissions (-1 = unlimited)
    int max_packet_life_time = -1;  // Max time to retransmit in ms (-1 = unlimited)
    std::string protocol;           // Sub-protocol name (optional)
    bool negotiated = false;        // Whether channel is negotiated out-of-band
    int id = -1;                    // Channel ID (-1 = auto-assign)

    // Convenience for unreliable unordered channel (good for presence)
    static DataChannelConfig unreliable() {
        DataChannelConfig config;
        config.ordered = false;
        config.max_retransmits = 0;
        return config;
    }

    // Convenience for reliable ordered channel (good for operations)
    static DataChannelConfig reliable() { return DataChannelConfig{}; }
};

// Delegate interface for DataChannel events
class DataChannelDelegate {
public:
    virtual ~DataChannelDelegate() = default;

    // Channel is open and ready for communication
    virtual void dataChannelDidOpen(RTCDataChannel& channel) = 0;

    // Channel is closed
    virtual void dataChannelDidClose(RTCDataChannel& channel) = 0;

    // Received a text message
    virtual void dataChannelDidReceiveMessage(RTCDataChannel& channel,
                                              const std::string& message) = 0;

    // Received binary data
    virtual void dataChannelDidReceiveData(RTCDataChannel& channel,
                                           const std::vector<uint8_t>& data) = 0;

    // Optional: Error occurred
    virtual void dataChannelDidError(RTCDataChannel& channel, const std::string& error) {
        (void)channel;
        (void)error;
    }

    // Optional: Buffered amount changed (for flow control)
    virtual void dataChannelBufferedAmountDidChange(RTCDataChannel& channel,
                                                    uint64_t previous_amount) {
        (void)channel;
        (void)previous_amount;
    }
};

// RTCDataChannel - bidirectional data channel within a peer connection
// Created via RTCPeerConnection::createDataChannel() or received via onDataChannel callback
class RTCDataChannel {
public:
    virtual ~RTCDataChannel() = default;

    // Channel identification
    [[nodiscard]] const std::string& getLabel() const { return label_; }
    [[nodiscard]] int getId() const { return id_; }

    // Channel state
    [[nodiscard]] DataChannelState getState() const { return state_; }
    [[nodiscard]] bool isOpen() const { return state_ == DataChannelState::OPEN; }

    // Channel configuration (read-only after creation)
    [[nodiscard]] bool isOrdered() const { return ordered_; }
    [[nodiscard]] int getMaxRetransmits() const { return max_retransmits_; }
    [[nodiscard]] int getMaxPacketLifeTime() const { return max_packet_life_time_; }
    [[nodiscard]] const std::string& getProtocol() const { return protocol_; }
    [[nodiscard]] bool isNegotiated() const { return negotiated_; }

    // Send data
    virtual bool send(const std::string& message) = 0;
    virtual bool sendBinary(const std::vector<uint8_t>& data) = 0;

    // Close the channel
    virtual void close() = 0;

    // Flow control
    [[nodiscard]] virtual uint64_t getBufferedAmount() const = 0;
    virtual void setBufferedAmountLowThreshold(uint64_t threshold) = 0;
    [[nodiscard]] virtual uint64_t getBufferedAmountLowThreshold() const = 0;

    // Delegate for events
    void setDelegate(DataChannelDelegate* delegate) { delegate_ = delegate; }
    [[nodiscard]] DataChannelDelegate* getDelegate() const { return delegate_; }

    // Configuration setters (for platform implementations during construction)
    void setLabel(std::string label) { label_ = std::move(label); }
    void setId(int id) { id_ = id; }
    void setOrdered(bool ordered) { ordered_ = ordered; }
    void setMaxRetransmits(int max) { max_retransmits_ = max; }
    void setMaxPacketLifeTime(int max) { max_packet_life_time_ = max; }
    void setProtocol(std::string protocol) { protocol_ = std::move(protocol); }
    void setNegotiated(bool negotiated) { negotiated_ = negotiated; }

protected:
    RTCDataChannel() = default;

    // Called by platform implementations
    void notifyOpen();
    void notifyClose();
    void notifyMessage(const std::string& message);
    void notifyData(const std::vector<uint8_t>& data);
    void notifyError(const std::string& error);
    void notifyBufferedAmountChange(uint64_t previous_amount);

    // State management
    void setState(DataChannelState state) { state_ = state; }

    DataChannelDelegate* delegate_ = nullptr;
    DataChannelState state_ = DataChannelState::CONNECTING;
    std::string label_;
    int id_ = -1;
    bool ordered_ = true;
    int max_retransmits_ = -1;
    int max_packet_life_time_ = -1;
    std::string protocol_;
    bool negotiated_ = false;
};

// Convert DataChannelState to string (for logging/debugging)
const char* dataChannelStateToString(DataChannelState state);

}  // namespace cells::net

#endif  // CELLS_NET_RTC_DATA_CHANNEL_H
