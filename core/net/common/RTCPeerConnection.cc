// Common RTCPeerConnection implementation
// Shared logic for all platforms

#include "core/net/include/RTCPeerConnection.h"

#include <algorithm>

namespace cells::net {

// Delegate notification methods

void RTCPeerConnection::notifyConnectionStateChange(PeerConnectionState state) {
    if (delegate_) {
        delegate_->peerConnectionStateDidChange(*this, state);
    }
}

void RTCPeerConnection::notifyICEConnectionStateChange(ICEConnectionState state) {
    if (delegate_) {
        delegate_->peerConnectionICEStateDidChange(*this, state);
    }
}

void RTCPeerConnection::notifyICEGatheringStateChange(ICEGatheringState state) {
    if (delegate_) {
        delegate_->peerConnectionICEGatheringStateDidChange(*this, state);
    }
}

void RTCPeerConnection::notifySignalingStateChange(SignalingState state) {
    if (delegate_) {
        delegate_->peerConnectionSignalingStateDidChange(*this, state);
    }
}

void RTCPeerConnection::notifyICECandidate(const ICECandidate& candidate) {
    if (delegate_) {
        delegate_->peerConnectionDidGatherICECandidate(*this, candidate);
    }
}

void RTCPeerConnection::notifyDataChannel(std::unique_ptr<RTCDataChannel> channel) {
    if (delegate_) {
        delegate_->peerConnectionDidReceiveDataChannel(*this, std::move(channel));
    }
}

void RTCPeerConnection::notifyNegotiationNeeded() {
    if (delegate_) {
        delegate_->peerConnectionNegotiationNeeded(*this);
    }
}

// Utility functions

const char* peerConnectionStateToString(PeerConnectionState state) {
    switch (state) {
        case PeerConnectionState::NEW:
            return "new";
        case PeerConnectionState::CONNECTING:
            return "connecting";
        case PeerConnectionState::CONNECTED:
            return "connected";
        case PeerConnectionState::DISCONNECTED:
            return "disconnected";
        case PeerConnectionState::FAILED:
            return "failed";
        case PeerConnectionState::CLOSED:
            return "closed";
    }
    return "unknown";
}

const char* iceConnectionStateToString(ICEConnectionState state) {
    switch (state) {
        case ICEConnectionState::NEW:
            return "new";
        case ICEConnectionState::CHECKING:
            return "checking";
        case ICEConnectionState::CONNECTED:
            return "connected";
        case ICEConnectionState::COMPLETED:
            return "completed";
        case ICEConnectionState::DISCONNECTED:
            return "disconnected";
        case ICEConnectionState::FAILED:
            return "failed";
        case ICEConnectionState::CLOSED:
            return "closed";
    }
    return "unknown";
}

const char* iceGatheringStateToString(ICEGatheringState state) {
    switch (state) {
        case ICEGatheringState::NEW:
            return "new";
        case ICEGatheringState::GATHERING:
            return "gathering";
        case ICEGatheringState::COMPLETE:
            return "complete";
    }
    return "unknown";
}

const char* signalingStateToString(SignalingState state) {
    switch (state) {
        case SignalingState::STABLE:
            return "stable";
        case SignalingState::HAVE_LOCAL_OFFER:
            return "have-local-offer";
        case SignalingState::HAVE_REMOTE_OFFER:
            return "have-remote-offer";
        case SignalingState::HAVE_LOCAL_PRANSWER:
            return "have-local-pranswer";
        case SignalingState::HAVE_REMOTE_PRANSWER:
            return "have-remote-pranswer";
        case SignalingState::CLOSED:
            return "closed";
    }
    return "unknown";
}

const char* sdpTypeToString(SDPType type) {
    switch (type) {
        case SDPType::OFFER:
            return "offer";
        case SDPType::PRANSWER:
            return "pranswer";
        case SDPType::ANSWER:
            return "answer";
        case SDPType::ROLLBACK:
            return "rollback";
    }
    return "unknown";
}

SDPType sdpTypeFromString(const std::string& type) {
    if (type == "offer")
        return SDPType::OFFER;
    if (type == "pranswer")
        return SDPType::PRANSWER;
    if (type == "answer")
        return SDPType::ANSWER;
    if (type == "rollback")
        return SDPType::ROLLBACK;
    return SDPType::OFFER;  // Default
}

}  // namespace cells::net
