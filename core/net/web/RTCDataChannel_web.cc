// Web (Emscripten) RTCDataChannel implementation
// Uses message passing to main thread where the actual DataChannel exists.
// The WASM runs in a Web Worker, but DataChannel is created on main thread.

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

// clang-format off

// Send text message via main thread
EM_JS(void, cells_dc_send_text, (int handle, const char* data, int length), {
    self.postMessage({
        type: 'dc_send',
        dcHandle: handle,
        isText: true,
        data: UTF8ToString(data, length)
    });
});

// Send binary message via main thread
EM_JS(void, cells_dc_send_binary, (int handle, const void* data, int length), {
    const buffer = new Uint8Array(Module.HEAPU8.buffer, data, length);
    // Copy the data since it may be freed after this call
    const dataCopy = Array.from(buffer);
    self.postMessage({
        type: 'dc_send',
        dcHandle: handle,
        isText: false,
        data: dataCopy
    });
});

// Close channel via main thread
EM_JS(void, cells_dc_close, (int handle), {
    self.postMessage({
        type: 'dc_close',
        dcHandle: handle
    });
});

// Get buffered amount - this is tricky because we need sync response
// For now, return 0 as approximation (async would be better)
EM_JS(double, cells_dc_get_buffered_amount, (int handle), {
    // TODO: Implement async buffered amount query
    // For now return 0 - actual value would need sync message which is complex
    return 0;
});

// Set buffered amount low threshold via main thread
EM_JS(void, cells_dc_set_buffered_amount_low_threshold, (int handle, double threshold), {
    self.postMessage({
        type: 'dc_set_buffered_amount_low_threshold',
        dcHandle: handle,
        threshold: threshold
    });
});

// clang-format on

class WebRTCDataChannel : public RTCDataChannel {
public:
    explicit WebRTCDataChannel(int js_handle) : js_handle_(js_handle) {
        // Register this instance using the js_handle as the key
        // The js_handle comes from main thread and is used for callbacks
        std::lock_guard<std::mutex> lock(g_channel_mutex);
        g_channel_registry[js_handle] = this;
    }

    ~WebRTCDataChannel() override {
        // Unregister
        {
            std::lock_guard<std::mutex> lock(g_channel_mutex);
            g_channel_registry.erase(js_handle_);
        }

        // Close the channel on main thread
        if (js_handle_ >= 0) {
            cells_dc_close(js_handle_);
        }
    }

    bool send(const std::string& message) override {
        if (state_ != DataChannelState::OPEN || js_handle_ < 0) {
            return false;
        }
        cells_dc_send_text(js_handle_, message.c_str(), static_cast<int>(message.size()));
        return true;
    }

    bool sendBinary(const std::vector<uint8_t>& data) override {
        if (state_ != DataChannelState::OPEN || js_handle_ < 0) {
            return false;
        }
        cells_dc_send_binary(js_handle_, data.data(), static_cast<int>(data.size()));
        return true;
    }

    void close() override {
        if (js_handle_ >= 0) {
            cells_dc_close(js_handle_);
        }
    }

    [[nodiscard]] uint64_t getBufferedAmount() const override {
        if (js_handle_ < 0)
            return 0;
        return static_cast<uint64_t>(cells_dc_get_buffered_amount(js_handle_));
    }

    void setBufferedAmountLowThreshold(uint64_t threshold) override {
        buffered_amount_low_threshold_ = threshold;
        if (js_handle_ >= 0) {
            cells_dc_set_buffered_amount_low_threshold(js_handle_, static_cast<double>(threshold));
        }
    }

    [[nodiscard]] uint64_t getBufferedAmountLowThreshold() const override {
        return buffered_amount_low_threshold_;
    }

    // Get js handle for use in callbacks
    [[nodiscard]] int getJsHandle() const { return js_handle_; }

    // Called from JS callbacks (via message from main thread)
    void jsOnOpen() { notifyOpen(); }

    void jsOnClose() { notifyClose(); }

    void jsOnMessageText(const char* data) { notifyMessage(std::string(data)); }

    void jsOnMessageBinary(const uint8_t* data, int length) {
        std::vector<uint8_t> binary_data(data, data + length);
        notifyData(binary_data);
    }

    void jsOnError(const char* error) { notifyError(std::string(error)); }

private:
    int js_handle_ = -1;
    uint64_t buffered_amount_low_threshold_ = 0;
};

// Exported C functions for JS callbacks (called when main thread sends events)
extern "C" {

EMSCRIPTEN_KEEPALIVE
void _cells_dc_on_open(int dc_handle) {
    std::lock_guard<std::mutex> lock(g_channel_mutex);
    auto it = g_channel_registry.find(dc_handle);
    if (it != g_channel_registry.end()) {
        it->second->jsOnOpen();
    }
}

EMSCRIPTEN_KEEPALIVE
void _cells_dc_on_close(int dc_handle) {
    std::lock_guard<std::mutex> lock(g_channel_mutex);
    auto it = g_channel_registry.find(dc_handle);
    if (it != g_channel_registry.end()) {
        it->second->jsOnClose();
    }
}

EMSCRIPTEN_KEEPALIVE
void _cells_dc_on_message_text(int dc_handle, const char* data) {
    std::lock_guard<std::mutex> lock(g_channel_mutex);
    auto it = g_channel_registry.find(dc_handle);
    if (it != g_channel_registry.end()) {
        it->second->jsOnMessageText(data);
    }
}

EMSCRIPTEN_KEEPALIVE
void _cells_dc_on_message_binary(int dc_handle, const uint8_t* data, int length) {
    std::lock_guard<std::mutex> lock(g_channel_mutex);
    auto it = g_channel_registry.find(dc_handle);
    if (it != g_channel_registry.end()) {
        it->second->jsOnMessageBinary(data, length);
    }
}

EMSCRIPTEN_KEEPALIVE
void _cells_dc_on_error(int dc_handle, const char* error) {
    std::lock_guard<std::mutex> lock(g_channel_mutex);
    auto it = g_channel_registry.find(dc_handle);
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
    // Callbacks are set up by the main thread RTCProxy, no need to call setupCallbacks
    return channel;
}

}  // namespace cells::net

#endif  // __EMSCRIPTEN__
