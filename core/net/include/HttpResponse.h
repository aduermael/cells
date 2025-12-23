// HTTP response container
// Holds status code, headers, and body from an HTTP request

#ifndef CELLS_NET_HTTP_RESPONSE_H
#define CELLS_NET_HTTP_RESPONSE_H

#include <cstdint>

#include <map>
#include <string>
#include <vector>

namespace cells::net {

// HTTP response from a completed request
class HttpResponse {
public:
    HttpResponse() = default;

    // Status code (200, 404, etc.)
    void setStatusCode(int code) { status_code_ = code; }
    [[nodiscard]] int getStatusCode() const { return status_code_; }

    // Check if response indicates success (2xx)
    [[nodiscard]] bool isSuccess() const { return status_code_ >= 200 && status_code_ < 300; }

    // Response headers (case-insensitive keys normalized to lowercase)
    void setHeader(const std::string& key, const std::string& value);
    [[nodiscard]] std::string getHeader(const std::string& key) const;
    [[nodiscard]] bool hasHeader(const std::string& key) const;
    [[nodiscard]] const std::map<std::string, std::string>& getHeaders() const { return headers_; }

    // Response body
    void appendBytes(const uint8_t* data, size_t length);
    void appendBytes(const std::vector<uint8_t>& data);
    [[nodiscard]] const std::vector<uint8_t>& getBytes() const { return body_; }
    [[nodiscard]] std::string getBodyAsString() const;

    // Content-Length from headers (or body size if not set)
    [[nodiscard]] size_t getContentLength() const;

    // Clear all data
    void clear();

private:
    int status_code_ = 0;
    std::map<std::string, std::string> headers_;
    std::vector<uint8_t> body_;
};

}  // namespace cells::net

#endif  // CELLS_NET_HTTP_RESPONSE_H
