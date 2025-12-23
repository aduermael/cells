// Apple (iOS/macOS) WebSocket implementation
// Uses NSURLSessionWebSocketTask for WebSocket connections

#if defined(__APPLE__)

#import <Foundation/Foundation.h>

#include "core/net/include/WSConnection.h"

namespace cells::net {

class AppleWSConnection : public WSConnection {
public:
    AppleWSConnection(std::string host, uint16_t port, std::string path, bool secure)
        : WSConnection(std::move(host), port, std::move(path), secure) {
        _init();
    }

    ~AppleWSConnection() override { _destroy(); }

protected:
    void _init() override {
        // Create session configuration
        NSURLSessionConfiguration* config = [NSURLSessionConfiguration defaultSessionConfiguration];
        session_ = [NSURLSession sessionWithConfiguration:config];
    }

    void _connect() override {
        @autoreleasepool {
            NSString* urlString = [NSString stringWithUTF8String:url_string_.c_str()];
            NSURL* url = [NSURL URLWithString:urlString];

            if (url == nil) {
                onError("Invalid WebSocket URL");
                return;
            }

            // Create WebSocket task
            task_ = [session_ webSocketTaskWithURL:url];

            // Start the connection
            [task_ resume];

            // Monitor connection state by attempting to receive
            // NSURLSessionWebSocketTask doesn't have a direct onOpen callback,
            // so we start receiving immediately and treat first receive setup as connected
            AppleWSConnection* __weak weakSelf = this;

            // Send a ping to verify connection
            [task_ sendPingWithPongReceiveHandler:^(NSError* error) {
              dispatch_async(dispatch_get_main_queue(), ^{
                AppleWSConnection* strongSelf = weakSelf;
                if (strongSelf == nullptr) {
                    return;
                }
                if (error != nil) {
                    strongSelf->onError([[error localizedDescription] UTF8String]);
                } else {
                    strongSelf->onOpen();
                    strongSelf->startReceiving();
                }
              });
            }];
        }
    }

    void _close() override {
        if (task_ != nil) {
            [task_ cancelWithCloseCode:NSURLSessionWebSocketCloseCodeNormalClosure
                                reason:nil];
            task_ = nil;
        }
    }

    void _send(const Payload& payload) override {
        if (task_ == nil) {
            return;
        }

        @autoreleasepool {
            NSURLSessionWebSocketMessage* message;
            if (payload.isText()) {
                NSString* text = [NSString stringWithUTF8String:payload.asString().c_str()];
                message = [[NSURLSessionWebSocketMessage alloc] initWithString:text];
            } else {
                NSData* data = [NSData dataWithBytes:payload.data().data() length:payload.data().size()];
                message = [[NSURLSessionWebSocketMessage alloc] initWithData:data];
            }

            AppleWSConnection* __weak weakSelf = this;
            [task_ sendMessage:message
                completionHandler:^(NSError* error) {
                  if (error != nil) {
                      dispatch_async(dispatch_get_main_queue(), ^{
                        AppleWSConnection* strongSelf = weakSelf;
                        if (strongSelf != nullptr) {
                            strongSelf->onError([[error localizedDescription] UTF8String]);
                        }
                      });
                  }
                }];
        }
    }

    void _destroy() override {
        if (task_ != nil) {
            [task_ cancel];
            task_ = nil;
        }
        if (session_ != nil) {
            [session_ invalidateAndCancel];
            session_ = nil;
        }
    }

private:
    NSURLSession* session_ = nil;
    NSURLSessionWebSocketTask* task_ = nil;

    void startReceiving() {
        if (task_ == nil) {
            return;
        }

        AppleWSConnection* __weak weakSelf = this;
        [task_ receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage* message,
                                                     NSError* error) {
          dispatch_async(dispatch_get_main_queue(), ^{
            AppleWSConnection* strongSelf = weakSelf;
            if (strongSelf == nullptr) {
                return;
            }

            if (error != nil) {
                // Check if it's a normal close
                if (error.code == NSURLErrorCancelled) {
                    strongSelf->onClose(1000, "");
                } else {
                    strongSelf->onError([[error localizedDescription] UTF8String]);
                }
                return;
            }

            // Process message
            if (message != nil) {
                Payload payload;
                if (message.type == NSURLSessionWebSocketMessageTypeString) {
                    payload = Payload([message.string UTF8String]);
                } else {
                    NSData* data = message.data;
                    std::vector<uint8_t> bytes(static_cast<const uint8_t*>(data.bytes),
                                               static_cast<const uint8_t*>(data.bytes) + data.length);
                    payload = Payload(std::move(bytes));
                }
                strongSelf->onMessage(payload);
            }

            // Continue receiving
            strongSelf->startReceiving();
          });
        }];
    }
};

// Factory implementation for Apple platforms
std::unique_ptr<WSConnection> WSConnection::make(const std::string& scheme, const std::string& host,
                                                 uint16_t port, const std::string& path) {
    bool secure = (scheme == "wss" || scheme == "https");
    return std::make_unique<AppleWSConnection>(host, port, path, secure);
}

}  // namespace cells::net

#endif  // __APPLE__
