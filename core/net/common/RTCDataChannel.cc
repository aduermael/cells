// Common RTCDataChannel implementation
// Shared logic for all platforms

#include "core/net/include/RTCDataChannel.h"

namespace cells::net {

// Delegate notification methods

void RTCDataChannel::notifyOpen() {
    state_ = DataChannelState::OPEN;
    if (delegate_) {
        delegate_->dataChannelDidOpen(*this);
    }
}

void RTCDataChannel::notifyClose() {
    state_ = DataChannelState::CLOSED;
    if (delegate_) {
        delegate_->dataChannelDidClose(*this);
    }
}

void RTCDataChannel::notifyMessage(const std::string& message) {
    if (delegate_) {
        delegate_->dataChannelDidReceiveMessage(*this, message);
    }
}

void RTCDataChannel::notifyData(const std::vector<uint8_t>& data) {
    if (delegate_) {
        delegate_->dataChannelDidReceiveData(*this, data);
    }
}

void RTCDataChannel::notifyError(const std::string& error) {
    if (delegate_) {
        delegate_->dataChannelDidError(*this, error);
    }
}

void RTCDataChannel::notifyBufferedAmountChange(uint64_t previous_amount) {
    if (delegate_) {
        delegate_->dataChannelBufferedAmountDidChange(*this, previous_amount);
    }
}

// Utility functions

const char* dataChannelStateToString(DataChannelState state) {
    switch (state) {
        case DataChannelState::CONNECTING:
            return "connecting";
        case DataChannelState::OPEN:
            return "open";
        case DataChannelState::CLOSING:
            return "closing";
        case DataChannelState::CLOSED:
            return "closed";
    }
    return "unknown";
}

}  // namespace cells::net
