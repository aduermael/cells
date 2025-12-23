// Common Connection base class implementation
// Shared logic for all connection types

#include "core/net/include/Connection.h"

namespace cells::net {

void Connection::notifyEstablished() {
    if (delegate_) {
        delegate_->connectionDidEstablish(*this);
    }
}

void Connection::notifyReceived(const Payload& payload) {
    if (delegate_) {
        delegate_->connectionDidReceive(*this, payload);
    }
}

void Connection::notifyClosed() {
    if (delegate_) {
        delegate_->connectionDidClose(*this);
    }
}

void Connection::notifyError(const std::string& error) {
    error_ = error;
    if (delegate_) {
        delegate_->connectionDidError(*this, error);
    }
}

const char* connectionStatusToString(ConnectionStatus status) {
    switch (status) {
        case ConnectionStatus::IDLE:
            return "IDLE";
        case ConnectionStatus::CONNECTING:
            return "CONNECTING";
        case ConnectionStatus::OK:
            return "OK";
        case ConnectionStatus::CLOSED_ON_ERROR:
            return "CLOSED_ON_ERROR";
        case ConnectionStatus::CLOSED:
            return "CLOSED";
    }
    return "UNKNOWN";
}

}  // namespace cells::net
