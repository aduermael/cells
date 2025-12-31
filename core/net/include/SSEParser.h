// Server-Sent Events (SSE) parser
// Parses SSE events from a streaming HTTP response

#ifndef CELLS_NET_SSE_PARSER_H
#define CELLS_NET_SSE_PARSER_H

#include <cstdint>

#include <functional>
#include <string>

namespace cells::net {

// Callback for complete SSE events
// Parameters: event type (may be empty), data (JSON or text)
using SSEEventCallback =
    std::function<void(const std::string& event_type, const std::string& data)>;

// Parser for Server-Sent Events stream
// Handles buffering and line parsing for incremental data
class SSEParser {
public:
    explicit SSEParser(SSEEventCallback callback);

    // Feed raw bytes from HTTP stream
    // May call callback zero or more times depending on complete events
    void feed(const uint8_t* data, size_t len);
    void feed(const std::string& data);

    // Reset parser state (for reuse)
    void reset();

private:
    // Process a complete line
    void processLine(const std::string& line);

    // Dispatch current event
    void dispatchEvent();

    SSEEventCallback callback_;
    std::string buffer_;      // Incomplete line buffer
    std::string event_type_;  // Current event type (from "event:" line)
    std::string event_data_;  // Current event data (from "data:" lines)
};

}  // namespace cells::net

#endif  // CELLS_NET_SSE_PARSER_H
