// Web (Emscripten) HTTP request implementation
// Uses emscripten_fetch API for async HTTP requests

#ifdef __EMSCRIPTEN__

#include <emscripten/fetch.h>
#include <sstream>

#include "core/net/include/HttpRequest.h"

namespace cells::net {

class WebHttpRequest : public HttpRequest {
public:
    WebHttpRequest(HttpMethod method, std::string host, uint16_t port, std::string path,
                   bool secure)
        : HttpRequest(method, std::move(host), port, std::move(path), secure) {}

    ~WebHttpRequest() override {
        if (fetch_ != nullptr) {
            emscripten_fetch_close(fetch_);
            fetch_ = nullptr;
        }
    }

protected:
    void _sendAsync() override {
        emscripten_fetch_attr_t attr;
        emscripten_fetch_attr_init(&attr);

        // Set method
        const char* method_str = httpMethodToString(method_);
        strncpy(attr.requestMethod, method_str, sizeof(attr.requestMethod) - 1);
        attr.requestMethod[sizeof(attr.requestMethod) - 1] = '\0';

        // Configure callbacks
        attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_REPLACE;
        attr.onsuccess = &WebHttpRequest::onSuccess;
        attr.onerror = &WebHttpRequest::onError;
        attr.userData = this;

        // Set timeout
        attr.timeoutMSecs = timeout_ms_;

        // Set headers
        buildHeaderArray();
        if (!header_array_.empty()) {
            attr.requestHeaders = header_array_.data();
        }

        // Set body
        if (!body_.empty()) {
            attr.requestData = reinterpret_cast<const char*>(body_.data());
            attr.requestDataSize = body_.size();
        }

        // Build URL
        url_ = buildUrl();

        // Start fetch
        fetch_ = emscripten_fetch(&attr, url_.c_str());
    }

    void _sendAsyncStreaming() override {
        emscripten_fetch_attr_t attr;
        emscripten_fetch_attr_init(&attr);

        // Set method
        const char* method_str = httpMethodToString(method_);
        strncpy(attr.requestMethod, method_str, sizeof(attr.requestMethod) - 1);
        attr.requestMethod[sizeof(attr.requestMethod) - 1] = '\0';

        // Configure for streaming: load to memory AND stream chunks via onprogress
        // Both flags are needed: LOAD_TO_MEMORY to access data, STREAM_DATA for progress callbacks
        attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_STREAM_DATA;
        attr.onsuccess = &WebHttpRequest::onStreamingSuccess;
        attr.onerror = &WebHttpRequest::onError;
        attr.onprogress = &WebHttpRequest::onProgress;
        attr.userData = this;

        // Set timeout
        attr.timeoutMSecs = timeout_ms_;

        // Set headers
        buildHeaderArray();
        if (!header_array_.empty()) {
            attr.requestHeaders = header_array_.data();
        }

        // Set body
        if (!body_.empty()) {
            attr.requestData = reinterpret_cast<const char*>(body_.data());
            attr.requestDataSize = body_.size();
        }

        // Build URL
        url_ = buildUrl();

        printf("[HttpRequest] _sendAsyncStreaming: %s %s\n", method_str, url_.c_str());

        // Track bytes processed for incremental streaming
        last_data_offset_ = 0;

        // Start fetch
        fetch_ = emscripten_fetch(&attr, url_.c_str());
        printf("[HttpRequest] _sendAsyncStreaming: fetch started, fetch_=%p\n", (void*)fetch_);
    }

    void _cancel() override {
        if (fetch_ != nullptr) {
            emscripten_fetch_close(fetch_);
            fetch_ = nullptr;
        }
    }

private:
    emscripten_fetch_t* fetch_ = nullptr;
    std::string url_;
    std::vector<const char*> header_array_;
    std::vector<std::string> header_strings_;  // Keep strings alive
    size_t last_data_offset_ = 0;              // For incremental streaming

    void buildHeaderArray() {
        header_strings_.clear();
        header_array_.clear();

        for (const auto& [key, value] : headers_) {
            header_strings_.push_back(key);
            header_strings_.push_back(value);
        }

        for (const auto& str : header_strings_) {
            header_array_.push_back(str.c_str());
        }
        header_array_.push_back(nullptr);  // Null terminator
    }

    std::string buildUrl() {
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
        return url_stream.str();
    }

    static void onSuccess(emscripten_fetch_t* fetch) {
        auto* request = static_cast<WebHttpRequest*>(fetch->userData);
        if (request == nullptr) {
            emscripten_fetch_close(fetch);
            return;
        }

        // Set response status
        request->response_.setStatusCode(static_cast<int>(fetch->status));

        // Copy response body
        if (fetch->numBytes > 0 && fetch->data != nullptr) {
            request->response_.appendBytes(reinterpret_cast<const uint8_t*>(fetch->data),
                                           fetch->numBytes);
        }

        // Note: emscripten_fetch doesn't easily expose response headers
        // Would need to use EMSCRIPTEN_FETCH_PERSIST_FILE or custom JS

        request->fetch_ = nullptr;
        emscripten_fetch_close(fetch);
        request->completeWithSuccess();
    }

    static void onError(emscripten_fetch_t* fetch) {
        auto* request = static_cast<WebHttpRequest*>(fetch->userData);
        printf("[HttpRequest] onError: status=%d, statusText=%s\n",
               fetch->status, fetch->statusText);

        if (request == nullptr) {
            emscripten_fetch_close(fetch);
            return;
        }

        std::string error = "HTTP request failed";
        if (fetch->status != 0) {
            error += " with status " + std::to_string(fetch->status);
        }
        if (fetch->statusText[0] != '\0') {
            error += ": ";
            error += fetch->statusText;
        }

        printf("[HttpRequest] onError: %s\n", error.c_str());
        request->fetch_ = nullptr;
        emscripten_fetch_close(fetch);
        request->completeWithError(error);
    }

    // Called during streaming as chunks arrive
    static void onProgress(emscripten_fetch_t* fetch) {
        auto* request = static_cast<WebHttpRequest*>(fetch->userData);
        if (request == nullptr) {
            printf("[HttpRequest] onProgress: request is null\n");
            return;
        }

        printf("[HttpRequest] onProgress: status=%d, numBytes=%llu, lastOffset=%zu\n",
               fetch->status, fetch->numBytes, request->last_data_offset_);

        // Set response status on first progress callback
        if (request->response_.getStatusCode() == 0 && fetch->status != 0) {
            request->response_.setStatusCode(static_cast<int>(fetch->status));
        }

        // Forward new data since last callback
        if (fetch->data != nullptr && fetch->numBytes > request->last_data_offset_) {
            size_t new_bytes = fetch->numBytes - request->last_data_offset_;
            const auto* new_data =
                reinterpret_cast<const uint8_t*>(fetch->data) + request->last_data_offset_;
            printf("[HttpRequest] onProgress: forwarding %zu new bytes\n", new_bytes);
            request->onStreamData(new_data, new_bytes);
            request->last_data_offset_ = fetch->numBytes;
        }
    }

    // Called when streaming completes successfully
    static void onStreamingSuccess(emscripten_fetch_t* fetch) {
        auto* request = static_cast<WebHttpRequest*>(fetch->userData);
        printf("[HttpRequest] onStreamingSuccess: status=%d, numBytes=%llu\n",
               fetch->status, fetch->numBytes);

        if (request == nullptr) {
            printf("[HttpRequest] onStreamingSuccess: request is null\n");
            emscripten_fetch_close(fetch);
            return;
        }

        // Set final response status
        request->response_.setStatusCode(static_cast<int>(fetch->status));

        // Process any remaining data
        if (fetch->data != nullptr && fetch->numBytes > request->last_data_offset_) {
            size_t new_bytes = fetch->numBytes - request->last_data_offset_;
            const auto* new_data =
                reinterpret_cast<const uint8_t*>(fetch->data) + request->last_data_offset_;
            printf("[HttpRequest] onStreamingSuccess: processing remaining %zu bytes\n", new_bytes);
            request->onStreamData(new_data, new_bytes);
        }

        request->fetch_ = nullptr;
        emscripten_fetch_close(fetch);
        printf("[HttpRequest] onStreamingSuccess: calling onStreamEnd\n");
        request->onStreamEnd();
    }
};

// Factory implementation for web
std::unique_ptr<HttpRequest> HttpRequest::make(HttpMethod method, const std::string& host,
                                               uint16_t port, const std::string& path,
                                               bool secure) {
    return std::make_unique<WebHttpRequest>(method, host, port, path, secure);
}

}  // namespace cells::net

#endif  // __EMSCRIPTEN__
