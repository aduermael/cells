// Web (Emscripten) RTCPeerConnection implementation
// Uses message passing to main thread where RTCPeerConnection is available.
// The WASM runs in a Web Worker, but RTCPeerConnection is main-thread only.

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/val.h>
#include <map>
#include <mutex>
#include <queue>

#include "core/net/include/RTCPeerConnection.h"

namespace cells::net {

// Forward declaration from RTCDataChannel_web.cc
std::unique_ptr<RTCDataChannel> createWebRTCDataChannel(int js_handle, const std::string& label,
                                                        const DataChannelConfig& config);

// Global registry for PeerConnection instances
static std::map<int, class WebRTCPeerConnection*> g_pc_registry;
static std::mutex g_pc_mutex;
static int g_next_pc_id = 1;

// clang-format off

// Initialize the WebRTC module - sets up message handlers
EM_JS(void, cells_rtc_init, (), {
    if (Module._rtcInitialized) return;
    Module._rtcInitialized = true;

    // Listen for messages from main thread
    self.addEventListener('message', function(e) {
        const msg = e.data;
        if (!msg || !msg.type) return;

        // Handle RTC callbacks from main thread
        // Note: Functions starting with _ in C are exported with double underscore
        switch (msg.type) {
            case 'rtc_on_create_sdp': {
                if (msg.success) {
                    Module.__cells_rtc_on_create_sdp(msg.registryId, 1,
                        Module.stringToUTF8OnStack(msg.sdp || ""), 0);
                } else {
                    Module.__cells_rtc_on_create_sdp(msg.registryId, 0, 0,
                        Module.stringToUTF8OnStack(msg.error || 'Unknown error'));
                }
                break;
            }
            case 'rtc_on_set_sdp': {
                if (msg.success) {
                    Module.__cells_rtc_on_set_sdp(msg.registryId, 1, 0);
                } else {
                    Module.__cells_rtc_on_set_sdp(msg.registryId, 0,
                        Module.stringToUTF8OnStack(msg.error || 'Unknown error'));
                }
                break;
            }
            case 'rtc_on_connection_state':
                Module.__cells_rtc_on_connection_state(msg.registryId, msg.state);
                break;
            case 'rtc_on_ice_connection_state':
                Module.__cells_rtc_on_ice_connection_state(msg.registryId, msg.state);
                break;
            case 'rtc_on_ice_gathering_state':
                Module.__cells_rtc_on_ice_gathering_state(msg.registryId, msg.state);
                break;
            case 'rtc_on_signaling_state':
                Module.__cells_rtc_on_signaling_state(msg.registryId, msg.state);
                break;
            case 'rtc_on_ice_candidate':
                if (msg.candidate) {
                    Module.__cells_rtc_on_ice_candidate(msg.registryId,
                        Module.stringToUTF8OnStack(msg.candidate),
                        msg.sdpMid ? Module.stringToUTF8OnStack(msg.sdpMid) : 0,
                        msg.sdpMLineIndex || 0);
                } else {
                    Module.__cells_rtc_on_ice_candidate(msg.registryId, 0, 0, 0);
                }
                break;
            case 'rtc_on_data_channel':
                Module.__cells_rtc_on_data_channel(msg.registryId, msg.dcHandle,
                    Module.stringToUTF8OnStack(msg.label || ""));
                break;
            case 'rtc_on_negotiation_needed':
                Module.__cells_rtc_on_negotiation_needed(msg.registryId);
                break;
            case 'dc_on_open':
                Module.__cells_dc_on_open(msg.dcHandle);
                break;
            case 'dc_on_close':
                Module.__cells_dc_on_close(msg.dcHandle);
                break;
            case 'dc_on_error':
                Module.__cells_dc_on_error(msg.dcHandle,
                    Module.stringToUTF8OnStack(msg.error || 'Unknown error'));
                break;
            case 'dc_on_message':
                if (msg.isText) {
                    // Use heap allocation to avoid stack corruption during C++ processing
                    const str = msg.data || "";
                    const strLen = Module.lengthBytesUTF8(str) + 1;
                    const strPtr = Module._malloc(strLen);
                    Module.stringToUTF8(str, strPtr, strLen);
                    Module.__cells_dc_on_message_text(msg.dcHandle, strPtr);
                    Module._free(strPtr);
                } else {
                    const bytes = new Uint8Array(msg.data);
                    const ptr = Module._malloc(bytes.length);
                    Module.HEAPU8.set(bytes, ptr);
                    Module.__cells_dc_on_message_binary(msg.dcHandle, ptr, bytes.length);
                    Module._free(ptr);
                }
                break;
        }
    });
});

// Create a new RTCPeerConnection - sends request to main thread
// Uses registryId as the identifier (main thread maps this to actual connection)
EM_JS(void, cells_rtc_create, (const char* iceServersJson, int registryId), {
    const iceServers = iceServersJson ? JSON.parse(UTF8ToString(iceServersJson)) : null;
    self.postMessage({
        type: 'rtc_create',
        iceServers,
        registryId
    });
});

// Destroy a PeerConnection
EM_JS(void, cells_rtc_destroy, (int registryId), {
    self.postMessage({ type: 'rtc_destroy', registryId });
});

// Close a PeerConnection
EM_JS(void, cells_rtc_close, (int registryId), {
    self.postMessage({ type: 'rtc_close', registryId });
});

// Create offer - async, response comes via callback
EM_JS(void, cells_rtc_create_offer, (int registryId), {
    self.postMessage({ type: 'rtc_create_offer', registryId });
});

// Create answer - async, response comes via callback
EM_JS(void, cells_rtc_create_answer, (int registryId), {
    self.postMessage({ type: 'rtc_create_answer', registryId });
});

// Set local description
EM_JS(void, cells_rtc_set_local_description, (int registryId, const char* type, const char* sdp), {
    self.postMessage({
        type: 'rtc_set_local_description',
        registryId,
        sdpType: UTF8ToString(type),
        sdp: UTF8ToString(sdp)
    });
});

// Set remote description
EM_JS(void, cells_rtc_set_remote_description, (int registryId, const char* type, const char* sdp), {
    self.postMessage({
        type: 'rtc_set_remote_description',
        registryId,
        sdpType: UTF8ToString(type),
        sdp: UTF8ToString(sdp)
    });
});

// Add ICE candidate
EM_JS(void, cells_rtc_add_ice_candidate, (int registryId, const char* candidate, const char* sdpMid, int sdpMLineIndex), {
    self.postMessage({
        type: 'rtc_add_ice_candidate',
        registryId,
        candidate: UTF8ToString(candidate),
        sdpMid: sdpMid ? UTF8ToString(sdpMid) : null,
        sdpMLineIndex
    });
});

// Create data channel - request main thread to create, returns dcHandle via callback
EM_JS(void, cells_rtc_create_data_channel, (int registryId, const char* label, int ordered, int maxRetransmits, int maxPacketLifeTime), {
    self.postMessage({
        type: 'rtc_create_data_channel',
        registryId,
        label: UTF8ToString(label),
        ordered: !!ordered,
        maxRetransmits,
        maxPacketLifeTime
    });
});

// clang-format on

class WebRTCPeerConnection : public RTCPeerConnection {
public:
    explicit WebRTCPeerConnection(const RTCConfiguration& config) {
        cells_rtc_init();

        // Register this instance first so callbacks can find us
        {
            std::lock_guard<std::mutex> lock(g_pc_mutex);
            registry_id_ = g_next_pc_id++;
            g_pc_registry[registry_id_] = this;
        }

        // Build ICE servers JSON
        std::string ice_json = buildIceServersJson(config);

        // Send create request - no return value, just fire and forget
        // Main thread will create the connection and send events via callbacks
        cells_rtc_create(ice_json.empty() ? nullptr : ice_json.c_str(), registry_id_);
    }

    ~WebRTCPeerConnection() override {
        // Send destroy request
        cells_rtc_destroy(registry_id_);

        // Unregister
        std::lock_guard<std::mutex> lock(g_pc_mutex);
        g_pc_registry.erase(registry_id_);
    }

    void createOffer(CreateSDPCallback callback) override {
        pending_create_callback_ = std::move(callback);
        pending_sdp_type_ = SDPType::OFFER;
        cells_rtc_create_offer(registry_id_);
    }

    void createAnswer(CreateSDPCallback callback) override {
        pending_create_callback_ = std::move(callback);
        pending_sdp_type_ = SDPType::ANSWER;
        cells_rtc_create_answer(registry_id_);
    }

    void setLocalDescription(const SessionDescription& sdp, SetSDPCallback callback) override {
        pending_set_callbacks_.push(std::move(callback));
        local_description_ = sdp;
        cells_rtc_set_local_description(registry_id_, sdpTypeToString(sdp.type), sdp.sdp.c_str());
    }

    void setRemoteDescription(const SessionDescription& sdp, SetSDPCallback callback) override {
        pending_set_callbacks_.push(std::move(callback));
        remote_description_ = sdp;
        cells_rtc_set_remote_description(registry_id_, sdpTypeToString(sdp.type), sdp.sdp.c_str());
    }

    void addIceCandidate(const ICECandidate& candidate, SetSDPCallback callback) override {
        pending_set_callbacks_.push(std::move(callback));
        cells_rtc_add_ice_candidate(registry_id_, candidate.candidate.c_str(),
                                    candidate.sdp_mid.empty() ? nullptr : candidate.sdp_mid.c_str(),
                                    candidate.sdp_mline_index);
    }

    std::unique_ptr<RTCDataChannel> createDataChannel(const std::string& label,
                                                      const DataChannelConfig& config) override {
        // Store pending data channel info for when we receive the callback
        pending_dc_label_ = label;
        pending_dc_config_ = config;
        pending_dc_callback_ = true;

        // Send request - actual channel created async, callback will provide dcHandle
        cells_rtc_create_data_channel(registry_id_, label.c_str(), config.ordered ? 1 : 0,
                                      config.max_retransmits, config.max_packet_life_time);

        // Note: This is a design limitation - we can't return the channel synchronously
        // The caller needs to use the onDataChannel callback instead, or we need
        // to restructure this to be async. For now, return nullptr and handle via callback.
        return nullptr;
    }

    void close() override { cells_rtc_close(registry_id_); }

    // These getters return cached state - main thread sends state updates via callbacks
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
        return local_description_.sdp.empty() ? nullptr : &local_description_;
    }

    [[nodiscard]] const SessionDescription* getRemoteDescription() const override {
        return remote_description_.sdp.empty() ? nullptr : &remote_description_;
    }

    // Callbacks from JS
    void jsOnCreateSDP(bool success, const char* sdp, const char* error) {
        if (pending_create_callback_) {
            auto callback = std::move(pending_create_callback_);
            if (success) {
                callback(true, SessionDescription(pending_sdp_type_, sdp ? sdp : ""), "");
            } else {
                callback(false, SessionDescription(), error ? error : "Unknown error");
            }
        }
    }

    void jsOnSetSDP(bool success, const char* error) {
        if (!pending_set_callbacks_.empty()) {
            auto callback = std::move(pending_set_callbacks_.front());
            pending_set_callbacks_.pop();
            if (callback) {
                callback(success, error ? error : "");
            }
        }
    }

    void jsOnConnectionState(int state) {
        connection_state_ = static_cast<PeerConnectionState>(state);
        notifyConnectionStateChange(connection_state_);
    }

    void jsOnICEConnectionState(int state) {
        ice_connection_state_ = static_cast<ICEConnectionState>(state);
        notifyICEConnectionStateChange(ice_connection_state_);
    }

    void jsOnICEGatheringState(int state) {
        ice_gathering_state_ = static_cast<ICEGatheringState>(state);
        notifyICEGatheringStateChange(ice_gathering_state_);
    }

    void jsOnSignalingState(int state) {
        signaling_state_ = static_cast<SignalingState>(state);
        notifySignalingStateChange(signaling_state_);
    }

    void jsOnICECandidate(const char* candidate, const char* sdp_mid, int sdp_mline_index) {
        if (candidate != nullptr) {
            ICECandidate cand(candidate, sdp_mid ? sdp_mid : "", sdp_mline_index);
            notifyICECandidate(cand);
        } else {
            // End of candidates
            ICECandidate empty;
            notifyICECandidate(empty);
        }
    }

    void jsOnDataChannel(int dc_handle, const char* label) {
        DataChannelConfig config;
        if (pending_dc_callback_ && pending_dc_label_ == (label ? label : "")) {
            // This is the channel we just created
            config = pending_dc_config_;
            pending_dc_callback_ = false;
        }
        auto channel = createWebRTCDataChannel(dc_handle, label ? label : "", config);
        notifyDataChannel(std::move(channel));
    }

    void jsOnNegotiationNeeded() { notifyNegotiationNeeded(); }

private:
    int registry_id_ = 0;

    SessionDescription local_description_;
    SessionDescription remote_description_;

    // Cached state (updated via callbacks from main thread)
    PeerConnectionState connection_state_ = PeerConnectionState::NEW;
    ICEConnectionState ice_connection_state_ = ICEConnectionState::NEW;
    ICEGatheringState ice_gathering_state_ = ICEGatheringState::NEW;
    SignalingState signaling_state_ = SignalingState::STABLE;

    CreateSDPCallback pending_create_callback_;
    std::queue<SetSDPCallback> pending_set_callbacks_;
    SDPType pending_sdp_type_ = SDPType::OFFER;

    // Pending data channel creation
    bool pending_dc_callback_ = false;
    std::string pending_dc_label_;
    DataChannelConfig pending_dc_config_;

    static std::string buildIceServersJson(const RTCConfiguration& config) {
        if (config.ice_servers.empty()) {
            return "";
        }

        std::string json = "[";
        bool first = true;
        for (const auto& server : config.ice_servers) {
            if (!first)
                json += ",";
            first = false;

            json += "{\"urls\":";
            if (server.urls.size() == 1) {
                json += "\"" + server.urls[0] + "\"";
            } else {
                json += "[";
                bool first_url = true;
                for (const auto& url : server.urls) {
                    if (!first_url)
                        json += ",";
                    first_url = false;
                    json += "\"" + url + "\"";
                }
                json += "]";
            }

            if (!server.username.empty()) {
                json += ",\"username\":\"" + server.username + "\"";
            }
            if (!server.credential.empty()) {
                json += ",\"credential\":\"" + server.credential + "\"";
            }
            json += "}";
        }
        json += "]";
        return json;
    }
};

// Exported C functions for JS callbacks
extern "C" {

EMSCRIPTEN_KEEPALIVE
void _cells_rtc_on_create_sdp(int registry_id, int success, const char* sdp, const char* error) {
    std::lock_guard<std::mutex> lock(g_pc_mutex);
    auto it = g_pc_registry.find(registry_id);
    if (it != g_pc_registry.end()) {
        it->second->jsOnCreateSDP(success != 0, sdp, error);
    }
}

EMSCRIPTEN_KEEPALIVE
void _cells_rtc_on_set_sdp(int registry_id, int success, const char* error) {
    std::lock_guard<std::mutex> lock(g_pc_mutex);
    auto it = g_pc_registry.find(registry_id);
    if (it != g_pc_registry.end()) {
        it->second->jsOnSetSDP(success != 0, error);
    }
}

EMSCRIPTEN_KEEPALIVE
void _cells_rtc_on_connection_state(int registry_id, int state) {
    std::lock_guard<std::mutex> lock(g_pc_mutex);
    auto it = g_pc_registry.find(registry_id);
    if (it != g_pc_registry.end()) {
        it->second->jsOnConnectionState(state);
    }
}

EMSCRIPTEN_KEEPALIVE
void _cells_rtc_on_ice_connection_state(int registry_id, int state) {
    std::lock_guard<std::mutex> lock(g_pc_mutex);
    auto it = g_pc_registry.find(registry_id);
    if (it != g_pc_registry.end()) {
        it->second->jsOnICEConnectionState(state);
    }
}

EMSCRIPTEN_KEEPALIVE
void _cells_rtc_on_ice_gathering_state(int registry_id, int state) {
    std::lock_guard<std::mutex> lock(g_pc_mutex);
    auto it = g_pc_registry.find(registry_id);
    if (it != g_pc_registry.end()) {
        it->second->jsOnICEGatheringState(state);
    }
}

EMSCRIPTEN_KEEPALIVE
void _cells_rtc_on_signaling_state(int registry_id, int state) {
    std::lock_guard<std::mutex> lock(g_pc_mutex);
    auto it = g_pc_registry.find(registry_id);
    if (it != g_pc_registry.end()) {
        it->second->jsOnSignalingState(state);
    }
}

EMSCRIPTEN_KEEPALIVE
void _cells_rtc_on_ice_candidate(int registry_id, const char* candidate, const char* sdp_mid,
                                 int sdp_mline_index) {
    std::lock_guard<std::mutex> lock(g_pc_mutex);
    auto it = g_pc_registry.find(registry_id);
    if (it != g_pc_registry.end()) {
        it->second->jsOnICECandidate(candidate, sdp_mid, sdp_mline_index);
    }
}

EMSCRIPTEN_KEEPALIVE
void _cells_rtc_on_data_channel(int registry_id, int dc_handle, const char* label) {
    std::lock_guard<std::mutex> lock(g_pc_mutex);
    auto it = g_pc_registry.find(registry_id);
    if (it != g_pc_registry.end()) {
        it->second->jsOnDataChannel(dc_handle, label);
    }
}

EMSCRIPTEN_KEEPALIVE
void _cells_rtc_on_negotiation_needed(int registry_id) {
    std::lock_guard<std::mutex> lock(g_pc_mutex);
    auto it = g_pc_registry.find(registry_id);
    if (it != g_pc_registry.end()) {
        it->second->jsOnNegotiationNeeded();
    }
}

}  // extern "C"

// Factory implementation for web
std::unique_ptr<RTCPeerConnection> RTCPeerConnection::make(const RTCConfiguration& config) {
    return std::make_unique<WebRTCPeerConnection>(config);
}

}  // namespace cells::net

#endif  // __EMSCRIPTEN__
