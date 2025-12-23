// Common WebSocket connection implementation
// Shared logic for all platforms; platform-specific code in web/ and apple/

#include "core/net/include/WSConnection.h"

namespace cells::net {

WSConnection::WSConnection(std::string host, uint16_t port, std::string path, bool secure)
    : host_(std::move(host)), port_(port), path_(std::move(path)), secure_(secure) {
    // Build URL string
    url_string_ = secure_ ? "wss://" : "ws://";
    url_string_ += host_;
    if ((secure_ && port_ != 443) || (!secure_ && port_ != 80)) {
        url_string_ += ":" + std::to_string(port_);
    }
    url_string_ += path_;
}

std::unique_ptr<WSConnection> WSConnection::make(const URL& url) {
    return make(url.getScheme(), url.getHost(), url.getEffectivePort(), url.getPath());
}

std::unique_ptr<WSConnection> WSConnection::make(const std::string& url) {
    auto parsed = URL::parse(url);
    if (!parsed) {
        return nullptr;  // Invalid URL
    }
    return make(*parsed);
}

void WSConnection::connect() {
    if (status_ != ConnectionStatus::IDLE) {
        return;  // Already connecting or connected
    }
    status_ = ConnectionStatus::CONNECTING;
    _connect();
}

void WSConnection::close() {
    if (status_ == ConnectionStatus::IDLE || status_ == ConnectionStatus::CLOSED ||
        status_ == ConnectionStatus::CLOSED_ON_ERROR) {
        return;  // Already closed
    }
    _close();
}

void WSConnection::reset() {
    _close();
    status_ = ConnectionStatus::IDLE;
    error_.clear();
}

void WSConnection::send(const Payload& payload) {
    if (status_ != ConnectionStatus::OK) {
        return;  // Not connected
    }
    _send(payload);
}

void WSConnection::onOpen() {
    status_ = ConnectionStatus::OK;
    notifyEstablished();
}

void WSConnection::onMessage(const Payload& payload) {
    notifyReceived(payload);
}

void WSConnection::onClose(uint16_t code, const std::string& reason) {
    (void)code;
    if (!reason.empty()) {
        error_ = reason;
        status_ = ConnectionStatus::CLOSED_ON_ERROR;
    } else {
        status_ = ConnectionStatus::CLOSED;
    }
    notifyClosed();
}

void WSConnection::onError(const std::string& error) {
    error_ = error;
    status_ = ConnectionStatus::CLOSED_ON_ERROR;
    notifyError(error);
    notifyClosed();
}

}  // namespace cells::net
