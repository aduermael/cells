// Default HTTP request implementation (stub for non-web, non-Apple platforms)
// This is a placeholder that always fails - real implementation needed for
// Linux/Windows

#if !defined(__EMSCRIPTEN__) && !defined(__APPLE__)

#include "core/net/include/HttpRequest.h"

namespace cells::net {

class DefaultHttpRequest : public HttpRequest {
public:
    DefaultHttpRequest(HttpMethod method, std::string host, uint16_t port, std::string path,
                       bool secure)
        : HttpRequest(method, std::move(host), port, std::move(path), secure) {}

protected:
    void _sendAsync() override {
        // No network implementation for this platform
        completeWithError("HTTP not implemented for this platform");
    }

    void _sendAsyncStreaming() override {
        // No network implementation for this platform
        completeWithError("HTTP streaming not implemented for this platform");
    }

    void _cancel() override {
        // Nothing to cancel
    }
};

// Factory implementation for default platforms
std::unique_ptr<HttpRequest> HttpRequest::make(HttpMethod method, const std::string& host,
                                               uint16_t port, const std::string& path,
                                               bool secure) {
    return std::make_unique<DefaultHttpRequest>(method, host, port, path, secure);
}

}  // namespace cells::net

#endif  // !__EMSCRIPTEN__ && !__APPLE__
