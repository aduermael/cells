// Apple (iOS/macOS) RTCPeerConnection implementation
// Uses WebRTC.framework (stasel/WebRTC) for native WebRTC support
//
// NOTE: This requires WebRTC.framework to be linked.
// Install via CocoaPods: pod 'WebRTC-lib'
// Or via SPM: https://github.com/stasel/WebRTC

#if defined(__APPLE__)

#import <Foundation/Foundation.h>

// Conditionally include WebRTC framework headers
#if __has_include(<WebRTC/WebRTC.h>)
#import <WebRTC/WebRTC.h>
#define CELLS_HAS_WEBRTC 1
#else
#define CELLS_HAS_WEBRTC 0
#endif

#include "core/net/include/RTCPeerConnection.h"

namespace cells::net {

#if CELLS_HAS_WEBRTC

// Forward declaration
std::unique_ptr<RTCDataChannel> createAppleRTCDataChannel(RTCDataChannel* native_channel);

// Delegate for RTCPeerConnection events
@interface CellsPeerConnectionDelegate : NSObject <RTCPeerConnectionDelegate>
@property(nonatomic, assign) class AppleRTCPeerConnection* connection;
@end

class AppleRTCPeerConnection : public RTCPeerConnection {
public:
    explicit AppleRTCPeerConnection(const RTCConfiguration& config) {
        @autoreleasepool {
            // Create factory
            RTCDefaultVideoDecoderFactory* decoderFactory = [[RTCDefaultVideoDecoderFactory alloc] init];
            RTCDefaultVideoEncoderFactory* encoderFactory = [[RTCDefaultVideoEncoderFactory alloc] init];
            factory_ = [[RTCPeerConnectionFactory alloc] initWithEncoderFactory:encoderFactory
                                                                 decoderFactory:decoderFactory];

            // Build configuration
            RTCConfiguration* rtcConfig = [[RTCConfiguration alloc] init];
            rtcConfig.iceServers = buildIceServers(config);
            rtcConfig.sdpSemantics = RTCSdpSemanticsUnifiedPlan;

            if (config.ice_transport_policy == ICETransportPolicy::RELAY) {
                rtcConfig.iceTransportPolicy = RTCIceTransportPolicyRelay;
            }

            // Create constraints
            RTCMediaConstraints* constraints =
                [[RTCMediaConstraints alloc] initWithMandatoryConstraints:nil optionalConstraints:nil];

            // Create delegate
            delegate_ = [[CellsPeerConnectionDelegate alloc] init];
            delegate_.connection = this;

            // Create peer connection
            native_pc_ = [factory_ peerConnectionWithConfiguration:rtcConfig
                                                       constraints:constraints
                                                          delegate:delegate_];
        }
    }

    ~AppleRTCPeerConnection() override {
        if (native_pc_ != nil) {
            [native_pc_ close];
            native_pc_ = nil;
        }
        delegate_.connection = nullptr;
        delegate_ = nil;
        factory_ = nil;
    }

    void createOffer(CreateSDPCallback callback) override {
        if (native_pc_ == nil) {
            callback(false, SessionDescription(), "PeerConnection not initialized");
            return;
        }

        RTCMediaConstraints* constraints =
            [[RTCMediaConstraints alloc] initWithMandatoryConstraints:nil optionalConstraints:nil];

        __block CreateSDPCallback cb = std::move(callback);
        [native_pc_
            offerForConstraints:constraints
              completionHandler:^(RTCSessionDescription* sdp, NSError* error) {
                dispatch_async(dispatch_get_main_queue(), ^{
                  if (error != nil) {
                      cb(false, SessionDescription(), [[error localizedDescription] UTF8String]);
                  } else {
                      cb(true, SessionDescription::offer([sdp.sdp UTF8String]), "");
                  }
                });
              }];
    }

    void createAnswer(CreateSDPCallback callback) override {
        if (native_pc_ == nil) {
            callback(false, SessionDescription(), "PeerConnection not initialized");
            return;
        }

        RTCMediaConstraints* constraints =
            [[RTCMediaConstraints alloc] initWithMandatoryConstraints:nil optionalConstraints:nil];

        __block CreateSDPCallback cb = std::move(callback);
        [native_pc_
            answerForConstraints:constraints
               completionHandler:^(RTCSessionDescription* sdp, NSError* error) {
                 dispatch_async(dispatch_get_main_queue(), ^{
                   if (error != nil) {
                       cb(false, SessionDescription(), [[error localizedDescription] UTF8String]);
                   } else {
                       cb(true, SessionDescription::answer([sdp.sdp UTF8String]), "");
                   }
                 });
               }];
    }

    void setLocalDescription(const SessionDescription& sdp, SetSDPCallback callback) override {
        if (native_pc_ == nil) {
            callback(false, "PeerConnection not initialized");
            return;
        }

        RTCSdpType type = sdp.type == SDPType::OFFER ? RTCSdpTypeOffer : RTCSdpTypeAnswer;
        RTCSessionDescription* desc =
            [[RTCSessionDescription alloc] initWithType:type
                                                    sdp:[NSString stringWithUTF8String:sdp.sdp.c_str()]];

        __block SetSDPCallback cb = std::move(callback);
        __block SessionDescription localSdp = sdp;
        __weak AppleRTCPeerConnection* weakSelf = this;

        [native_pc_ setLocalDescription:desc
                      completionHandler:^(NSError* error) {
                        dispatch_async(dispatch_get_main_queue(), ^{
                          AppleRTCPeerConnection* strongSelf = weakSelf;
                          if (strongSelf != nullptr) {
                              strongSelf->local_description_ = localSdp;
                          }
                          if (error != nil) {
                              cb(false, [[error localizedDescription] UTF8String]);
                          } else {
                              cb(true, "");
                          }
                        });
                      }];
    }

    void setRemoteDescription(const SessionDescription& sdp, SetSDPCallback callback) override {
        if (native_pc_ == nil) {
            callback(false, "PeerConnection not initialized");
            return;
        }

        RTCSdpType type = sdp.type == SDPType::OFFER ? RTCSdpTypeOffer : RTCSdpTypeAnswer;
        RTCSessionDescription* desc =
            [[RTCSessionDescription alloc] initWithType:type
                                                    sdp:[NSString stringWithUTF8String:sdp.sdp.c_str()]];

        __block SetSDPCallback cb = std::move(callback);
        __block SessionDescription remoteSdp = sdp;
        __weak AppleRTCPeerConnection* weakSelf = this;

        [native_pc_ setRemoteDescription:desc
                       completionHandler:^(NSError* error) {
                         dispatch_async(dispatch_get_main_queue(), ^{
                           AppleRTCPeerConnection* strongSelf = weakSelf;
                           if (strongSelf != nullptr) {
                               strongSelf->remote_description_ = remoteSdp;
                           }
                           if (error != nil) {
                               cb(false, [[error localizedDescription] UTF8String]);
                           } else {
                               cb(true, "");
                           }
                         });
                       }];
    }

    void addIceCandidate(const ICECandidate& candidate, SetSDPCallback callback) override {
        if (native_pc_ == nil) {
            callback(false, "PeerConnection not initialized");
            return;
        }

        RTCIceCandidate* rtcCandidate =
            [[RTCIceCandidate alloc] initWithSdp:[NSString stringWithUTF8String:candidate.candidate.c_str()]
                                   sdpMLineIndex:candidate.sdp_mline_index
                                          sdpMid:[NSString stringWithUTF8String:candidate.sdp_mid.c_str()]];

        __block SetSDPCallback cb = std::move(callback);
        [native_pc_ addIceCandidate:rtcCandidate
                  completionHandler:^(NSError* error) {
                    dispatch_async(dispatch_get_main_queue(), ^{
                      if (error != nil) {
                          cb(false, [[error localizedDescription] UTF8String]);
                      } else {
                          cb(true, "");
                      }
                    });
                  }];
    }

    std::unique_ptr<RTCDataChannel> createDataChannel(const std::string& label,
                                                      const DataChannelConfig& config) override {
        if (native_pc_ == nil) {
            return nullptr;
        }

        RTCDataChannelConfiguration* dcConfig = [[RTCDataChannelConfiguration alloc] init];
        dcConfig.isOrdered = config.ordered;
        if (config.max_retransmits >= 0) {
            dcConfig.maxRetransmits = config.max_retransmits;
        }
        if (config.max_packet_life_time >= 0) {
            dcConfig.maxPacketLifeTime = config.max_packet_life_time;
        }
        if (!config.protocol.empty()) {
            dcConfig.protocol = [NSString stringWithUTF8String:config.protocol.c_str()];
        }
        dcConfig.isNegotiated = config.negotiated;
        if (config.id >= 0) {
            dcConfig.channelId = config.id;
        }

        RTCDataChannel* channel =
            [native_pc_ dataChannelForLabel:[NSString stringWithUTF8String:label.c_str()]
                              configuration:dcConfig];

        if (channel == nil) {
            return nullptr;
        }

        return createAppleRTCDataChannel(channel);
    }

    void close() override {
        if (native_pc_ != nil) {
            [native_pc_ close];
        }
    }

    [[nodiscard]] PeerConnectionState getConnectionState() const override {
        if (native_pc_ == nil) {
            return PeerConnectionState::CLOSED;
        }

        switch (native_pc_.connectionState) {
            case RTCPeerConnectionStateNew:
                return PeerConnectionState::NEW;
            case RTCPeerConnectionStateConnecting:
                return PeerConnectionState::CONNECTING;
            case RTCPeerConnectionStateConnected:
                return PeerConnectionState::CONNECTED;
            case RTCPeerConnectionStateDisconnected:
                return PeerConnectionState::DISCONNECTED;
            case RTCPeerConnectionStateFailed:
                return PeerConnectionState::FAILED;
            case RTCPeerConnectionStateClosed:
                return PeerConnectionState::CLOSED;
        }
        return PeerConnectionState::CLOSED;
    }

    [[nodiscard]] ICEConnectionState getICEConnectionState() const override {
        if (native_pc_ == nil) {
            return ICEConnectionState::CLOSED;
        }

        switch (native_pc_.iceConnectionState) {
            case RTCIceConnectionStateNew:
                return ICEConnectionState::NEW;
            case RTCIceConnectionStateChecking:
                return ICEConnectionState::CHECKING;
            case RTCIceConnectionStateConnected:
                return ICEConnectionState::CONNECTED;
            case RTCIceConnectionStateCompleted:
                return ICEConnectionState::COMPLETED;
            case RTCIceConnectionStateDisconnected:
                return ICEConnectionState::DISCONNECTED;
            case RTCIceConnectionStateFailed:
                return ICEConnectionState::FAILED;
            case RTCIceConnectionStateClosed:
                return ICEConnectionState::CLOSED;
            case RTCIceConnectionStateCount:
                return ICEConnectionState::CLOSED;
        }
        return ICEConnectionState::CLOSED;
    }

    [[nodiscard]] ICEGatheringState getICEGatheringState() const override {
        if (native_pc_ == nil) {
            return ICEGatheringState::COMPLETE;
        }

        switch (native_pc_.iceGatheringState) {
            case RTCIceGatheringStateNew:
                return ICEGatheringState::NEW;
            case RTCIceGatheringStateGathering:
                return ICEGatheringState::GATHERING;
            case RTCIceGatheringStateComplete:
                return ICEGatheringState::COMPLETE;
        }
        return ICEGatheringState::COMPLETE;
    }

    [[nodiscard]] SignalingState getSignalingState() const override {
        if (native_pc_ == nil) {
            return SignalingState::CLOSED;
        }

        switch (native_pc_.signalingState) {
            case RTCSignalingStateStable:
                return SignalingState::STABLE;
            case RTCSignalingStateHaveLocalOffer:
                return SignalingState::HAVE_LOCAL_OFFER;
            case RTCSignalingStateHaveRemoteOffer:
                return SignalingState::HAVE_REMOTE_OFFER;
            case RTCSignalingStateHaveLocalPrAnswer:
                return SignalingState::HAVE_LOCAL_PRANSWER;
            case RTCSignalingStateHaveRemotePrAnswer:
                return SignalingState::HAVE_REMOTE_PRANSWER;
            case RTCSignalingStateClosed:
                return SignalingState::CLOSED;
        }
        return SignalingState::CLOSED;
    }

    [[nodiscard]] const SessionDescription* getLocalDescription() const override {
        return local_description_.sdp.empty() ? nullptr : &local_description_;
    }

    [[nodiscard]] const SessionDescription* getRemoteDescription() const override {
        return remote_description_.sdp.empty() ? nullptr : &remote_description_;
    }

    // Called from delegate
    void onConnectionStateChange(RTCPeerConnectionState state) {
        PeerConnectionState pcState;
        switch (state) {
            case RTCPeerConnectionStateNew:
                pcState = PeerConnectionState::NEW;
                break;
            case RTCPeerConnectionStateConnecting:
                pcState = PeerConnectionState::CONNECTING;
                break;
            case RTCPeerConnectionStateConnected:
                pcState = PeerConnectionState::CONNECTED;
                break;
            case RTCPeerConnectionStateDisconnected:
                pcState = PeerConnectionState::DISCONNECTED;
                break;
            case RTCPeerConnectionStateFailed:
                pcState = PeerConnectionState::FAILED;
                break;
            case RTCPeerConnectionStateClosed:
                pcState = PeerConnectionState::CLOSED;
                break;
        }
        notifyConnectionStateChange(pcState);
    }

    void onIceConnectionStateChange(RTCIceConnectionState state) {
        ICEConnectionState iceState;
        switch (state) {
            case RTCIceConnectionStateNew:
                iceState = ICEConnectionState::NEW;
                break;
            case RTCIceConnectionStateChecking:
                iceState = ICEConnectionState::CHECKING;
                break;
            case RTCIceConnectionStateConnected:
                iceState = ICEConnectionState::CONNECTED;
                break;
            case RTCIceConnectionStateCompleted:
                iceState = ICEConnectionState::COMPLETED;
                break;
            case RTCIceConnectionStateDisconnected:
                iceState = ICEConnectionState::DISCONNECTED;
                break;
            case RTCIceConnectionStateFailed:
                iceState = ICEConnectionState::FAILED;
                break;
            case RTCIceConnectionStateClosed:
            case RTCIceConnectionStateCount:
                iceState = ICEConnectionState::CLOSED;
                break;
        }
        notifyICEConnectionStateChange(iceState);
    }

    void onIceGatheringStateChange(RTCIceGatheringState state) {
        ICEGatheringState gatherState;
        switch (state) {
            case RTCIceGatheringStateNew:
                gatherState = ICEGatheringState::NEW;
                break;
            case RTCIceGatheringStateGathering:
                gatherState = ICEGatheringState::GATHERING;
                break;
            case RTCIceGatheringStateComplete:
                gatherState = ICEGatheringState::COMPLETE;
                break;
        }
        notifyICEGatheringStateChange(gatherState);
    }

    void onSignalingStateChange(RTCSignalingState state) {
        SignalingState sigState;
        switch (state) {
            case RTCSignalingStateStable:
                sigState = SignalingState::STABLE;
                break;
            case RTCSignalingStateHaveLocalOffer:
                sigState = SignalingState::HAVE_LOCAL_OFFER;
                break;
            case RTCSignalingStateHaveRemoteOffer:
                sigState = SignalingState::HAVE_REMOTE_OFFER;
                break;
            case RTCSignalingStateHaveLocalPrAnswer:
                sigState = SignalingState::HAVE_LOCAL_PRANSWER;
                break;
            case RTCSignalingStateHaveRemotePrAnswer:
                sigState = SignalingState::HAVE_REMOTE_PRANSWER;
                break;
            case RTCSignalingStateClosed:
                sigState = SignalingState::CLOSED;
                break;
        }
        notifySignalingStateChange(sigState);
    }

    void onIceCandidate(RTCIceCandidate* candidate) {
        ICECandidate cand([candidate.sdp UTF8String], candidate.sdpMid ? [candidate.sdpMid UTF8String] : "",
                         candidate.sdpMLineIndex);
        notifyICECandidate(cand);
    }

    void onDataChannel(RTCDataChannel* channel) {
        auto wrapped = createAppleRTCDataChannel(channel);
        notifyDataChannel(std::move(wrapped));
    }

    void onNegotiationNeeded() { notifyNegotiationNeeded(); }

private:
    RTCPeerConnectionFactory* factory_ = nil;
    RTCPeerConnection* native_pc_ = nil;
    CellsPeerConnectionDelegate* delegate_ = nil;
    SessionDescription local_description_;
    SessionDescription remote_description_;

    static NSArray<RTCIceServer*>* buildIceServers(const RTCConfiguration& config) {
        NSMutableArray<RTCIceServer*>* servers = [NSMutableArray array];

        for (const auto& server : config.ice_servers) {
            NSMutableArray<NSString*>* urls = [NSMutableArray array];
            for (const auto& url : server.urls) {
                [urls addObject:[NSString stringWithUTF8String:url.c_str()]];
            }

            RTCIceServer* iceServer;
            if (!server.username.empty() && !server.credential.empty()) {
                iceServer =
                    [[RTCIceServer alloc] initWithURLStrings:urls
                                                   username:[NSString stringWithUTF8String:server.username.c_str()]
                                                 credential:[NSString stringWithUTF8String:server.credential.c_str()]];
            } else {
                iceServer = [[RTCIceServer alloc] initWithURLStrings:urls];
            }
            [servers addObject:iceServer];
        }

        return servers;
    }
};

@implementation CellsPeerConnectionDelegate

- (void)peerConnection:(RTCPeerConnection*)peerConnection didChangeConnectionState:(RTCPeerConnectionState)newState {
    if (_connection != nullptr) {
        dispatch_async(dispatch_get_main_queue(), ^{
          if (_connection != nullptr) {
              _connection->onConnectionStateChange(newState);
          }
        });
    }
}

- (void)peerConnection:(RTCPeerConnection*)peerConnection didChangeIceConnectionState:(RTCIceConnectionState)newState {
    if (_connection != nullptr) {
        dispatch_async(dispatch_get_main_queue(), ^{
          if (_connection != nullptr) {
              _connection->onIceConnectionStateChange(newState);
          }
        });
    }
}

- (void)peerConnection:(RTCPeerConnection*)peerConnection didChangeIceGatheringState:(RTCIceGatheringState)newState {
    if (_connection != nullptr) {
        dispatch_async(dispatch_get_main_queue(), ^{
          if (_connection != nullptr) {
              _connection->onIceGatheringStateChange(newState);
          }
        });
    }
}

- (void)peerConnection:(RTCPeerConnection*)peerConnection didChangeSignalingState:(RTCSignalingState)stateChanged {
    if (_connection != nullptr) {
        dispatch_async(dispatch_get_main_queue(), ^{
          if (_connection != nullptr) {
              _connection->onSignalingStateChange(stateChanged);
          }
        });
    }
}

- (void)peerConnection:(RTCPeerConnection*)peerConnection didGenerateIceCandidate:(RTCIceCandidate*)candidate {
    if (_connection != nullptr) {
        dispatch_async(dispatch_get_main_queue(), ^{
          if (_connection != nullptr) {
              _connection->onIceCandidate(candidate);
          }
        });
    }
}

- (void)peerConnection:(RTCPeerConnection*)peerConnection didOpenDataChannel:(RTCDataChannel*)dataChannel {
    if (_connection != nullptr) {
        dispatch_async(dispatch_get_main_queue(), ^{
          if (_connection != nullptr) {
              _connection->onDataChannel(dataChannel);
          }
        });
    }
}

- (void)peerConnectionShouldNegotiate:(RTCPeerConnection*)peerConnection {
    if (_connection != nullptr) {
        dispatch_async(dispatch_get_main_queue(), ^{
          if (_connection != nullptr) {
              _connection->onNegotiationNeeded();
          }
        });
    }
}

// Required delegate methods (not used for DataChannel-only connections)
- (void)peerConnection:(RTCPeerConnection*)peerConnection didAddStream:(RTCMediaStream*)stream {
}
- (void)peerConnection:(RTCPeerConnection*)peerConnection didRemoveStream:(RTCMediaStream*)stream {
}
- (void)peerConnection:(RTCPeerConnection*)peerConnection didRemoveIceCandidates:(NSArray<RTCIceCandidate*>*)candidates {
}

@end

// Factory implementation for Apple platforms
std::unique_ptr<RTCPeerConnection> RTCPeerConnection::make(const RTCConfiguration& config) {
    return std::make_unique<AppleRTCPeerConnection>(config);
}

#else  // !CELLS_HAS_WEBRTC

// Stub implementation when WebRTC.framework is not available
class StubRTCPeerConnection : public RTCPeerConnection {
public:
    explicit StubRTCPeerConnection(const RTCConfiguration& /*config*/) {}

    void createOffer(CreateSDPCallback callback) override {
        callback(false, SessionDescription(), "WebRTC not available - link WebRTC.framework");
    }

    void createAnswer(CreateSDPCallback callback) override {
        callback(false, SessionDescription(), "WebRTC not available - link WebRTC.framework");
    }

    void setLocalDescription(const SessionDescription& /*sdp*/, SetSDPCallback callback) override {
        callback(false, "WebRTC not available - link WebRTC.framework");
    }

    void setRemoteDescription(const SessionDescription& /*sdp*/, SetSDPCallback callback) override {
        callback(false, "WebRTC not available - link WebRTC.framework");
    }

    void addIceCandidate(const ICECandidate& /*candidate*/, SetSDPCallback callback) override {
        callback(false, "WebRTC not available - link WebRTC.framework");
    }

    std::unique_ptr<RTCDataChannel> createDataChannel(const std::string& /*label*/,
                                                      const DataChannelConfig& /*config*/) override {
        return nullptr;
    }

    void close() override {}

    [[nodiscard]] PeerConnectionState getConnectionState() const override { return PeerConnectionState::CLOSED; }

    [[nodiscard]] ICEConnectionState getICEConnectionState() const override { return ICEConnectionState::CLOSED; }

    [[nodiscard]] ICEGatheringState getICEGatheringState() const override { return ICEGatheringState::COMPLETE; }

    [[nodiscard]] SignalingState getSignalingState() const override { return SignalingState::CLOSED; }

    [[nodiscard]] const SessionDescription* getLocalDescription() const override { return nullptr; }

    [[nodiscard]] const SessionDescription* getRemoteDescription() const override { return nullptr; }
};

// Factory implementation (stub)
std::unique_ptr<RTCPeerConnection> RTCPeerConnection::make(const RTCConfiguration& config) {
    return std::make_unique<StubRTCPeerConnection>(config);
}

#endif  // CELLS_HAS_WEBRTC

}  // namespace cells::net

#endif  // __APPLE__
