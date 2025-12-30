// URL parsing and construction implementation

#include "core/net/include/URL.h"

#include <cctype>
#include <cstdlib>

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace cells::net {

URL::URL(std::string scheme, std::string host, uint16_t port, std::string path)
    : scheme_(std::move(scheme)), host_(std::move(host)), port_(port), path_(std::move(path)) {}

std::optional<URL> URL::parse(const std::string& url_string) {
    URL url;

    // Find scheme
    const size_t scheme_end = url_string.find("://");
    if (scheme_end == std::string::npos || scheme_end == 0) {
        return std::nullopt;  // No scheme or empty scheme
    }
    url.scheme_ = url_string.substr(0, scheme_end);
    std::transform(url.scheme_.begin(), url.scheme_.end(), url.scheme_.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Find host start
    const size_t host_start = scheme_end + 3;
    if (host_start >= url_string.size()) {
        return std::nullopt;
    }

    // Find path start (first / after host)
    const size_t path_start = url_string.find('/', host_start);

    // Find query start
    const size_t query_start = url_string.find('?', host_start);

    // Find fragment start
    const size_t fragment_start = url_string.find('#', host_start);

    // Determine where host:port ends
    const size_t host_end_candidates = std::min({path_start, query_start, fragment_start});
    const size_t host_end =
        (host_end_candidates == std::string::npos) ? url_string.size() : host_end_candidates;

    // Extract host:port
    const std::string host_port = url_string.substr(host_start, host_end - host_start);

    // Check for port
    const size_t colon = host_port.rfind(':');
    if (colon != std::string::npos && colon > 0) {
        // Check if this is IPv6 (contains multiple colons or brackets)
        const size_t bracket = host_port.find('[');
        if (bracket != std::string::npos) {
            // IPv6: [::1]:8080
            const size_t close_bracket = host_port.find(']');
            if (close_bracket != std::string::npos && colon > close_bracket) {
                url.host_ = host_port.substr(0, colon);
                const std::string port_str = host_port.substr(colon + 1);
                char* endptr = nullptr;
                const unsigned long port_val = strtoul(port_str.c_str(), &endptr, 10);
                if (endptr == port_str.c_str() || *endptr != '\0' || port_val > 65535) {
                    return std::nullopt;
                }
                url.port_ = static_cast<uint16_t>(port_val);
            } else {
                url.host_ = host_port;
            }
        } else {
            // Regular host:port
            url.host_ = host_port.substr(0, colon);
            const std::string port_str = host_port.substr(colon + 1);
            char* endptr = nullptr;
            const unsigned long port_val = strtoul(port_str.c_str(), &endptr, 10);
            if (endptr == port_str.c_str() || *endptr != '\0' || port_val > 65535) {
                return std::nullopt;
            }
            url.port_ = static_cast<uint16_t>(port_val);
        }
    } else {
        url.host_ = host_port;
    }

    if (url.host_.empty()) {
        return std::nullopt;
    }

    // Extract path
    if (path_start != std::string::npos && path_start < url_string.size()) {
        size_t path_end = std::min(query_start, fragment_start);
        if (path_end == std::string::npos) {
            path_end = url_string.size();
        }
        url.path_ = url_string.substr(path_start, path_end - path_start);
    } else {
        url.path_ = "/";
    }

    // Extract query
    if (query_start != std::string::npos && query_start < url_string.size()) {
        size_t query_end = fragment_start;
        if (query_end == std::string::npos) {
            query_end = url_string.size();
        }
        url.query_ = url_string.substr(query_start + 1, query_end - query_start - 1);
        url.parseQueryParams();
    }

    // Extract fragment
    if (fragment_start != std::string::npos && fragment_start + 1 < url_string.size()) {
        url.fragment_ = url_string.substr(fragment_start + 1);
    }

    return url;
}

uint16_t URL::getEffectivePort() const {
    if (port_ != 0) {
        return port_;
    }
    if (scheme_ == "https" || scheme_ == "wss") {
        return 443;
    }
    if (scheme_ == "http" || scheme_ == "ws") {
        return 80;
    }
    return 0;
}

std::string URL::toString() const {
    std::ostringstream oss;
    oss << scheme_ << "://" << host_;

    // Only include port if non-default
    const uint16_t default_port = (scheme_ == "https" || scheme_ == "wss") ? 443 : 80;
    if (port_ != 0 && port_ != default_port) {
        oss << ":" << port_;
    }

    oss << (path_.empty() ? "/" : path_);

    if (!query_.empty()) {
        oss << "?" << query_;
    }

    if (!fragment_.empty()) {
        oss << "#" << fragment_;
    }

    return oss.str();
}

std::string URL::getPathAndQuery() const {
    std::ostringstream oss;
    oss << (path_.empty() ? "/" : path_);

    if (!query_.empty()) {
        oss << "?" << query_;
    }

    return oss.str();
}

void URL::setQueryParam(const std::string& key, const std::string& value) {
    query_params_[key] = value;
    buildQueryFromParams();
}

std::string URL::getQueryParam(const std::string& key) const {
    const auto it = query_params_.find(key);
    if (it != query_params_.end()) {
        return it->second;
    }
    return "";
}

bool URL::hasQueryParam(const std::string& key) const {
    return query_params_.find(key) != query_params_.end();
}

void URL::parseQueryParams() {
    query_params_.clear();
    if (query_.empty()) {
        return;
    }

    std::istringstream stream(query_);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        const size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            const std::string key = urlDecode(pair.substr(0, eq));
            const std::string value = urlDecode(pair.substr(eq + 1));
            query_params_[key] = value;
        } else {
            query_params_[urlDecode(pair)] = "";
        }
    }
}

void URL::buildQueryFromParams() {
    std::ostringstream oss;
    bool first = true;
    for (const auto& [key, value] : query_params_) {
        if (!first) {
            oss << "&";
        }
        first = false;
        oss << urlEncode(key) << "=" << urlEncode(value);
    }
    query_ = oss.str();
}

std::string urlEncode(const std::string& str) {
    std::ostringstream oss;
    oss << std::hex << std::uppercase;
    for (const unsigned char c : str) {
        if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << c;
        } else {
            oss << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
    }
    return oss.str();
}

std::string urlDecode(const std::string& str) {
    std::string result;
    result.reserve(str.size());

    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            // Parse hex value without exceptions
            char hex[3] = {str[i + 1], str[i + 2], '\0'};
            char* endptr = nullptr;
            const long value = strtol(hex, &endptr, 16);
            if (endptr == hex + 2 && value >= 0 && value <= 255) {
                result += static_cast<char>(value);
                i += 2;
            } else {
                result += str[i];
            }
        } else if (str[i] == '+') {
            result += ' ';
        } else {
            result += str[i];
        }
    }

    return result;
}

}  // namespace cells::net
