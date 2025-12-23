// URL parsing and construction utility
// Parses URLs into components and builds URL strings

#ifndef CELLS_NET_URL_H
#define CELLS_NET_URL_H

#include <cstdint>

#include <map>
#include <optional>
#include <string>

namespace cells::net {

// Parsed URL with scheme, host, port, path, and query components
class URL {
public:
    URL() = default;

    // Parse a URL string (e.g., "https://example.com:8080/path?key=value")
    // Returns nullopt if parsing fails
    static std::optional<URL> parse(const std::string& url_string);

    // Construct URL from components
    URL(std::string scheme, std::string host, uint16_t port, std::string path);

    // URL components
    [[nodiscard]] const std::string& getScheme() const { return scheme_; }
    [[nodiscard]] const std::string& getHost() const { return host_; }
    [[nodiscard]] uint16_t getPort() const { return port_; }
    [[nodiscard]] const std::string& getPath() const { return path_; }
    [[nodiscard]] const std::string& getQuery() const { return query_; }
    [[nodiscard]] const std::string& getFragment() const { return fragment_; }

    // Setters for building URLs
    void setScheme(const std::string& scheme) { scheme_ = scheme; }
    void setHost(const std::string& host) { host_ = host; }
    void setPort(uint16_t port) { port_ = port; }
    void setPath(const std::string& path) { path_ = path; }
    void setQuery(const std::string& query) { query_ = query; }
    void setFragment(const std::string& fragment) { fragment_ = fragment; }

    // Query parameter helpers
    void setQueryParam(const std::string& key, const std::string& value);
    [[nodiscard]] std::string getQueryParam(const std::string& key) const;
    [[nodiscard]] bool hasQueryParam(const std::string& key) const;

    // Derived properties
    [[nodiscard]] bool isSecure() const { return scheme_ == "https" || scheme_ == "wss"; }
    [[nodiscard]] uint16_t getEffectivePort() const;

    // Build full URL string
    [[nodiscard]] std::string toString() const;

    // Build path + query + fragment (for HTTP request line)
    [[nodiscard]] std::string getPathAndQuery() const;

private:
    std::string scheme_;    // "http", "https", "ws", "wss"
    std::string host_;      // "example.com"
    uint16_t port_ = 0;     // 0 means use default for scheme
    std::string path_;      // "/api/v1/data"
    std::string query_;     // "key=value&foo=bar" (without ?)
    std::string fragment_;  // "section" (without #)

    std::map<std::string, std::string> query_params_;

    void parseQueryParams();
    void buildQueryFromParams();
};

// URL encoding/decoding utilities
std::string urlEncode(const std::string& str);
std::string urlDecode(const std::string& str);

}  // namespace cells::net

#endif  // CELLS_NET_URL_H
