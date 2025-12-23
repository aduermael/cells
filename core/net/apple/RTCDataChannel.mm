// Apple (iOS/macOS) RTCDataChannel implementation
// Uses WebRTC.framework (stasel/WebRTC) for native WebRTC support
//
// NOTE: This requires WebRTC.framework to be linked.
// Install via CocoaPods: pod 'WebRTC-lib'
// Or via SPM: https://github.com/stasel/WebRTC

#if defined(__APPLE__)

#import <Foundation/Foundation.h>

// Conditionally include WebRTC framework headers
// When WebRTC.framework is not available, we provide stub implementations
#if __has_include(<WebRTC/WebRTC.h>)
#import <WebRTC/WebRTC.h>
#define CELLS_HAS_WEBRTC 1
#else
#define CELLS_HAS_WEBRTC 0
#endif

#include "core/net/include/RTCDataChannel.h"

namespace cells::net {

#if CELLS_HAS_WEBRTC

// Delegate for RTCDataChannel events
@interface CellsDataChannelDelegate : NSObject <RTCDataChannelDelegate>
@property(nonatomic, assign) RTCDataChannel* channel;
@end

@implementation CellsDataChannelDelegate

- (void)dataChannelDidChangeState:(RTCDataChannel*)dataChannel {
    if (_channel != nullptr) {
        switch (dataChannel.readyState) {
            case RTCDataChannelStateOpen:
                _channel->notifyOpen();
                break;
            case RTCDataChannelStateClosed:
                _channel->notifyClose();
                break;
            default:
                break;
        }
    }
}

- (void)dataChannel:(RTCDataChannel*)dataChannel didReceiveMessageWithBuffer:(RTCDataBuffer*)buffer {
    if (_channel == nullptr) {
        return;
    }

    NSData* data = buffer.data;
    if (buffer.isBinary) {
        std::vector<uint8_t> bytes(static_cast<const uint8_t*>(data.bytes),
                                   static_cast<const uint8_t*>(data.bytes) + data.length);
        _channel->notifyData(bytes);
    } else {
        NSString* text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
        _channel->notifyMessage([text UTF8String]);
    }
}

- (void)dataChannel:(RTCDataChannel*)dataChannel didChangeBufferedAmount:(uint64_t)amount {
    // Could notify buffered amount change if needed
}

@end

class AppleRTCDataChannel : public RTCDataChannel {
public:
    AppleRTCDataChannel(RTCDataChannel* native_channel) : native_channel_(native_channel) {
        delegate_ = [[CellsDataChannelDelegate alloc] init];
        delegate_.channel = this;
        native_channel_.delegate = delegate_;

        // Copy properties
        setLabel([native_channel_.label UTF8String]);
        setId(native_channel_.channelId);
        setOrdered(native_channel_.isOrdered);
        setMaxRetransmits(native_channel_.maxRetransmits);
        setMaxPacketLifeTime(native_channel_.maxPacketLifeTime);
        setProtocol([native_channel_.protocol UTF8String]);
        setNegotiated(native_channel_.isNegotiated);
    }

    ~AppleRTCDataChannel() override {
        if (native_channel_ != nil) {
            native_channel_.delegate = nil;
            [native_channel_ close];
            native_channel_ = nil;
        }
        delegate_ = nil;
    }

    bool send(const std::string& message) override {
        if (native_channel_ == nil || native_channel_.readyState != RTCDataChannelStateOpen) {
            return false;
        }

        NSData* data = [NSData dataWithBytes:message.c_str() length:message.size()];
        RTCDataBuffer* buffer = [[RTCDataBuffer alloc] initWithData:data isBinary:NO];
        return [native_channel_ sendData:buffer];
    }

    bool sendBinary(const std::vector<uint8_t>& data) override {
        if (native_channel_ == nil || native_channel_.readyState != RTCDataChannelStateOpen) {
            return false;
        }

        NSData* nsData = [NSData dataWithBytes:data.data() length:data.size()];
        RTCDataBuffer* buffer = [[RTCDataBuffer alloc] initWithData:nsData isBinary:YES];
        return [native_channel_ sendData:buffer];
    }

    void close() override {
        if (native_channel_ != nil) {
            [native_channel_ close];
        }
    }

    [[nodiscard]] uint64_t getBufferedAmount() const override {
        return native_channel_ != nil ? native_channel_.bufferedAmount : 0;
    }

    void setBufferedAmountLowThreshold(uint64_t threshold) override {
        buffered_amount_low_threshold_ = threshold;
        // WebRTC.framework doesn't have a direct bufferedAmountLowThreshold property
    }

    [[nodiscard]] uint64_t getBufferedAmountLowThreshold() const override {
        return buffered_amount_low_threshold_;
    }

private:
    RTCDataChannel* native_channel_ = nil;
    CellsDataChannelDelegate* delegate_ = nil;
    uint64_t buffered_amount_low_threshold_ = 0;
};

// Factory function
std::unique_ptr<RTCDataChannel> createAppleRTCDataChannel(RTCDataChannel* native_channel) {
    return std::make_unique<AppleRTCDataChannel>(native_channel);
}

#else  // !CELLS_HAS_WEBRTC

// Stub implementation when WebRTC.framework is not available
class StubRTCDataChannel : public RTCDataChannel {
public:
    StubRTCDataChannel() { setState(DataChannelState::CLOSED); }

    bool send(const std::string& /*message*/) override { return false; }

    bool sendBinary(const std::vector<uint8_t>& /*data*/) override { return false; }

    void close() override {}

    [[nodiscard]] uint64_t getBufferedAmount() const override { return 0; }

    void setBufferedAmountLowThreshold(uint64_t /*threshold*/) override {}

    [[nodiscard]] uint64_t getBufferedAmountLowThreshold() const override { return 0; }
};

#endif  // CELLS_HAS_WEBRTC

}  // namespace cells::net

#endif  // __APPLE__
