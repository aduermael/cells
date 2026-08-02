// Linux/native HTTP request implementation using libcurl
// Uses curl multi interface for async operations with a dedicated thread

#if !defined(__EMSCRIPTEN__) && !defined(__APPLE__) && !defined(_WIN32)

#include <cstring>

#include <atomic>
#include <curl/curl.h>
#include <mutex>
#include <sstream>
#include <thread>

#include "core/net/include/HttpRequest.h"

namespace cells::net {

// Global curl initialization (called once)
static std::once_flag curl_init_flag;
static void initCurl() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

// Curl write callback for response body
static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* request = static_cast<HttpRequest*>(userdata);
    size_t total = size * nmemb;
    request->getResponse().appendBytes(reinterpret_cast<const uint8_t*>(ptr), total);
    return total;
}

// Curl write callback for streaming response
static size_t streamWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata);

// Curl header callback
static size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* request = static_cast<HttpRequest*>(userdata);
    size_t total = size * nitems;

    // Parse header line: "Key: Value\r\n"
    std::string line(buffer, total);

    // Skip status line (starts with HTTP/)
    if (line.rfind("HTTP/", 0) == 0) {
        return total;
    }

    // Find colon separator
    auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);

        // Trim leading whitespace from value
        auto start = value.find_first_not_of(" \t");
        if (start != std::string::npos) {
            value = value.substr(start);
        }

        // Trim trailing \r\n
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
            value.pop_back();
        }

        request->getResponse().setHeader(key, value);
    }

    return total;
}

class CurlHttpRequest : public HttpRequest {
public:
    CurlHttpRequest(HttpMethod method, std::string host, uint16_t port, std::string path,
                    bool secure)
        : HttpRequest(method, std::move(host), port, std::move(path), secure) {
        std::call_once(curl_init_flag, initCurl);
    }

    ~CurlHttpRequest() override {
        cancel();
        // Wait for thread to finish
        if (thread_.joinable()) {
            thread_.join();
        }
    }

protected:
    void _sendAsync() override { startRequest(false); }

    void _sendAsyncStreaming() override { startRequest(true); }

    void _cancel() override {
        cancelled_.store(true);
        // Thread will check cancelled_ and clean up
    }

private:
    std::thread thread_;
    std::atomic<bool> cancelled_{false};
    CURL* easy_ = nullptr;
    struct curl_slist* headers_list_ = nullptr;

    std::string buildUrl() const {
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
                // URL encode would be nice here, but for now simple concat
                url_stream << key << "=" << value;
            }
        }

        return url_stream.str();
    }

    void startRequest(bool streaming) {
        thread_ = std::thread([this, streaming]() { runRequest(streaming); });
    }

    void runRequest(bool streaming) {
        easy_ = curl_easy_init();
        if (easy_ == nullptr) {
            completeWithError("Failed to initialize curl");
            return;
        }

        // Set URL
        std::string url = buildUrl();
        curl_easy_setopt(easy_, CURLOPT_URL, url.c_str());

        // Set method
        switch (method_) {
            case HttpMethod::GET:
                // GET is default
                break;
            case HttpMethod::POST:
                curl_easy_setopt(easy_, CURLOPT_POST, 1L);
                break;
            case HttpMethod::PUT:
                curl_easy_setopt(easy_, CURLOPT_CUSTOMREQUEST, "PUT");
                break;
            case HttpMethod::DELETE:
                curl_easy_setopt(easy_, CURLOPT_CUSTOMREQUEST, "DELETE");
                break;
            case HttpMethod::PATCH:
                curl_easy_setopt(easy_, CURLOPT_CUSTOMREQUEST, "PATCH");
                break;
            case HttpMethod::HEAD:
                curl_easy_setopt(easy_, CURLOPT_NOBODY, 1L);
                break;
        }

        // Set timeout
        curl_easy_setopt(easy_, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms_));

        // Set headers
        for (const auto& [key, value] : headers_) {
            std::string header = key + ": " + value;
            headers_list_ = curl_slist_append(headers_list_, header.c_str());
        }
        if (headers_list_ != nullptr) {
            curl_easy_setopt(easy_, CURLOPT_HTTPHEADER, headers_list_);
        }

        // Set body
        if (!body_.empty()) {
            curl_easy_setopt(easy_, CURLOPT_POSTFIELDS, body_.data());
            curl_easy_setopt(easy_, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_.size()));
        }

        // Set callbacks
        curl_easy_setopt(easy_, CURLOPT_HEADERFUNCTION, headerCallback);
        curl_easy_setopt(easy_, CURLOPT_HEADERDATA, this);

        if (streaming) {
            curl_easy_setopt(easy_, CURLOPT_WRITEFUNCTION, streamWriteCallback);
        } else {
            curl_easy_setopt(easy_, CURLOPT_WRITEFUNCTION, writeCallback);
        }
        curl_easy_setopt(easy_, CURLOPT_WRITEDATA, this);

        // Follow redirects
        curl_easy_setopt(easy_, CURLOPT_FOLLOWLOCATION, 1L);

        // Enable verbose for debugging (disabled by default)
        // curl_easy_setopt(easy_, CURLOPT_VERBOSE, 1L);

        // Perform request
        CURLcode res = curl_easy_perform(easy_);

        // Check for cancellation
        if (cancelled_.load()) {
            cleanup();
            return;
        }

        if (res != CURLE_OK) {
            cleanup();
            completeWithError(curl_easy_strerror(res));
            return;
        }

        // Get response code
        long response_code = 0;
        curl_easy_getinfo(easy_, CURLINFO_RESPONSE_CODE, &response_code);
        response_.setStatusCode(static_cast<int>(response_code));

        cleanup();

        if (streaming) {
            onStreamEnd();
        } else {
            completeWithSuccess();
        }
    }

    void cleanup() {
        if (headers_list_ != nullptr) {
            curl_slist_free_all(headers_list_);
            headers_list_ = nullptr;
        }
        if (easy_ != nullptr) {
            curl_easy_cleanup(easy_);
            easy_ = nullptr;
        }
    }

public:
    // Called by stream callback
    void handleStreamData(const uint8_t* data, size_t len) { onStreamData(data, len); }
};

// Curl write callback for streaming response
static size_t streamWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* request = static_cast<CurlHttpRequest*>(userdata);
    size_t total = size * nmemb;
    request->handleStreamData(reinterpret_cast<const uint8_t*>(ptr), total);
    return total;
}

// Factory implementation for Linux/native platforms
std::unique_ptr<HttpRequest> HttpRequest::make(HttpMethod method, const std::string& host,
                                               uint16_t port, const std::string& path,
                                               bool secure) {
    return std::make_unique<CurlHttpRequest>(method, host, port, path, secure);
}

}  // namespace cells::net

#endif  // !__EMSCRIPTEN__ && !__APPLE__ && !_WIN32
