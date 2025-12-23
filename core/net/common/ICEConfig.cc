// ICEConfig implementation
// Centralized ICE server configuration management

#include "core/net/include/ICEConfig.h"

namespace cells::net {

// Default public STUN servers
static const std::vector<ICEServer> kDefaultStunServers = {
    ICEServer("stun:stun.l.google.com:19302"), ICEServer("stun:stun1.l.google.com:19302"),
    ICEServer("stun:stun2.l.google.com:19302"), ICEServer("stun:stun.cloudflare.com:3478")};

const std::vector<ICEServer>& getDefaultStunServers() {
    return kDefaultStunServers;
}

// RTCConfiguration implementation

RTCConfiguration RTCConfiguration::defaultConfig() {
    RTCConfiguration config;
    config.ice_servers = kDefaultStunServers;
    config.ice_candidate_pool_size = 10;
    return config;
}

RTCConfiguration RTCConfiguration::withServers(std::vector<ICEServer> servers) {
    RTCConfiguration config;
    config.ice_servers = std::move(servers);
    return config;
}

// ICEConfigManager implementation

ICEConfigManager& ICEConfigManager::instance() {
    static ICEConfigManager instance;
    return instance;
}

ICEConfigManager::ICEConfigManager() : stun_servers_(kDefaultStunServers) {}

RTCConfiguration ICEConfigManager::getConfiguration() const {
    RTCConfiguration config;

    if (relay_only_) {
        // Relay-only mode: only use TURN servers
        config.ice_servers = turn_servers_;
        config.ice_transport_policy = ICETransportPolicy::RELAY;
    } else {
        // Normal mode: combine STUN and TURN servers
        config.ice_servers = stun_servers_;
        config.ice_servers.insert(config.ice_servers.end(), turn_servers_.begin(),
                                  turn_servers_.end());
        config.ice_transport_policy = ICETransportPolicy::ALL;
    }

    config.ice_candidate_pool_size = ice_candidate_pool_size_;
    return config;
}

std::vector<ICEServer> ICEConfigManager::getIceServers() const {
    std::vector<ICEServer> servers;

    if (relay_only_) {
        servers = turn_servers_;
    } else {
        servers = stun_servers_;
        servers.insert(servers.end(), turn_servers_.begin(), turn_servers_.end());
    }

    return servers;
}

void ICEConfigManager::addTurnServer(const std::string& url, const std::string& username,
                                     const std::string& credential) {
    turn_servers_.emplace_back(url, username, credential);
}

void ICEConfigManager::setTurnServers(std::vector<ICEServer> servers) {
    turn_servers_ = std::move(servers);
}

void ICEConfigManager::clearTurnServers() {
    turn_servers_.clear();
}

bool ICEConfigManager::hasTurnServers() const {
    return !turn_servers_.empty();
}

void ICEConfigManager::setRelayOnly(bool relay_only) {
    relay_only_ = relay_only;
}

bool ICEConfigManager::isRelayOnly() const {
    return relay_only_;
}

void ICEConfigManager::setIceCandidatePoolSize(int size) {
    ice_candidate_pool_size_ = size;
}

int ICEConfigManager::getIceCandidatePoolSize() const {
    return ice_candidate_pool_size_;
}

}  // namespace cells::net
