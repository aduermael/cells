// RTCPeerConnection implementation using libdatachannel
// For native platforms (macOS, Linux, Windows, etc.) - not emscripten

#if !defined(__EMSCRIPTEN__)

#include <cctype>

#include <memory>
#include <mutex>
#include <optional>
#include <rtc/rtc.hpp>
#include <string>
#include <vector>

#include "core/log/include/Logger.h"
#include "core/net/include/RTCPeerConnection.h"

namespace cells::net {

// Forward declaration from RTCDataChannel_libdc.cc
std::unique_ptr<RTCDataChannel> createLibdcDataChannel(std::shared_ptr<rtc::DataChannel> dc);

// Map libdatachannel states to our state enums
static PeerConnectionState mapState(rtc::PeerConnection::State state) {
    switch (state) {
        case rtc::PeerConnection::State::New:
            return PeerConnectionState::NEW;
        case rtc::PeerConnection::State::Connecting:
            return PeerConnectionState::CONNECTING;
        case rtc::PeerConnection::State::Connected:
            return PeerConnectionState::CONNECTED;
        case rtc::PeerConnection::State::Disconnected:
            return PeerConnectionState::DISCONNECTED;
        case rtc::PeerConnection::State::Failed:
            return PeerConnectionState::FAILED;
        case rtc::PeerConnection::State::Closed:
            return PeerConnectionState::CLOSED;
        default:
            return PeerConnectionState::NEW;
    }
}

static ICEConnectionState mapIceState(rtc::PeerConnection::IceState state) {
    switch (state) {
        case rtc::PeerConnection::IceState::New:
            return ICEConnectionState::NEW;
        case rtc::PeerConnection::IceState::Checking:
            return ICEConnectionState::CHECKING;
        case rtc::PeerConnection::IceState::Connected:
            return ICEConnectionState::CONNECTED;
        case rtc::PeerConnection::IceState::Completed:
            return ICEConnectionState::COMPLETED;
        case rtc::PeerConnection::IceState::Disconnected:
            return ICEConnectionState::DISCONNECTED;
        case rtc::PeerConnection::IceState::Failed:
            return ICEConnectionState::FAILED;
        case rtc::PeerConnection::IceState::Closed:
            return ICEConnectionState::CLOSED;
        default:
            return ICEConnectionState::NEW;
    }
}

static ICEGatheringState mapGatheringState(rtc::PeerConnection::GatheringState state) {
    switch (state) {
        case rtc::PeerConnection::GatheringState::New:
            return ICEGatheringState::NEW;
        case rtc::PeerConnection::GatheringState::InProgress:
            return ICEGatheringState::GATHERING;
        case rtc::PeerConnection::GatheringState::Complete:
            return ICEGatheringState::COMPLETE;
        default:
            return ICEGatheringState::NEW;
    }
}

static SignalingState mapSignalingState(rtc::PeerConnection::SignalingState state) {
    switch (state) {
        case rtc::PeerConnection::SignalingState::Stable:
            return SignalingState::STABLE;
        case rtc::PeerConnection::SignalingState::HaveLocalOffer:
            return SignalingState::HAVE_LOCAL_OFFER;
        case rtc::PeerConnection::SignalingState::HaveRemoteOffer:
            return SignalingState::HAVE_REMOTE_OFFER;
        case rtc::PeerConnection::SignalingState::HaveLocalPranswer:
            return SignalingState::HAVE_LOCAL_PRANSWER;
        case rtc::PeerConnection::SignalingState::HaveRemotePranswer:
            return SignalingState::HAVE_REMOTE_PRANSWER;
        default:
            return SignalingState::STABLE;
    }
}

// Browser RTCIceCandidate.candidate is the attribute value ("candidate:...") without
// the SDP "a=" line prefix. libdatachannel's operator string() adds "a="; use
// Candidate::candidate() when exporting, and strip "a=" when importing.
// Also lowercase the transport token (libdc emits "UDP"; browsers emit "udp").
static std::string iceCandidateForSignaling(const rtc::Candidate& cand) {
    std::string s = cand.candidate();
    // candidate:<foundation> <component> <protocol> ...
    const size_t sp1 = s.find(' ');
    if (sp1 == std::string::npos) {
        return s;
    }
    const size_t sp2 = s.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) {
        return s;
    }
    const size_t proto_begin = sp1 + 1;
    // protocol runs until next space after component — actually:
    // fields: [0]=candidate:f [1]=component [2]=protocol
    // sp1 after field0, sp2 after field1, protocol is sp2+1 .. next space
    const size_t proto_start = sp2 + 1;
    const size_t proto_end = s.find(' ', proto_start);
    if (proto_end == std::string::npos) {
        return s;
    }
    for (size_t i = proto_start; i < proto_end; ++i) {
        const unsigned char ch = static_cast<unsigned char>(s[i]);
        s[i] = static_cast<char>(std::tolower(ch));
    }
    return s;
}

static std::string normalizeIncomingIceCandidate(std::string candidate) {
    if (candidate.rfind("a=", 0) == 0) {
        candidate.erase(0, 2);
    }
    // Lowercase transport protocol for consistent parsing.
    const size_t sp1 = candidate.find(' ');
    if (sp1 == std::string::npos) {
        return candidate;
    }
    const size_t sp2 = candidate.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) {
        return candidate;
    }
    const size_t proto_start = sp2 + 1;
    const size_t proto_end = candidate.find(' ', proto_start);
    if (proto_end == std::string::npos) {
        return candidate;
    }
    for (size_t i = proto_start; i < proto_end; ++i) {
        const unsigned char ch = static_cast<unsigned char>(candidate[i]);
        candidate[i] = static_cast<char>(std::tolower(ch));
    }
    return candidate;
}

// Convert our ICE server config to libdatachannel format
static rtc::Configuration toLibdcConfig(const RTCConfiguration& config) {
    rtc::Configuration rtcConfig;

    for (const auto& server : config.ice_servers) {
        for (const auto& url : server.urls) {
            if (!server.username.empty() && !server.credential.empty()) {
                // libdatachannel URL form: turn:user:pass@host:port
                // (NOT url:user:pass — that fails to parse).
                std::string turn_url = url;
                const auto scheme_end = turn_url.find(':');
                if (scheme_end != std::string::npos && turn_url.find('@') == std::string::npos) {
                    turn_url.insert(scheme_end + 1,
                                    server.username + ":" + server.credential + "@");
                }
                rtcConfig.iceServers.emplace_back(turn_url);
            } else {
                // STUN server (no credentials needed)
                rtcConfig.iceServers.emplace_back(url);
            }
        }
    }

    if (config.ice_transport_policy == ICETransportPolicy::RELAY) {
        rtcConfig.iceTransportPolicy = rtc::TransportPolicy::Relay;
    }

    return rtcConfig;
}

class LibdcPeerConnection : public RTCPeerConnection {
public:
    explicit LibdcPeerConnection(const RTCConfiguration& config) {
        try {
            auto rtcConfig = toLibdcConfig(config);
            pc_ = std::make_unique<rtc::PeerConnection>(rtcConfig);
            setupCallbacks();
        } catch (const std::exception& e) {
            // Failed to create peer connection
            connection_state_ = PeerConnectionState::FAILED;
        }
    }

    ~LibdcPeerConnection() override {
        if (pc_) {
            pc_->resetCallbacks();
        }
    }

    void createOffer(CreateSDPCallback callback) override {
        if (!pc_) {
            callback(false, SessionDescription(), "PeerConnection not initialized");
            return;
        }

        // libdatachannel auto-generates an offer when createDataChannel() is called
        // The offer may already be available in local_description_ via onLocalDescription callback
        // Check if we already have an offer from the auto-generation
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (local_description_.type == SDPType::OFFER && !local_description_.sdp.empty()) {
                callback(true, local_description_, "");
                return;
            }
        }

        // Store callback before calling libdatachannel (callback may fire synchronously)
        // Don't hold lock while calling into libdatachannel to avoid deadlock
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_create_callback_ = std::move(callback);
            pending_sdp_type_ = SDPType::OFFER;
        }

        // In libdatachannel, setting local description with type generates the offer/answer
        try {
            pc_->setLocalDescription(rtc::Description::Type::Offer);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto cb = std::move(pending_create_callback_);
            pending_create_callback_ = nullptr;
            if (cb) {
                cb(false, SessionDescription(), e.what());
            }
        }
    }

    void createAnswer(CreateSDPCallback callback) override {
        if (!pc_) {
            callback(false, SessionDescription(), "PeerConnection not initialized");
            return;
        }

        // libdatachannel auto-generates the answer when setRemoteDescription(offer) is called
        // The answer is already available in local_description_ via onLocalDescription callback
        // Check if we already have an answer from the auto-generation
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (local_description_.type == SDPType::ANSWER && !local_description_.sdp.empty()) {
                callback(true, local_description_, "");
                return;
            }
        }

        // Store callback before calling libdatachannel (callback may fire synchronously)
        // Don't hold lock while calling into libdatachannel to avoid deadlock
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_create_callback_ = std::move(callback);
            pending_sdp_type_ = SDPType::ANSWER;
        }

        try {
            pc_->setLocalDescription(rtc::Description::Type::Answer);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto cb = std::move(pending_create_callback_);
            pending_create_callback_ = nullptr;
            if (cb) {
                cb(false, SessionDescription(), e.what());
            }
        }
    }

    void setLocalDescription(const SessionDescription& sdp, SetSDPCallback callback) override {
        if (!pc_) {
            callback(false, "PeerConnection not initialized");
            return;
        }

        // In libdatachannel, local description is set when creating offer/answer
        // This method is called after createOffer/createAnswer completes
        // The description is already set, so we just confirm success
        local_description_ = sdp;
        callback(true, "");
    }

    void setRemoteDescription(const SessionDescription& sdp, SetSDPCallback callback) override {
        if (!pc_) {
            callback(false, "PeerConnection not initialized");
            return;
        }

        try {
            rtc::Description::Type type;
            switch (sdp.type) {
                case SDPType::OFFER:
                    type = rtc::Description::Type::Offer;
                    break;
                case SDPType::ANSWER:
                    type = rtc::Description::Type::Answer;
                    break;
                case SDPType::PRANSWER:
                    type = rtc::Description::Type::Pranswer;
                    break;
                case SDPType::ROLLBACK:
                    type = rtc::Description::Type::Rollback;
                    break;
                default:
                    type = rtc::Description::Type::Unspec;
            }

            rtc::Description desc(sdp.sdp, type);
            pc_->setRemoteDescription(desc);

            remote_description_ = sdp;
            remote_description_set_ = true;

            // libdatachannel does not buffer candidates before remote description;
            // browsers do. Flush any that arrived early (trickle ICE).
            flushPendingRemoteCandidates();

            callback(true, "");
        } catch (const std::exception& e) {
            callback(false, e.what());
        }
    }

    void addIceCandidate(const ICECandidate& candidate, SetSDPCallback callback) override {
        if (!pc_) {
            callback(false, "PeerConnection not initialized");
            return;
        }

        if (candidate.isEmpty()) {
            // End of candidates - no action needed
            callback(true, "");
            return;
        }

        // Buffer until setRemoteDescription succeeds (browser-compatible trickle).
        if (!remote_description_set_) {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_remote_candidates_.push_back(candidate);
            callback(true, "");
            return;
        }

        callback(applyRemoteCandidate(candidate), "");
    }

    std::unique_ptr<RTCDataChannel> createDataChannel(const std::string& label,
                                                      const DataChannelConfig& config) override {
        if (!pc_) {
            return nullptr;
        }

        try {
            rtc::DataChannelInit init;
            init.negotiated = config.negotiated;

            rtc::Reliability reliability;
            reliability.unordered = !config.ordered;
            if (config.max_retransmits >= 0) {
                reliability.maxRetransmits = static_cast<unsigned int>(config.max_retransmits);
            }
            if (config.max_packet_life_time >= 0) {
                reliability.maxPacketLifeTime =
                    std::chrono::milliseconds(config.max_packet_life_time);
            }
            init.reliability = reliability;
            init.protocol = config.protocol;

            if (config.id >= 0) {
                init.id = static_cast<uint16_t>(config.id);
            }

            auto dc = pc_->createDataChannel(label, init);
            return createLibdcDataChannel(dc);
        } catch (const std::exception&) {
            return nullptr;
        }
    }

    void close() override {
        if (pc_) {
            pc_->close();
        }
    }

    [[nodiscard]] PeerConnectionState getConnectionState() const override {
        return connection_state_;
    }

    [[nodiscard]] ICEConnectionState getICEConnectionState() const override {
        return ice_connection_state_;
    }

    [[nodiscard]] ICEGatheringState getICEGatheringState() const override {
        return ice_gathering_state_;
    }

    [[nodiscard]] SignalingState getSignalingState() const override { return signaling_state_; }

    [[nodiscard]] const SessionDescription* getLocalDescription() const override {
        if (local_description_.sdp.empty()) {
            return nullptr;
        }
        return &local_description_;
    }

    [[nodiscard]] const SessionDescription* getRemoteDescription() const override {
        if (remote_description_.sdp.empty()) {
            return nullptr;
        }
        return &remote_description_;
    }

private:
    void setupCallbacks() {
        if (!pc_) {
            return;
        }

        pc_->onStateChange([this](rtc::PeerConnection::State state) {
            connection_state_ = mapState(state);
            notifyConnectionStateChange(connection_state_);
        });

        pc_->onIceStateChange([this](rtc::PeerConnection::IceState state) {
            ice_connection_state_ = mapIceState(state);
            notifyICEConnectionStateChange(ice_connection_state_);
        });

        pc_->onGatheringStateChange([this](rtc::PeerConnection::GatheringState state) {
            ice_gathering_state_ = mapGatheringState(state);
            notifyICEGatheringStateChange(ice_gathering_state_);
        });

        pc_->onSignalingStateChange([this](rtc::PeerConnection::SignalingState state) {
            signaling_state_ = mapSignalingState(state);
            notifySignalingStateChange(signaling_state_);
        });

        pc_->onLocalDescription([this](rtc::Description desc) {
            std::lock_guard<std::mutex> lock(mutex_);

            SDPType type;
            switch (desc.type()) {
                case rtc::Description::Type::Offer:
                    type = SDPType::OFFER;
                    break;
                case rtc::Description::Type::Answer:
                    type = SDPType::ANSWER;
                    break;
                case rtc::Description::Type::Pranswer:
                    type = SDPType::PRANSWER;
                    break;
                case rtc::Description::Type::Rollback:
                    type = SDPType::ROLLBACK;
                    break;
                default:
                    type = SDPType::OFFER;
            }

            local_description_ = SessionDescription(type, std::string(desc));

            if (pending_create_callback_) {
                auto callback = std::move(pending_create_callback_);
                pending_create_callback_ = nullptr;
                callback(true, local_description_, "");
            }
        });

        pc_->onLocalCandidate([this](rtc::Candidate cand) {
            ICECandidate candidate;
            // Must NOT include "a=" — browsers reject that form in addIceCandidate.
            candidate.candidate = iceCandidateForSignaling(cand);
            auto mid = cand.mid();
            if (!mid.empty()) {
                candidate.sdp_mid = mid;
            }
            candidate.sdp_mline_index = 0;  // libdatachannel doesn't expose this directly
            notifyICECandidate(candidate);
        });

        pc_->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) {
            auto channel = createLibdcDataChannel(dc);
            notifyDataChannel(std::move(channel));
        });
    }

    bool applyRemoteCandidate(const ICECandidate& candidate) {
        if (!pc_) {
            return false;
        }
        try {
            const std::string normalized = normalizeIncomingIceCandidate(candidate.candidate);
            rtc::Candidate cand(normalized, candidate.sdp_mid);
            // Browser host candidates are often *.local (mDNS). Resolve before
            // add so libjuice can form host pairs on the same LAN as the CLI.
            if (!cand.isResolved()) {
                if (!cand.resolve(rtc::Candidate::ResolveMode::Lookup)) {
                    // Still hand off unresolved — libdc may retry async.
                    LOG_INFO("[RTC] ICE resolve pending/failed: %s", normalized.c_str());
                } else {
                    LOG_INFO("[RTC] ICE resolved remote candidate mid=%s",
                             candidate.sdp_mid.c_str());
                }
            }
            pc_->addRemoteCandidate(std::move(cand));
            return true;
        } catch (const std::exception& e) {
            LOG_INFO("[RTC] addRemoteCandidate failed: %s (%s)", e.what(),
                     candidate.candidate.c_str());
            return false;
        }
    }

    void flushPendingRemoteCandidates() {
        std::vector<ICECandidate> pending;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending.swap(pending_remote_candidates_);
        }
        for (const auto& c : pending) {
            applyRemoteCandidate(c);
        }
    }

    std::unique_ptr<rtc::PeerConnection> pc_;
    std::mutex mutex_;

    // Cached state
    PeerConnectionState connection_state_ = PeerConnectionState::NEW;
    ICEConnectionState ice_connection_state_ = ICEConnectionState::NEW;
    ICEGatheringState ice_gathering_state_ = ICEGatheringState::NEW;
    SignalingState signaling_state_ = SignalingState::STABLE;

    SessionDescription local_description_;
    SessionDescription remote_description_;
    bool remote_description_set_ = false;
    std::vector<ICECandidate> pending_remote_candidates_;

    // Pending callbacks
    CreateSDPCallback pending_create_callback_;
    SDPType pending_sdp_type_ = SDPType::OFFER;
};

// Factory implementation for native platforms using libdatachannel
std::unique_ptr<RTCPeerConnection> RTCPeerConnection::make(const RTCConfiguration& config) {
    return std::make_unique<LibdcPeerConnection>(config);
}

}  // namespace cells::net

#endif  // !__EMSCRIPTEN__
