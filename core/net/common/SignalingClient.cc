// SignalingClient implementation
// Uses WSConnection for WebSocket transport

#include "core/net/include/SignalingClient.h"

#include <random>

#include "core/log/include/Logger.h"
#include "core/net/include/SignalingProtocol.h"

namespace cells::net {

const char* signalingClientStateToString(SignalingClientState state) {
    switch (state) {
        case SignalingClientState::DISCONNECTED:
            return "DISCONNECTED";
        case SignalingClientState::CONNECTING:
            return "CONNECTING";
        case SignalingClientState::CONNECTED:
            return "CONNECTED";
        case SignalingClientState::RECONNECTING:
            return "RECONNECTING";
        case SignalingClientState::IN_ROOM:
            return "IN_ROOM";
    }
    return "UNKNOWN";
}

SignalingClient::SignalingClient(SignalingClientConfig config)
    : config_(std::move(config)), current_reconnect_delay_ms_(config_.reconnect_delay_ms) {}

SignalingClient::~SignalingClient() {
    cancelReconnect();
    if (ws_) {
        ws_->setDelegate(nullptr);
        ws_->close();
    }
}

void SignalingClient::connect(const std::string& room_id, const std::string& peer_id) {
    room_id_ = room_id;
    peer_id_ = peer_id;
    should_reconnect_ = true;

    LOG_INFO("[Signaling] Connecting to %s", config_.url.c_str());

    // If already connected, just join the new room
    if (ws_ && ws_->isConnected()) {
        sendJoin();
        return;
    }

    setState(SignalingClientState::CONNECTING);

    // Create WebSocket connection
    ws_ = WSConnection::make(config_.url);
    if (!ws_) {
        LOG_ERROR("[Signaling] Failed to create WebSocket");
        setState(SignalingClientState::DISCONNECTED);
        return;
    }

    ws_->setDelegate(this);
    ws_->connect();
}

void SignalingClient::disconnect() {
    should_reconnect_ = false;
    cancelReconnect();

    if (ws_) {
        // Send leave message if in a room
        if (!room_id_.empty() && ws_->isConnected()) {
            sendMessage(SignalingProtocol::buildLeaveMessage(room_id_, peer_id_));
        }

        ws_->setDelegate(nullptr);
        ws_->close();
        ws_.reset();
    }

    room_id_.clear();
    setState(SignalingClientState::DISCONNECTED);
}

void SignalingClient::leaveRoom() {
    if (!room_id_.empty() && ws_ && ws_->isConnected()) {
        sendMessage(SignalingProtocol::buildLeaveMessage(room_id_, peer_id_));
    }
    room_id_.clear();

    if (state_ == SignalingClientState::IN_ROOM) {
        setState(SignalingClientState::CONNECTED);
    }
}

void SignalingClient::sendOffer(const std::string& target_peer, const SessionDescription& sdp) {
    if (ws_ && ws_->isConnected()) {
        sendMessage(SignalingProtocol::buildOfferMessage(target_peer, sdp));
    }
}

void SignalingClient::sendAnswer(const std::string& target_peer, const SessionDescription& sdp) {
    if (ws_ && ws_->isConnected()) {
        sendMessage(SignalingProtocol::buildAnswerMessage(target_peer, sdp));
    }
}

void SignalingClient::sendICECandidate(const std::string& target_peer,
                                       const ICECandidate& candidate) {
    if (ws_ && ws_->isConnected()) {
        sendMessage(SignalingProtocol::buildICECandidateMessage(target_peer, candidate));
    }
}

bool SignalingClient::isConnected() const {
    return ws_ && ws_->isConnected();
}

void SignalingClient::connectionDidEstablish(Connection& /*connection*/) {
    LOG_INFO("[Signaling] Connected to server");

    // Reset reconnection state on successful connect
    current_reconnect_delay_ms_ = config_.reconnect_delay_ms;
    reconnect_attempts_ = 0;

    setState(SignalingClientState::CONNECTED);

    // Automatically join room if we have a room ID
    if (!room_id_.empty()) {
        sendJoin();
    }
}

void SignalingClient::connectionDidReceive(Connection& /*connection*/, const Payload& payload) {
    if (payload.isText()) {
        handleMessage(payload.asString());
    }
}

void SignalingClient::connectionDidClose(Connection& /*connection*/) {
    LOG_INFO("[Signaling] Connection closed");

    const bool was_in_room = (state_ == SignalingClientState::IN_ROOM);
    setState(SignalingClientState::DISCONNECTED);

    // Attempt reconnection if appropriate
    if (was_in_room && should_reconnect_) {
        scheduleReconnect();
    }
}

void SignalingClient::connectionDidError(Connection& /*connection*/, const std::string& error) {
    LOG_ERROR("[Signaling] Error: %s", error.c_str());
    if (delegate_) {
        delegate_->signalingClientDidReceiveError(*this, error);
    }
}

void SignalingClient::sendMessage(const std::string& json) {
    if (ws_ && ws_->isConnected()) {
        ws_->send(Payload(json));
    }
}

void SignalingClient::handleMessage(const std::string& json) {
    const std::string type = SignalingProtocol::parseMessageType(json);

    if (type == SignalingMessageType::JOINED) {
        std::string room;
        std::vector<std::string> peers;
        if (SignalingProtocol::parseJoinedMessage(json, room, peers)) {
            setState(SignalingClientState::IN_ROOM);
            if (delegate_) {
                delegate_->signalingClientDidJoinRoom(*this, room, peers);
            }
        }
    } else if (type == SignalingMessageType::PEER_JOINED) {
        std::string peer_id;
        if (SignalingProtocol::parsePeerJoinedMessage(json, peer_id)) {
            if (delegate_) {
                delegate_->signalingClientPeerDidJoin(*this, peer_id);
            }
        }
    } else if (type == SignalingMessageType::PEER_LEFT) {
        std::string peer_id;
        if (SignalingProtocol::parsePeerLeftMessage(json, peer_id)) {
            if (delegate_) {
                delegate_->signalingClientPeerDidLeave(*this, peer_id);
            }
        }
    } else if (type == SignalingMessageType::OFFER) {
        std::string from_peer;
        SessionDescription sdp;
        if (SignalingProtocol::parseOfferMessage(json, from_peer, sdp)) {
            if (delegate_) {
                delegate_->signalingClientDidReceiveOffer(*this, from_peer, sdp);
            }
        }
    } else if (type == SignalingMessageType::ANSWER) {
        std::string from_peer;
        SessionDescription sdp;
        if (SignalingProtocol::parseAnswerMessage(json, from_peer, sdp)) {
            if (delegate_) {
                delegate_->signalingClientDidReceiveAnswer(*this, from_peer, sdp);
            }
        }
    } else if (type == SignalingMessageType::ICE_CANDIDATE) {
        std::string from_peer;
        ICECandidate candidate;
        if (SignalingProtocol::parseICECandidateMessage(json, from_peer, candidate)) {
            if (delegate_) {
                delegate_->signalingClientDidReceiveICECandidate(*this, from_peer, candidate);
            }
        }
    } else if (type == SignalingMessageType::PEER_LIST) {
        // Peer list message - we handle this via joined message with peers array
        // but log for now
    } else if (type == SignalingMessageType::ERROR) {
        std::string error;
        if (SignalingProtocol::parseErrorMessage(json, error)) {
            if (delegate_) {
                delegate_->signalingClientDidReceiveError(*this, error);
            }
        }
    }
}

void SignalingClient::sendJoin() {
    if (!room_id_.empty()) {
        sendMessage(SignalingProtocol::buildJoinMessage(room_id_, peer_id_));
    }
}

void SignalingClient::scheduleReconnect() {
    if (reconnect_attempts_ >= config_.max_reconnect_attempts) {
        if (delegate_) {
            delegate_->signalingClientMaxReconnectsReached(*this);
        }
        return;
    }

    setState(SignalingClientState::RECONNECTING);

    if (delegate_) {
        delegate_->signalingClientWillReconnect(*this, reconnect_attempts_ + 1,
                                                current_reconnect_delay_ms_);
    }

    // Note: Timer implementation is platform-specific
    // For now, we rely on the caller to call attemptReconnect() after the delay
    // In a real implementation, this would schedule a callback

    // Update delay with exponential backoff + jitter
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.85, 1.15);

    current_reconnect_delay_ms_ = static_cast<int>(std::min(
        static_cast<double>(current_reconnect_delay_ms_) * config_.reconnect_multiplier * dis(gen),
        static_cast<double>(config_.max_reconnect_delay_ms)));
}

void SignalingClient::attemptReconnect() {
    if (!should_reconnect_) {
        return;
    }

    ++reconnect_attempts_;

    // Re-create WebSocket and connect
    ws_ = WSConnection::make(config_.url);
    if (!ws_) {
        scheduleReconnect();
        return;
    }

    ws_->setDelegate(this);
    ws_->connect();
}

void SignalingClient::cancelReconnect() {
    // In a real implementation, this would cancel any scheduled timer
    should_reconnect_ = false;
}

void SignalingClient::setState(SignalingClientState new_state) {
    if (state_ == new_state) {
        return;
    }

    state_ = new_state;

    if (delegate_) {
        delegate_->signalingClientStateDidChange(*this, state_);
    }
}

}  // namespace cells::net
