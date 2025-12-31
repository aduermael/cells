// Apple (iOS/macOS) HTTP request implementation
// Uses NSURLSession for async HTTP requests

#if defined(__APPLE__)

#import <Foundation/Foundation.h>

#include <sstream>

#include "core/net/include/HttpRequest.h"

// Forward declaration for streaming delegate
namespace cells::net {
class AppleHttpRequest;
}

// Delegate for streaming responses
@interface StreamingSessionDelegate : NSObject <NSURLSessionDataDelegate>
@property(nonatomic, assign) cells::net::AppleHttpRequest* request;
@end

namespace cells::net {

class AppleHttpRequest : public HttpRequest {
public:
    AppleHttpRequest(HttpMethod method,
                     std::string host,
                     uint16_t port,
                     std::string path,
                     bool secure)
        : HttpRequest(method, std::move(host), port, std::move(path), secure) {}

    ~AppleHttpRequest() override { cancelTask(); }

protected:
    void _sendAsync() override {
        @autoreleasepool {
            // Build URL
            NSString* urlString = buildUrlString();
            NSURL* url = [NSURL URLWithString:urlString];

            if (url == nil) {
                completeWithError("Invalid URL");
                return;
            }

            // Create request
            NSMutableURLRequest* request =
                [NSMutableURLRequest requestWithURL:url];

            // Set method
            request.HTTPMethod =
                [NSString stringWithUTF8String:httpMethodToString(method_)];

            // Set timeout
            request.timeoutInterval = timeout_ms_ / 1000.0;

            // Set headers
            for (const auto& [key, value] : headers_) {
                [request setValue:[NSString stringWithUTF8String:value.c_str()]
                    forHTTPHeaderField:[NSString
                                           stringWithUTF8String:key.c_str()]];
            }

            // Set body
            if (!body_.empty()) {
                request.HTTPBody =
                    [NSData dataWithBytes:body_.data() length:body_.size()];
            }

            // Get shared session
            NSURLSession* session = [NSURLSession sharedSession];

            // Store reference to self for the completion handler
            // We use __block to allow modification in the block
            AppleHttpRequest* __block requestPtr = this;

            // Create data task
            task_ = [session
                dataTaskWithRequest:request
                  completionHandler:^(NSData* data, NSURLResponse* urlResponse,
                                      NSError* error) {
                    // Dispatch to main thread for thread safety
                    dispatch_async(dispatch_get_main_queue(), ^{
                      if (requestPtr == nullptr) {
                          return;
                      }

                      if (error != nil) {
                          std::string errorMsg =
                              [[error localizedDescription] UTF8String];
                          requestPtr->completeWithError(errorMsg);
                          return;
                      }

                      // Process response
                      NSHTTPURLResponse* httpResponse =
                          (NSHTTPURLResponse*)urlResponse;
                      requestPtr->response_.setStatusCode(
                          static_cast<int>(httpResponse.statusCode));

                      // Copy headers
                      NSDictionary* responseHeaders =
                          httpResponse.allHeaderFields;
                      for (NSString* key in responseHeaders) {
                          NSString* value = responseHeaders[key];
                          requestPtr->response_.setHeader(
                              [key UTF8String], [value UTF8String]);
                      }

                      // Copy body
                      if (data != nil && data.length > 0) {
                          requestPtr->response_.appendBytes(
                              static_cast<const uint8_t*>(data.bytes),
                              data.length);
                      }

                      requestPtr->task_ = nil;
                      requestPtr->completeWithSuccess();
                    });
                  }];

            // Start the task
            [task_ resume];
        }
    }

    void _sendAsyncStreaming() override {
        @autoreleasepool {
            NSString* urlString = buildUrlString();
            NSURL* url = [NSURL URLWithString:urlString];

            if (url == nil) {
                completeWithError("Invalid URL");
                return;
            }

            // Create request
            NSMutableURLRequest* request =
                [NSMutableURLRequest requestWithURL:url];

            // Set method
            request.HTTPMethod =
                [NSString stringWithUTF8String:httpMethodToString(method_)];

            // Set timeout
            request.timeoutInterval = timeout_ms_ / 1000.0;

            // Set headers
            for (const auto& [key, value] : headers_) {
                [request setValue:[NSString stringWithUTF8String:value.c_str()]
                    forHTTPHeaderField:[NSString
                                           stringWithUTF8String:key.c_str()]];
            }

            // Set body
            if (!body_.empty()) {
                request.HTTPBody =
                    [NSData dataWithBytes:body_.data() length:body_.size()];
            }

            // Create streaming delegate
            streamingDelegate_ = [[StreamingSessionDelegate alloc] init];
            streamingDelegate_.request = this;

            // Create session with delegate for streaming
            NSURLSessionConfiguration* config =
                [NSURLSessionConfiguration defaultSessionConfiguration];
            streamingSession_ = [NSURLSession sessionWithConfiguration:config
                                                              delegate:streamingDelegate_
                                                         delegateQueue:[NSOperationQueue mainQueue]];

            // Create data task (without completion handler - use delegate)
            task_ = [streamingSession_ dataTaskWithRequest:request];

            // Start the task
            [task_ resume];
        }
    }

    void _cancel() override { cancelTask(); }

    // Called by delegate when data is received
    void didReceiveData(NSData* data) {
        if (data != nil && data.length > 0) {
            onStreamData(static_cast<const uint8_t*>(data.bytes), data.length);
        }
    }

    // Called by delegate when response is received
    void didReceiveResponse(NSURLResponse* urlResponse) {
        NSHTTPURLResponse* httpResponse = (NSHTTPURLResponse*)urlResponse;
        response_.setStatusCode(static_cast<int>(httpResponse.statusCode));

        // Copy headers
        NSDictionary* responseHeaders = httpResponse.allHeaderFields;
        for (NSString* key in responseHeaders) {
            NSString* value = responseHeaders[key];
            response_.setHeader([key UTF8String], [value UTF8String]);
        }
    }

    // Called by delegate when task completes
    void didCompleteWithError(NSError* error) {
        cleanupStreaming();
        if (error != nil) {
            std::string errorMsg = [[error localizedDescription] UTF8String];
            completeWithError(errorMsg);
        } else {
            onStreamEnd();
        }
    }

private:
    NSURLSessionDataTask* task_ = nil;
    NSURLSession* streamingSession_ = nil;
    StreamingSessionDelegate* streamingDelegate_ = nil;

    NSString* buildUrlString() {
        std::ostringstream url_stream;
        url_stream << (secure_ ? "https" : "http") << "://" << host_;
        if ((secure_ && port_ != 443) || (!secure_ && port_ != 80)) {
            url_stream << ":" << port_;
        }
        url_stream << path_;

        // Add query parameters
        if (!query_params_.empty()) {
            url_stream << "?";
            bool first = true;
            for (const auto& [key, value] : query_params_) {
                if (!first) {
                    url_stream << "&";
                }
                first = false;
                url_stream << key << "=" << value;
            }
        }

        return [NSString stringWithUTF8String:url_stream.str().c_str()];
    }

    void cancelTask() {
        if (task_ != nil) {
            [task_ cancel];
            task_ = nil;
        }
        cleanupStreaming();
    }

    void cleanupStreaming() {
        if (streamingSession_ != nil) {
            [streamingSession_ invalidateAndCancel];
            streamingSession_ = nil;
        }
        if (streamingDelegate_ != nil) {
            streamingDelegate_.request = nullptr;
            streamingDelegate_ = nil;
        }
    }
};

// Factory implementation for Apple platforms
std::unique_ptr<HttpRequest> HttpRequest::make(HttpMethod method,
                                               const std::string& host,
                                               uint16_t port,
                                               const std::string& path,
                                               bool secure) {
    return std::make_unique<AppleHttpRequest>(method, host, port, path, secure);
}

}  // namespace cells::net

// Implementation of streaming delegate
@implementation StreamingSessionDelegate

- (void)URLSession:(NSURLSession*)session
              dataTask:(NSURLSessionDataTask*)dataTask
    didReceiveResponse:(NSURLResponse*)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completionHandler {
    if (_request != nullptr) {
        _request->didReceiveResponse(response);
    }
    completionHandler(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession*)session
          dataTask:(NSURLSessionDataTask*)dataTask
    didReceiveData:(NSData*)data {
    if (_request != nullptr) {
        _request->didReceiveData(data);
    }
}

- (void)URLSession:(NSURLSession*)session
                    task:(NSURLSessionTask*)task
    didCompleteWithError:(NSError*)error {
    if (_request != nullptr) {
        _request->didCompleteWithError(error);
    }
}

@end

#endif  // __APPLE__
