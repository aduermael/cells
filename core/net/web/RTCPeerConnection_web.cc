// Web (Emscripten) RTCPeerConnection implementation
// Uses browser's RTCPeerConnection API via EM_JS

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

// Initialize the WebRTC module
EM_JS(void, cells_rtc_init, (), {
    if (!Module._rtcPeerConnections) {
        Module._rtcPeerConnections = new Map();
        Module._rtcDataChannels = new Map();
        Module._nextPcHandle = 1;
        Module._nextDcHandle = 1;
    }
});

// Create a new RTCPeerConnection
EM_JS(int, cells_rtc_create, (const char* iceServersJson), {
    const config = {};
    if (iceServersJson) {
        const servers = JSON.parse(UTF8ToString(iceServersJson));
        config.iceServers = servers;
    }

    const pc = new RTCPeerConnection(config);
    const handle = Module._nextPcHandle++;
    Module._rtcPeerConnections.set(handle, pc);
    return handle;
});

// Destroy a PeerConnection
EM_JS(void, cells_rtc_destroy, (int handle), {
    const pc = Module._rtcPeerConnections.get(handle);
    if (pc) {
        pc.close();
        Module._rtcPeerConnections.delete(handle);
    }
});

// Close a PeerConnection
EM_JS(void, cells_rtc_close, (int handle), {
    const pc = Module._rtcPeerConnections.get(handle);
    if (pc) {
        pc.close();
    }
});

// Create offer
EM_JS(void, cells_rtc_create_offer, (int handle, int registryId), {
    const pc = Module._rtcPeerConnections.get(handle);
    if (!pc) {
        Module._cells_rtc_on_create_sdp(registryId, 0, 0, Module.stringToUTF8OnStack('PeerConnection not found'));
        return;
    }

    pc.createOffer().then(function(offer) {
        const sdpPtr = Module.stringToUTF8OnStack(offer.sdp);
        Module._cells_rtc_on_create_sdp(registryId, 1, sdpPtr, 0);
    }).catch(function(error) {
        const errorPtr = Module.stringToUTF8OnStack(error.message || 'createOffer failed');
        Module._cells_rtc_on_create_sdp(registryId, 0, 0, errorPtr);
    });
});

// Create answer
EM_JS(void, cells_rtc_create_answer, (int handle, int registryId), {
    const pc = Module._rtcPeerConnections.get(handle);
    if (!pc) {
        Module._cells_rtc_on_create_sdp(registryId, 0, 0, Module.stringToUTF8OnStack('PeerConnection not found'));
        return;
    }

    pc.createAnswer().then(function(answer) {
        const sdpPtr = Module.stringToUTF8OnStack(answer.sdp);
        Module._cells_rtc_on_create_sdp(registryId, 1, sdpPtr, 0);
    }).catch(function(error) {
        const errorPtr = Module.stringToUTF8OnStack(error.message || 'createAnswer failed');
        Module._cells_rtc_on_create_sdp(registryId, 0, 0, errorPtr);
    });
});

// Set local description
EM_JS(void, cells_rtc_set_local_description, (int handle, int registryId, const char* type, const char* sdp), {
    const pc = Module._rtcPeerConnections.get(handle);
    if (!pc) {
        Module._cells_rtc_on_set_sdp(registryId, 0, Module.stringToUTF8OnStack('PeerConnection not found'));
        return;
    }

    const desc = {
        type: UTF8ToString(type),
        sdp: UTF8ToString(sdp)
    };

    pc.setLocalDescription(desc).then(function() {
        Module._cells_rtc_on_set_sdp(registryId, 1, 0);
    }).catch(function(error) {
        const errorPtr = Module.stringToUTF8OnStack(error.message || 'setLocalDescription failed');
        Module._cells_rtc_on_set_sdp(registryId, 0, errorPtr);
    });
});

// Set remote description
EM_JS(void, cells_rtc_set_remote_description, (int handle, int registryId, const char* type, const char* sdp), {
    const pc = Module._rtcPeerConnections.get(handle);
    if (!pc) {
        Module._cells_rtc_on_set_sdp(registryId, 0, Module.stringToUTF8OnStack('PeerConnection not found'));
        return;
    }

    const desc = {
        type: UTF8ToString(type),
        sdp: UTF8ToString(sdp)
    };

    pc.setRemoteDescription(desc).then(function() {
        Module._cells_rtc_on_set_sdp(registryId, 1, 0);
    }).catch(function(error) {
        const errorPtr = Module.stringToUTF8OnStack(error.message || 'setRemoteDescription failed');
        Module._cells_rtc_on_set_sdp(registryId, 0, errorPtr);
    });
});

// Add ICE candidate
EM_JS(void, cells_rtc_add_ice_candidate, (int handle, int registryId, const char* candidate, const char* sdpMid, int sdpMLineIndex), {
    const pc = Module._rtcPeerConnections.get(handle);
    if (!pc) {
        Module._cells_rtc_on_set_sdp(registryId, 0, Module.stringToUTF8OnStack('PeerConnection not found'));
        return;
    }

    const candidateObj = {
        candidate: UTF8ToString(candidate),
        sdpMid: sdpMid ? UTF8ToString(sdpMid) : null,
        sdpMLineIndex: sdpMLineIndex
    };

    pc.addIceCandidate(candidateObj).then(function() {
        Module._cells_rtc_on_set_sdp(registryId, 1, 0);
    }).catch(function(error) {
        const errorPtr = Module.stringToUTF8OnStack(error.message || 'addIceCandidate failed');
        Module._cells_rtc_on_set_sdp(registryId, 0, errorPtr);
    });
});

// Create data channel
EM_JS(int, cells_rtc_create_data_channel, (int handle, const char* label, int ordered, int maxRetransmits, int maxPacketLifeTime), {
    const pc = Module._rtcPeerConnections.get(handle);
    if (!pc) return -1;

    const options = {};
    options.ordered = !!ordered;
    if (maxRetransmits >= 0) options.maxRetransmits = maxRetransmits;
    if (maxPacketLifeTime >= 0) options.maxPacketLifeTime = maxPacketLifeTime;

    const channel = pc.createDataChannel(UTF8ToString(label), options);
    const dcHandle = Module._nextDcHandle++;
    Module._rtcDataChannels.set(dcHandle, channel);
    return dcHandle;
});

// Setup callbacks for peer connection
EM_JS(void, cells_rtc_setup_callbacks, (int handle, int registryId), {
    const pc = Module._rtcPeerConnections.get(handle);
    if (!pc) return;

    pc.onconnectionstatechange = function() {
        let state = 0; // NEW
        switch (pc.connectionState) {
            case 'new': state = 0; break;
            case 'connecting': state = 1; break;
            case 'connected': state = 2; break;
            case 'disconnected': state = 3; break;
            case 'failed': state = 4; break;
            case 'closed': state = 5; break;
        }
        Module._cells_rtc_on_connection_state(registryId, state);
    };

    pc.oniceconnectionstatechange = function() {
        let state = 0; // NEW
        switch (pc.iceConnectionState) {
            case 'new': state = 0; break;
            case 'checking': state = 1; break;
            case 'connected': state = 2; break;
            case 'completed': state = 3; break;
            case 'disconnected': state = 4; break;
            case 'failed': state = 5; break;
            case 'closed': state = 6; break;
        }
        Module._cells_rtc_on_ice_connection_state(registryId, state);
    };

    pc.onicegatheringstatechange = function() {
        let state = 0; // NEW
        switch (pc.iceGatheringState) {
            case 'new': state = 0; break;
            case 'gathering': state = 1; break;
            case 'complete': state = 2; break;
        }
        Module._cells_rtc_on_ice_gathering_state(registryId, state);
    };

    pc.onsignalingstatechange = function() {
        let state = 0; // STABLE
        switch (pc.signalingState) {
            case 'stable': state = 0; break;
            case 'have-local-offer': state = 1; break;
            case 'have-remote-offer': state = 2; break;
            case 'have-local-pranswer': state = 3; break;
            case 'have-remote-pranswer': state = 4; break;
            case 'closed': state = 5; break;
        }
        Module._cells_rtc_on_signaling_state(registryId, state);
    };

    pc.onicecandidate = function(event) {
        if (event.candidate) {
            const candPtr = Module.stringToUTF8OnStack(event.candidate.candidate);
            const midPtr = event.candidate.sdpMid ? Module.stringToUTF8OnStack(event.candidate.sdpMid) : 0;
            Module._cells_rtc_on_ice_candidate(registryId, candPtr, midPtr, event.candidate.sdpMLineIndex || 0);
        } else {
            // End of candidates
            Module._cells_rtc_on_ice_candidate(registryId, 0, 0, 0);
        }
    };

    pc.ondatachannel = function(event) {
        const channel = event.channel;
        const dcHandle = Module._nextDcHandle++;
        Module._rtcDataChannels.set(dcHandle, channel);
        const labelPtr = Module.stringToUTF8OnStack(channel.label);
        Module._cells_rtc_on_data_channel(registryId, dcHandle, labelPtr);
    };

    pc.onnegotiationneeded = function() {
        Module._cells_rtc_on_negotiation_needed(registryId);
    };
});

// Get connection state
EM_JS(int, cells_rtc_get_connection_state, (int handle), {
    const pc = Module._rtcPeerConnections.get(handle);
    if (!pc) return 5; // CLOSED
    switch (pc.connectionState) {
        case 'new': return 0;
        case 'connecting': return 1;
        case 'connected': return 2;
        case 'disconnected': return 3;
        case 'failed': return 4;
        case 'closed': return 5;
    }
    return 0;
});

// Get ICE connection state
EM_JS(int, cells_rtc_get_ice_connection_state, (int handle), {
    const pc = Module._rtcPeerConnections.get(handle);
    if (!pc) return 6; // CLOSED
    switch (pc.iceConnectionState) {
        case 'new': return 0;
        case 'checking': return 1;
        case 'connected': return 2;
        case 'completed': return 3;
        case 'disconnected': return 4;
        case 'failed': return 5;
        case 'closed': return 6;
    }
    return 0;
});

// Get ICE gathering state
EM_JS(int, cells_rtc_get_ice_gathering_state, (int handle), {
    const pc = Module._rtcPeerConnections.get(handle);
    if (!pc) return 2; // COMPLETE
    switch (pc.iceGatheringState) {
        case 'new': return 0;
        case 'gathering': return 1;
        case 'complete': return 2;
    }
    return 0;
});

// Get signaling state
EM_JS(int, cells_rtc_get_signaling_state, (int handle), {
    const pc = Module._rtcPeerConnections.get(handle);
    if (!pc) return 5; // CLOSED
    switch (pc.signalingState) {
        case 'stable': return 0;
        case 'have-local-offer': return 1;
        case 'have-remote-offer': return 2;
        case 'have-local-pranswer': return 3;
        case 'have-remote-pranswer': return 4;
        case 'closed': return 5;
    }
    return 0;
});

// clang-format on

class WebRTCPeerConnection : public RTCPeerConnection {
public:
    explicit WebRTCPeerConnection(const RTCConfiguration& config) {
        cells_rtc_init();

        // Build ICE servers JSON
        std::string ice_json = buildIceServersJson(config);

        js_handle_ = cells_rtc_create(ice_json.empty() ? nullptr : ice_json.c_str());

        if (js_handle_ <= 0) {
            return;
        }

        // Register this instance
        std::lock_guard<std::mutex> lock(g_pc_mutex);
        registry_id_ = g_next_pc_id++;
        g_pc_registry[registry_id_] = this;

        // Setup callbacks
        cells_rtc_setup_callbacks(js_handle_, registry_id_);
    }

    ~WebRTCPeerConnection() override {
        // Unregister
        {
            std::lock_guard<std::mutex> lock(g_pc_mutex);
            g_pc_registry.erase(registry_id_);
        }

        if (js_handle_ > 0) {
            cells_rtc_destroy(js_handle_);
        }
    }

    void createOffer(CreateSDPCallback callback) override {
        pending_create_callback_ = std::move(callback);
        pending_sdp_type_ = SDPType::OFFER;
        cells_rtc_create_offer(js_handle_, registry_id_);
    }

    void createAnswer(CreateSDPCallback callback) override {
        pending_create_callback_ = std::move(callback);
        pending_sdp_type_ = SDPType::ANSWER;
        cells_rtc_create_answer(js_handle_, registry_id_);
    }

    void setLocalDescription(const SessionDescription& sdp, SetSDPCallback callback) override {
        pending_set_callback_ = std::move(callback);
        local_description_ = sdp;
        cells_rtc_set_local_description(js_handle_, registry_id_, sdpTypeToString(sdp.type),
                                        sdp.sdp.c_str());
    }

    void setRemoteDescription(const SessionDescription& sdp, SetSDPCallback callback) override {
        pending_set_callback_ = std::move(callback);
        remote_description_ = sdp;
        cells_rtc_set_remote_description(js_handle_, registry_id_, sdpTypeToString(sdp.type),
                                         sdp.sdp.c_str());
    }

    void addIceCandidate(const ICECandidate& candidate, SetSDPCallback callback) override {
        pending_set_callback_ = std::move(callback);
        cells_rtc_add_ice_candidate(js_handle_, registry_id_, candidate.candidate.c_str(),
                                    candidate.sdp_mid.empty() ? nullptr : candidate.sdp_mid.c_str(),
                                    candidate.sdp_mline_index);
    }

    std::unique_ptr<RTCDataChannel> createDataChannel(const std::string& label,
                                                      const DataChannelConfig& config) override {
        int dc_handle =
            cells_rtc_create_data_channel(js_handle_, label.c_str(), config.ordered ? 1 : 0,
                                          config.max_retransmits, config.max_packet_life_time);

        if (dc_handle < 0) {
            return nullptr;
        }

        return createWebRTCDataChannel(dc_handle, label, config);
    }

    void close() override { cells_rtc_close(js_handle_); }

    [[nodiscard]] PeerConnectionState getConnectionState() const override {
        return static_cast<PeerConnectionState>(cells_rtc_get_connection_state(js_handle_));
    }

    [[nodiscard]] ICEConnectionState getICEConnectionState() const override {
        return static_cast<ICEConnectionState>(cells_rtc_get_ice_connection_state(js_handle_));
    }

    [[nodiscard]] ICEGatheringState getICEGatheringState() const override {
        return static_cast<ICEGatheringState>(cells_rtc_get_ice_gathering_state(js_handle_));
    }

    [[nodiscard]] SignalingState getSignalingState() const override {
        return static_cast<SignalingState>(cells_rtc_get_signaling_state(js_handle_));
    }

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
        if (pending_set_callback_) {
            auto callback = std::move(pending_set_callback_);
            callback(success, error ? error : "");
        }
    }

    void jsOnConnectionState(int state) {
        notifyConnectionStateChange(static_cast<PeerConnectionState>(state));
    }

    void jsOnICEConnectionState(int state) {
        notifyICEConnectionStateChange(static_cast<ICEConnectionState>(state));
    }

    void jsOnICEGatheringState(int state) {
        notifyICEGatheringStateChange(static_cast<ICEGatheringState>(state));
    }

    void jsOnSignalingState(int state) {
        notifySignalingStateChange(static_cast<SignalingState>(state));
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
        DataChannelConfig config;  // Default config for received channels
        auto channel = createWebRTCDataChannel(dc_handle, label ? label : "", config);
        notifyDataChannel(std::move(channel));
    }

    void jsOnNegotiationNeeded() { notifyNegotiationNeeded(); }

private:
    int js_handle_ = 0;
    int registry_id_ = 0;

    SessionDescription local_description_;
    SessionDescription remote_description_;

    CreateSDPCallback pending_create_callback_;
    SetSDPCallback pending_set_callback_;
    SDPType pending_sdp_type_ = SDPType::OFFER;

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
