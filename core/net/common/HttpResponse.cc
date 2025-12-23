// HTTP response implementation

#include "core/net/include/HttpResponse.h"

#include <cctype>
#include <cstdlib>

#include <algorithm>

namespace cells::net {

namespace {
// Convert string to lowercase for case-insensitive header comparison
std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}
}  // namespace

void HttpResponse::setHeader(const std::string& key, const std::string& value) {
    headers_[toLower(key)] = value;
}

std::string HttpResponse::getHeader(const std::string& key) const {
    const auto it = headers_.find(toLower(key));
    if (it != headers_.end()) {
        return it->second;
    }
    return "";
}

bool HttpResponse::hasHeader(const std::string& key) const {
    return headers_.find(toLower(key)) != headers_.end();
}

void HttpResponse::appendBytes(const uint8_t* data, size_t length) {
    body_.insert(body_.end(), data, data + length);
}

void HttpResponse::appendBytes(const std::vector<uint8_t>& data) {
    body_.insert(body_.end(), data.begin(), data.end());
}

std::string HttpResponse::getBodyAsString() const {
    return {body_.begin(), body_.end()};
}

size_t HttpResponse::getContentLength() const {
    const std::string content_length = getHeader("content-length");
    if (!content_length.empty()) {
        // Try to parse as number, return body size on failure
        const char* const start = content_length.c_str();
        // NOLINTNEXTLINE(misc-const-correctness) - strtoull requires non-const
        char* end = nullptr;
        const unsigned long long value = std::strtoull(start, &end, 10);
        if (end != start && *end == '\0') {
            return static_cast<size_t>(value);
        }
    }
    return body_.size();
}

void HttpResponse::clear() {
    status_code_ = 0;
    headers_.clear();
    body_.clear();
}

}  // namespace cells::net
