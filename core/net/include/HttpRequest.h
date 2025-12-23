// HTTP request abstraction with platform-specific implementations
// Follows xptools pattern: common interface with platform hooks

#ifndef CELLS_NET_HTTP_REQUEST_H
#define CELLS_NET_HTTP_REQUEST_H

#include <cstdint>

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "core/net/include/HttpResponse.h"
#include "core/net/include/URL.h"

namespace cells::net {

// HTTP methods
enum class HttpMethod : std::uint8_t { GET, POST, PUT, DELETE, PATCH, HEAD };

// Request status
enum class HttpRequestStatus : std::uint8_t {
    WAITING,     // Created but not sent
    PROCESSING,  // Request in progress
    DONE,        // Completed successfully
    FAILED,      // Request failed (network error, timeout, etc.)
    CANCELLED    // Cancelled by user
};

// Forward declaration
class HttpRequest;

// Callback for async requests
using HttpRequestCallback = std::function<void(HttpRequest& request)>;

// HTTP request with async/sync execution
// Platform-specific implementations in web/HttpRequest_web.cc and
// apple/HttpRequest.mm
class HttpRequest {
public:
    virtual ~HttpRequest() = default;

    // Factory method - creates platform-specific implementation
    static std::unique_ptr<HttpRequest> make(HttpMethod method, const std::string& host,
                                             uint16_t port, const std::string& path,
                                             bool secure = true);

    // Convenience factory from URL
    static std::unique_ptr<HttpRequest> make(HttpMethod method, const URL& url);

    // Request configuration (call before send)
    void setHeader(const std::string& key, const std::string& value);
    void setBody(const std::vector<uint8_t>& body);
    void setBody(const std::string& body);
    void setTimeout(uint32_t timeout_ms);

    // Query parameters (appended to URL)
    void setQueryParam(const std::string& key, const std::string& value);

    // Callback for async completion
    void setCallback(HttpRequestCallback callback) { callback_ = std::move(callback); }

    // Execute request
    void sendAsync();  // Non-blocking, calls callback on completion
    void sendSync();   // Blocking, returns when complete
    void cancel();     // Cancel in-progress request

    // Status and response
    [[nodiscard]] HttpRequestStatus getStatus() const { return status_; }
    [[nodiscard]] const HttpResponse& getResponse() const { return response_; }
    [[nodiscard]] HttpResponse& getResponse() { return response_; }

    // Error information (if status == FAILED)
    [[nodiscard]] const std::string& getError() const { return error_; }

    // Request info
    [[nodiscard]] HttpMethod getMethod() const { return method_; }
    [[nodiscard]] const std::string& getHost() const { return host_; }
    [[nodiscard]] uint16_t getPort() const { return port_; }
    [[nodiscard]] const std::string& getPath() const { return path_; }
    [[nodiscard]] bool isSecure() const { return secure_; }

protected:
    HttpRequest(HttpMethod method, std::string host, uint16_t port, std::string path, bool secure);

    // Platform-specific hooks (implemented per platform)
    virtual void _sendAsync() = 0;
    virtual void _cancel() = 0;

    // Called by platform implementation when request completes
    void completeWithSuccess();
    void completeWithError(const std::string& error);

    // Request parameters
    HttpMethod method_;
    std::string host_;
    uint16_t port_;
    std::string path_;
    bool secure_;
    std::map<std::string, std::string> headers_;
    std::map<std::string, std::string> query_params_;
    std::vector<uint8_t> body_;
    uint32_t timeout_ms_ = 30000;  // 30 second default

    // Response
    HttpResponse response_;
    HttpRequestStatus status_ = HttpRequestStatus::WAITING;
    std::string error_;

    // Callback
    HttpRequestCallback callback_;
};

// Convert HttpMethod to string (for logging/debugging)
const char* httpMethodToString(HttpMethod method);

}  // namespace cells::net

#endif  // CELLS_NET_HTTP_REQUEST_H
