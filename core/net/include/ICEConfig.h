// ICE (Interactive Connectivity Establishment) configuration
// Manages STUN/TURN server configuration for WebRTC NAT traversal

#ifndef CELLS_NET_ICE_CONFIG_H
#define CELLS_NET_ICE_CONFIG_H

#include <cstdint>

#include <string>
#include <vector>

namespace cells::net {

// Single ICE server configuration (STUN or TURN)
struct ICEServer {
    std::vector<std::string> urls;  // Server URLs (stun: or turn:)
    std::string username;           // Username for TURN authentication
    std::string credential;         // Credential/password for TURN

    // Convenience constructors
    ICEServer() = default;

    explicit ICEServer(std::string url) : urls{std::move(url)} {}

    ICEServer(std::string url, std::string user, std::string cred)
        : urls{std::move(url)}, username(std::move(user)), credential(std::move(cred)) {}

    ICEServer(std::vector<std::string> server_urls, std::string user, std::string cred)
        : urls(std::move(server_urls)), username(std::move(user)), credential(std::move(cred)) {}

    // Check if this is a TURN server (requires credentials)
    [[nodiscard]] bool isTurn() const {
        for (const auto& url : urls) {
            if (url.rfind("turn:", 0) == 0 || url.rfind("turns:", 0) == 0) {
                return true;
            }
        }
        return false;
    }

    // Check if this is a STUN server
    [[nodiscard]] bool isStun() const {
        for (const auto& url : urls) {
            if (url.rfind("stun:", 0) == 0 || url.rfind("stuns:", 0) == 0) {
                return true;
            }
        }
        return false;
    }
};

// ICE transport policy
enum class ICETransportPolicy : std::uint8_t {
    ALL,   // Use all available candidate types (default)
    RELAY  // Only use relay candidates (TURN only)
};

// Bundle policy for media streams
enum class BundlePolicy : std::uint8_t {
    BALANCED,    // Gather candidates for each media type
    MAX_BUNDLE,  // Gather candidates for only one media type
    MAX_COMPAT   // Gather candidates for each track
};

// RTCP mux policy
enum class RTCPMuxPolicy : std::uint8_t {
    REQUIRE  // Require RTCP multiplexing (default)
};

// Complete RTC configuration for peer connections
struct RTCConfiguration {
    std::vector<ICEServer> ice_servers;
    ICETransportPolicy ice_transport_policy = ICETransportPolicy::ALL;
    BundlePolicy bundle_policy = BundlePolicy::BALANCED;
    RTCPMuxPolicy rtcp_mux_policy = RTCPMuxPolicy::REQUIRE;
    int ice_candidate_pool_size = 0;

    // Create default configuration with public STUN servers
    static RTCConfiguration defaultConfig();

    // Create configuration with custom ICE servers
    static RTCConfiguration withServers(std::vector<ICEServer> servers);

    // Add a STUN server
    void addStunServer(const std::string& url) { ice_servers.emplace_back(url); }

    // Add a TURN server with credentials
    void addTurnServer(const std::string& url, const std::string& username,
                       const std::string& credential) {
        ice_servers.emplace_back(url, username, credential);
    }

    // Set relay-only mode (TURN only)
    void setRelayOnly(bool relay_only) {
        ice_transport_policy = relay_only ? ICETransportPolicy::RELAY : ICETransportPolicy::ALL;
    }
};

// Global ICE configuration manager
// Provides centralized ICE server configuration like ice-config.js
class ICEConfigManager {
public:
    // Singleton access
    static ICEConfigManager& instance();

    // Get current configuration
    [[nodiscard]] RTCConfiguration getConfiguration() const;

    // Get ICE servers only
    [[nodiscard]] std::vector<ICEServer> getIceServers() const;

    // Add custom TURN server
    void addTurnServer(const std::string& url, const std::string& username = "",
                       const std::string& credential = "");

    // Set custom TURN servers (replaces existing)
    void setTurnServers(std::vector<ICEServer> servers);

    // Clear custom TURN servers
    void clearTurnServers();

    // Check if TURN is configured
    [[nodiscard]] bool hasTurnServers() const;

    // Enable/disable relay-only mode
    void setRelayOnly(bool relay_only);
    [[nodiscard]] bool isRelayOnly() const;

    // Set ICE candidate pool size
    void setIceCandidatePoolSize(int size);
    [[nodiscard]] int getIceCandidatePoolSize() const;

    // Non-copyable
    ICEConfigManager(const ICEConfigManager&) = delete;
    ICEConfigManager& operator=(const ICEConfigManager&) = delete;

private:
    ICEConfigManager();
    ~ICEConfigManager() = default;

    std::vector<ICEServer> stun_servers_;  // Default STUN servers
    std::vector<ICEServer> turn_servers_;  // Custom TURN servers
    bool relay_only_ = false;
    int ice_candidate_pool_size_ = 10;
};

// Default public STUN servers
const std::vector<ICEServer>& getDefaultStunServers();

}  // namespace cells::net

#endif  // CELLS_NET_ICE_CONFIG_H
