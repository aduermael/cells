// Server-Sent Events (SSE) parser implementation
// Reference: https://html.spec.whatwg.org/multipage/server-sent-events.html

#include "core/net/include/SSEParser.h"

#include <cstring>

namespace cells::net {

SSEParser::SSEParser(SSEEventCallback callback) : callback_(std::move(callback)) {}

void SSEParser::feed(const uint8_t* data, size_t len) {
    feed(std::string(reinterpret_cast<const char*>(data), len));
}

void SSEParser::feed(const std::string& data) {
    // Append new data to buffer
    buffer_ += data;

    // Process complete lines
    size_t pos = 0;
    while (pos < buffer_.size()) {
        // Find line ending (LF or CRLF)
        const size_t line_end = buffer_.find('\n', pos);
        if (line_end == std::string::npos) {
            // No complete line yet, keep remaining in buffer
            buffer_ = buffer_.substr(pos);
            return;
        }

        // Extract line (strip CR if present)
        size_t line_len = line_end - pos;
        if (line_len > 0 && buffer_[line_end - 1] == '\r') {
            --line_len;
        }
        const std::string line = buffer_.substr(pos, line_len);
        pos = line_end + 1;

        processLine(line);
    }

    // All data processed
    buffer_.clear();
}

void SSEParser::reset() {
    buffer_.clear();
    event_type_.clear();
    event_data_.clear();
}

void SSEParser::processLine(const std::string& line) {
    // Empty line = dispatch event
    if (line.empty()) {
        dispatchEvent();
        return;
    }

    // Comment line (starts with ':')
    if (line[0] == ':') {
        return;
    }

    // Parse field name and value
    const size_t colon_pos = line.find(':');
    std::string field;
    std::string value;

    if (colon_pos == std::string::npos) {
        // Field with no value
        field = line;
    } else {
        field = line.substr(0, colon_pos);
        // Skip optional space after colon
        size_t value_start = colon_pos + 1;
        if (value_start < line.size() && line[value_start] == ' ') {
            ++value_start;
        }
        value = line.substr(value_start);
    }

    // Handle known fields
    if (field == "event") {
        event_type_ = value;
    } else if (field == "data") {
        // Append to data buffer (with newline if not first)
        if (!event_data_.empty()) {
            event_data_ += '\n';
        }
        event_data_ += value;
    }
    // id and retry fields are ignored for now
}

void SSEParser::dispatchEvent() {
    // Only dispatch if we have data
    if (event_data_.empty()) {
        return;
    }

    // Call callback
    if (callback_) {
        callback_(event_type_, event_data_);
    }

    // Reset event state (but not buffer)
    event_type_.clear();
    event_data_.clear();
}

}  // namespace cells::net
