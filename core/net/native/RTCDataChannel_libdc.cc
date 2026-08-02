// RTCDataChannel implementation using libdatachannel
// For native platforms (macOS, Linux, Windows, etc.) - not emscripten

#if !defined(__EMSCRIPTEN__)

#include <cstring>

#include <memory>
#include <rtc/rtc.hpp>
#include <string>
#include <vector>

#include "core/net/include/RTCDataChannel.h"

namespace cells::net {

// Forward declaration for the factory function used by RTCPeerConnection_libdc.cc
class LibdcDataChannel;
std::unique_ptr<RTCDataChannel> createLibdcDataChannel(std::shared_ptr<rtc::DataChannel> dc);

// LibdcDataChannel wraps rtc::DataChannel from libdatachannel
class LibdcDataChannel : public RTCDataChannel {
public:
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init) - dc_ is initialized in init list
    explicit LibdcDataChannel(std::shared_ptr<rtc::DataChannel> dc) : dc_(std::move(dc)) {
        if (!dc_) {
            setState(DataChannelState::CLOSED);
            return;
        }

        // Extract configuration from the libdatachannel channel
        setLabel(dc_->label());
        setProtocol(dc_->protocol());
        auto id_opt = dc_->id();
        if (id_opt) {
            setId(static_cast<int>(*id_opt));
        }

        // Extract reliability settings
        auto reliability = dc_->reliability();
        setOrdered(!reliability.unordered);
        if (reliability.maxRetransmits) {
            setMaxRetransmits(static_cast<int>(*reliability.maxRetransmits));
        }
        if (reliability.maxPacketLifeTime) {
            setMaxPacketLifeTime(static_cast<int>(reliability.maxPacketLifeTime->count()));
        }

        // Set initial state
        if (dc_->isOpen()) {
            setState(DataChannelState::OPEN);
        } else if (dc_->isClosed()) {
            setState(DataChannelState::CLOSED);
        } else {
            setState(DataChannelState::CONNECTING);
        }

        // Set up callbacks
        dc_->onOpen([this]() {
            setState(DataChannelState::OPEN);
            notifyOpen();
        });

        dc_->onClosed([this]() {
            setState(DataChannelState::CLOSED);
            notifyClose();
        });

        dc_->onError([this](std::string error) { notifyError(error); });

        dc_->onMessage([this](rtc::message_variant data) {
            if (std::holds_alternative<rtc::binary>(data)) {
                const auto& binary = std::get<rtc::binary>(data);
                std::vector<uint8_t> bytes(binary.size());
                std::memcpy(bytes.data(), binary.data(), binary.size());
                notifyData(bytes);
            } else {
                notifyMessage(std::get<std::string>(data));
            }
        });
    }

    ~LibdcDataChannel() override {
        if (dc_) {
            dc_->resetCallbacks();
        }
    }

    bool send(const std::string& message) override {
        if (!dc_ || !dc_->isOpen()) {
            return false;
        }
        return dc_->send(message);
    }

    bool sendBinary(const std::vector<uint8_t>& data) override {
        if (!dc_ || !dc_->isOpen()) {
            return false;
        }
        rtc::binary binary(data.size());
        std::memcpy(binary.data(), data.data(), data.size());
        return dc_->send(std::move(binary));
    }

    void close() override {
        if (dc_) {
            setState(DataChannelState::CLOSING);
            dc_->close();
        }
    }

    [[nodiscard]] uint64_t getBufferedAmount() const override {
        if (!dc_) {
            return 0;
        }
        return dc_->bufferedAmount();
    }

    void setBufferedAmountLowThreshold(uint64_t threshold) override {
        if (dc_) {
            dc_->setBufferedAmountLowThreshold(static_cast<size_t>(threshold));
        }
        buffered_amount_low_threshold_ = threshold;
    }

    [[nodiscard]] uint64_t getBufferedAmountLowThreshold() const override {
        return buffered_amount_low_threshold_;
    }

private:
    std::shared_ptr<rtc::DataChannel> dc_;
    uint64_t buffered_amount_low_threshold_ = 0;
};

// Factory function called by RTCPeerConnection_libdc.cc
std::unique_ptr<RTCDataChannel> createLibdcDataChannel(std::shared_ptr<rtc::DataChannel> dc) {
    return std::make_unique<LibdcDataChannel>(std::move(dc));
}

}  // namespace cells::net

#endif  // !__EMSCRIPTEN__
