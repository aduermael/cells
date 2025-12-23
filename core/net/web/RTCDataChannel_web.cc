// Web (Emscripten) RTCDataChannel implementation
// Uses browser's RTCDataChannel API via EM_JS

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/val.h>
#include <map>
#include <mutex>

#include "core/net/include/RTCDataChannel.h"

namespace cells::net {

// Global registry for DataChannel instances (for callbacks from JS)
static std::map<int, class WebRTCDataChannel*> g_channel_registry;
static std::mutex g_channel_mutex;
static int g_next_channel_id = 1;

class WebRTCDataChannel : public RTCDataChannel {
public:
    explicit WebRTCDataChannel(int js_handle) : js_handle_(js_handle) {
        // Register this instance
        std::lock_guard<std::mutex> lock(g_channel_mutex);
        registry_id_ = g_next_channel_id++;
        g_channel_registry[registry_id_] = this;
    }

    ~WebRTCDataChannel() override {
        // Unregister
        std::lock_guard<std::mutex> lock(g_channel_mutex);
        g_channel_registry.erase(registry_id_);

        // Close and destroy JS handle
        if (js_handle_ >= 0) {
            destroyChannel(js_handle_);
        }
    }

    bool send(const std::string& message) override {
        if (state_ != DataChannelState::OPEN || js_handle_ < 0) {
            return false;
        }
        sendText(js_handle_, message.c_str(), message.size());
        return true;
    }

    bool sendBinary(const std::vector<uint8_t>& data) override {
        if (state_ != DataChannelState::OPEN || js_handle_ < 0) {
            return false;
        }
        sendBinaryData(js_handle_, data.data(), data.size());
        return true;
    }

    void close() override {
        if (js_handle_ >= 0) {
            closeChannel(js_handle_);
        }
    }

    [[nodiscard]] uint64_t getBufferedAmount() const override {
        if (js_handle_ < 0)
            return 0;
        return static_cast<uint64_t>(getBufferedAmountJS(js_handle_));
    }

    void setBufferedAmountLowThreshold(uint64_t threshold) override {
        buffered_amount_low_threshold_ = threshold;
        if (js_handle_ >= 0) {
            setBufferedAmountLowThresholdJS(js_handle_, static_cast<double>(threshold));
        }
    }

    [[nodiscard]] uint64_t getBufferedAmountLowThreshold() const override {
        return buffered_amount_low_threshold_;
    }

    // Setup callbacks from JS
    void setupCallbacks() {
        if (js_handle_ >= 0) {
            setupChannelCallbacks(js_handle_, registry_id_);
        }
    }

    // Get registry ID for JS callbacks
    [[nodiscard]] int getRegistryId() const { return registry_id_; }

    // Called from JS callbacks
    void jsOnOpen() { notifyOpen(); }

    void jsOnClose() { notifyClose(); }

    void jsOnMessage(const char* data, int length, bool is_binary) {
        if (is_binary) {
            std::vector<uint8_t> binary_data(data, data + length);
            notifyData(binary_data);
        } else {
            notifyMessage(std::string(data, static_cast<size_t>(length)));
        }
    }

    void jsOnError(const char* error) { notifyError(std::string(error)); }

private:
    int js_handle_ = -1;
    int registry_id_ = 0;
    uint64_t buffered_amount_low_threshold_ = 0;

    // JS interop functions
    static void sendText(int handle, const char* data, size_t length);
    static void sendBinaryData(int handle, const uint8_t* data, size_t length);
    static void closeChannel(int handle);
    static void destroyChannel(int handle);
    static double getBufferedAmountJS(int handle);
    static void setBufferedAmountLowThresholdJS(int handle, double threshold);
    static void setupChannelCallbacks(int handle, int registry_id);
};

// JS interop implementations via EM_JS

// clang-format off
EM_JS(void, cells_dc_send_text, (int handle, const char* data, int length), {
    const channel = Module._rtcDataChannels.get(handle);
    if (channel && channel.readyState === 'open') {
        channel.send(UTF8ToString(data, length));
    }
});

EM_JS(void, cells_dc_send_binary, (int handle, const void* data, int length), {
    const channel = Module._rtcDataChannels.get(handle);
    if (channel && channel.readyState === 'open') {
        const buffer = new Uint8Array(Module.HEAPU8.buffer, data, length);
        channel.send(buffer.slice().buffer);
    }
});

EM_JS(void, cells_dc_close, (int handle), {
    const channel = Module._rtcDataChannels.get(handle);
    if (channel) {
        channel.close();
    }
});

EM_JS(void, cells_dc_destroy, (int handle), {
    const channel = Module._rtcDataChannels.get(handle);
    if (channel) {
        channel.onopen = null;
        channel.onclose = null;
        channel.onmessage = null;
        channel.onerror = null;
        Module._rtcDataChannels.delete(handle);
    }
});

EM_JS(double, cells_dc_get_buffered_amount, (int handle), {
    const channel = Module._rtcDataChannels.get(handle);
    return channel ? channel.bufferedAmount : 0;
});

EM_JS(void, cells_dc_set_buffered_amount_low_threshold, (int handle, double threshold), {
    const channel = Module._rtcDataChannels.get(handle);
    if (channel) {
        channel.bufferedAmountLowThreshold = threshold;
    }
});

EM_JS(void, cells_dc_setup_callbacks, (int handle, int registryId), {
    const channel = Module._rtcDataChannels.get(handle);
    if (!channel) return;

    channel.onopen = function() {
        Module._cells_dc_on_open(registryId);
    };

    channel.onclose = function() {
        Module._cells_dc_on_close(registryId);
    };

    channel.onerror = function(event) {
        const errorMsg = event.error ? event.error.message : 'DataChannel error';
        const msgPtr = Module.stringToUTF8OnStack(errorMsg);
        Module._cells_dc_on_error(registryId, msgPtr);
    };

    channel.onmessage = function(event) {
        if (typeof event.data === 'string') {
            const msgPtr = Module.stringToUTF8OnStack(event.data);
            Module._cells_dc_on_message(registryId, msgPtr, event.data.length, 0);
        } else if (event.data instanceof ArrayBuffer) {
            const data = new Uint8Array(event.data);
            const ptr = Module._malloc(data.length);
            Module.HEAPU8.set(data, ptr);
            Module._cells_dc_on_message(registryId, ptr, data.length, 1);
            Module._free(ptr);
        }
    };
});
// clang-format on

void WebRTCDataChannel::sendText(int handle, const char* data, size_t length) {
    cells_dc_send_text(handle, data, static_cast<int>(length));
}

void WebRTCDataChannel::sendBinaryData(int handle, const uint8_t* data, size_t length) {
    cells_dc_send_binary(handle, data, static_cast<int>(length));
}

void WebRTCDataChannel::closeChannel(int handle) {
    cells_dc_close(handle);
}

void WebRTCDataChannel::destroyChannel(int handle) {
    cells_dc_destroy(handle);
}

double WebRTCDataChannel::getBufferedAmountJS(int handle) {
    return cells_dc_get_buffered_amount(handle);
}

void WebRTCDataChannel::setBufferedAmountLowThresholdJS(int handle, double threshold) {
    cells_dc_set_buffered_amount_low_threshold(handle, threshold);
}

void WebRTCDataChannel::setupChannelCallbacks(int handle, int registry_id) {
    cells_dc_setup_callbacks(handle, registry_id);
}

// Exported C functions for JS callbacks
extern "C" {

EMSCRIPTEN_KEEPALIVE
void _cells_dc_on_open(int registry_id) {
    std::lock_guard<std::mutex> lock(g_channel_mutex);
    auto it = g_channel_registry.find(registry_id);
    if (it != g_channel_registry.end()) {
        it->second->jsOnOpen();
    }
}

EMSCRIPTEN_KEEPALIVE
void _cells_dc_on_close(int registry_id) {
    std::lock_guard<std::mutex> lock(g_channel_mutex);
    auto it = g_channel_registry.find(registry_id);
    if (it != g_channel_registry.end()) {
        it->second->jsOnClose();
    }
}

EMSCRIPTEN_KEEPALIVE
void _cells_dc_on_message(int registry_id, const char* data, int length, int is_binary) {
    std::lock_guard<std::mutex> lock(g_channel_mutex);
    auto it = g_channel_registry.find(registry_id);
    if (it != g_channel_registry.end()) {
        it->second->jsOnMessage(data, length, is_binary != 0);
    }
}

EMSCRIPTEN_KEEPALIVE
void _cells_dc_on_error(int registry_id, const char* error) {
    std::lock_guard<std::mutex> lock(g_channel_mutex);
    auto it = g_channel_registry.find(registry_id);
    if (it != g_channel_registry.end()) {
        it->second->jsOnError(error);
    }
}

}  // extern "C"

// Factory function to create WebRTCDataChannel from JS handle
std::unique_ptr<RTCDataChannel> createWebRTCDataChannel(int js_handle, const std::string& label,
                                                        const DataChannelConfig& config) {
    auto channel = std::make_unique<WebRTCDataChannel>(js_handle);
    channel->setLabel(label);
    channel->setOrdered(config.ordered);
    channel->setMaxRetransmits(config.max_retransmits);
    channel->setMaxPacketLifeTime(config.max_packet_life_time);
    channel->setProtocol(config.protocol);
    channel->setNegotiated(config.negotiated);
    if (config.id >= 0) {
        channel->setId(config.id);
    }
    channel->setupCallbacks();
    return channel;
}

}  // namespace cells::net

#endif  // __EMSCRIPTEN__
