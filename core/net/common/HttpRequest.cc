// Common HTTP request implementation
// Shared logic for all platforms; platform-specific code in web/ and apple/

#include "core/net/include/HttpRequest.h"

namespace cells::net {

HttpRequest::HttpRequest(HttpMethod method, std::string host, uint16_t port, std::string path,
                         bool secure)
    : method_(method),
      host_(std::move(host)),
      port_(port),
      path_(std::move(path)),
      secure_(secure) {
    // Set default headers
    headers_["User-Agent"] = "cells/1.0";
    headers_["Accept"] = "*/*";
}

std::unique_ptr<HttpRequest> HttpRequest::make(HttpMethod method, const URL& url) {
    return make(method, url.getHost(), url.getEffectivePort(), url.getPath(), url.isSecure());
}

void HttpRequest::setHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

void HttpRequest::setBody(const std::vector<uint8_t>& body) {
    body_ = body;
}

void HttpRequest::setBody(const std::string& body) {
    body_.assign(body.begin(), body.end());
}

void HttpRequest::setTimeout(uint32_t timeout_ms) {
    timeout_ms_ = timeout_ms;
}

void HttpRequest::setQueryParam(const std::string& key, const std::string& value) {
    query_params_[key] = value;
}

void HttpRequest::sendAsync() {
    if (status_ != HttpRequestStatus::WAITING) {
        return;  // Already sent or completed
    }
    status_ = HttpRequestStatus::PROCESSING;
    _sendAsync();
}

void HttpRequest::sendAsyncStreaming() {
    if (status_ != HttpRequestStatus::WAITING) {
        return;  // Already sent or completed
    }
    status_ = HttpRequestStatus::STREAMING;
    _sendAsyncStreaming();
}

void HttpRequest::sendSync() {
    if (status_ != HttpRequestStatus::WAITING) {
        return;
    }

    // For sync requests, we send async and busy-wait
    // This is not ideal but keeps the interface simple
    // Platform implementations can override for better sync support
    bool done = false;
    auto original_callback = callback_;

    callback_ = [&done, &original_callback](HttpRequest& req) {
        done = true;
        if (original_callback) {
            original_callback(req);
        }
    };

    sendAsync();

    // Busy wait (platform implementations should do better)
    while (!done) {
        // Platform-specific event loop pumping would go here
        // For now, just yield
    }

    callback_ = original_callback;
}

void HttpRequest::cancel() {
    if (status_ != HttpRequestStatus::PROCESSING) {
        return;
    }
    _cancel();
    status_ = HttpRequestStatus::CANCELLED;
    if (callback_) {
        callback_(*this);
    }
}

void HttpRequest::completeWithSuccess() {
    status_ = HttpRequestStatus::DONE;
    if (callback_) {
        callback_(*this);
    }
}

void HttpRequest::completeWithError(const std::string& error) {
    error_ = error;
    status_ = HttpRequestStatus::FAILED;
    if (callback_) {
        callback_(*this);
    }
}

void HttpRequest::onStreamData(const uint8_t* data, size_t len) {
    if (stream_callback_) {
        stream_callback_(data, len);
    }
}

void HttpRequest::onStreamEnd() {
    status_ = HttpRequestStatus::DONE;
    if (callback_) {
        callback_(*this);
    }
}

const char* httpMethodToString(HttpMethod method) {
    switch (method) {
        case HttpMethod::GET:
            return "GET";
        case HttpMethod::POST:
            return "POST";
        case HttpMethod::PUT:
            return "PUT";
        case HttpMethod::DELETE:
            return "DELETE";
        case HttpMethod::PATCH:
            return "PATCH";
        case HttpMethod::HEAD:
            return "HEAD";
    }
    return "GET";
}

}  // namespace cells::net
