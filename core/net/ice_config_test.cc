// Tests for ICEConfig
// These tests verify ICE server configuration management

#include <gtest/gtest.h>

#include "core/net/include/ICEConfig.h"

namespace cells::net {
namespace {

TEST(ICEServerTest, DefaultConstruction) {
    ICEServer server;
    EXPECT_TRUE(server.urls.empty());
    EXPECT_TRUE(server.username.empty());
    EXPECT_TRUE(server.credential.empty());
    EXPECT_FALSE(server.isStun());
    EXPECT_FALSE(server.isTurn());
}

TEST(ICEServerTest, StunServer) {
    ICEServer server("stun:stun.example.com:3478");
    EXPECT_EQ(server.urls.size(), 1);
    EXPECT_EQ(server.urls[0], "stun:stun.example.com:3478");
    EXPECT_TRUE(server.isStun());
    EXPECT_FALSE(server.isTurn());
}

TEST(ICEServerTest, TurnServer) {
    ICEServer server("turn:turn.example.com:3478", "user", "pass");
    EXPECT_EQ(server.urls.size(), 1);
    EXPECT_EQ(server.urls[0], "turn:turn.example.com:3478");
    EXPECT_EQ(server.username, "user");
    EXPECT_EQ(server.credential, "pass");
    EXPECT_FALSE(server.isStun());
    EXPECT_TRUE(server.isTurn());
}

TEST(ICEServerTest, TurnsServer) {
    ICEServer server("turns:turn.example.com:443", "user", "pass");
    EXPECT_TRUE(server.isTurn());
    EXPECT_FALSE(server.isStun());
}

TEST(ICEServerTest, MultipleUrls) {
    std::vector<std::string> urls = {"stun:stun1.example.com:3478", "stun:stun2.example.com:3478"};
    ICEServer server(urls, "", "");
    EXPECT_EQ(server.urls.size(), 2);
    EXPECT_TRUE(server.isStun());
}

TEST(RTCConfigurationTest, DefaultConfig) {
    auto config = RTCConfiguration::defaultConfig();
    EXPECT_FALSE(config.ice_servers.empty());
    EXPECT_EQ(config.ice_transport_policy, ICETransportPolicy::ALL);
    EXPECT_EQ(config.ice_candidate_pool_size, 10);

    // Should have at least one STUN server
    bool has_stun = false;
    for (const auto& server : config.ice_servers) {
        if (server.isStun()) {
            has_stun = true;
            break;
        }
    }
    EXPECT_TRUE(has_stun);
}

TEST(RTCConfigurationTest, WithServers) {
    std::vector<ICEServer> servers = {ICEServer("stun:custom.example.com:3478")};
    auto config = RTCConfiguration::withServers(servers);
    EXPECT_EQ(config.ice_servers.size(), 1);
    EXPECT_EQ(config.ice_servers[0].urls[0], "stun:custom.example.com:3478");
}

TEST(RTCConfigurationTest, AddServers) {
    RTCConfiguration config;
    config.addStunServer("stun:stun.example.com:3478");
    config.addTurnServer("turn:turn.example.com:3478", "user", "pass");

    EXPECT_EQ(config.ice_servers.size(), 2);
    EXPECT_TRUE(config.ice_servers[0].isStun());
    EXPECT_TRUE(config.ice_servers[1].isTurn());
}

TEST(RTCConfigurationTest, SetRelayOnly) {
    RTCConfiguration config;
    EXPECT_EQ(config.ice_transport_policy, ICETransportPolicy::ALL);

    config.setRelayOnly(true);
    EXPECT_EQ(config.ice_transport_policy, ICETransportPolicy::RELAY);

    config.setRelayOnly(false);
    EXPECT_EQ(config.ice_transport_policy, ICETransportPolicy::ALL);
}

TEST(ICEConfigManagerTest, Singleton) {
    auto& manager1 = ICEConfigManager::instance();
    auto& manager2 = ICEConfigManager::instance();
    EXPECT_EQ(&manager1, &manager2);
}

TEST(ICEConfigManagerTest, DefaultConfiguration) {
    auto& manager = ICEConfigManager::instance();

    // Reset to defaults
    manager.clearTurnServers();
    manager.setRelayOnly(false);

    auto config = manager.getConfiguration();
    EXPECT_FALSE(config.ice_servers.empty());
    EXPECT_EQ(config.ice_transport_policy, ICETransportPolicy::ALL);
}

TEST(ICEConfigManagerTest, AddTurnServer) {
    auto& manager = ICEConfigManager::instance();

    // Reset
    manager.clearTurnServers();
    EXPECT_FALSE(manager.hasTurnServers());

    // Add TURN
    manager.addTurnServer("turn:turn.example.com:3478", "user", "pass");
    EXPECT_TRUE(manager.hasTurnServers());

    auto servers = manager.getIceServers();
    bool found_turn = false;
    for (const auto& server : servers) {
        if (server.isTurn()) {
            found_turn = true;
            EXPECT_EQ(server.username, "user");
            EXPECT_EQ(server.credential, "pass");
        }
    }
    EXPECT_TRUE(found_turn);

    // Clean up
    manager.clearTurnServers();
}

TEST(ICEConfigManagerTest, RelayOnlyMode) {
    auto& manager = ICEConfigManager::instance();

    // Setup: add a TURN server
    manager.clearTurnServers();
    manager.addTurnServer("turn:turn.example.com:3478", "user", "pass");
    manager.setRelayOnly(true);
    EXPECT_TRUE(manager.isRelayOnly());

    auto config = manager.getConfiguration();
    EXPECT_EQ(config.ice_transport_policy, ICETransportPolicy::RELAY);

    // In relay mode, should only have TURN servers
    for (const auto& server : config.ice_servers) {
        EXPECT_TRUE(server.isTurn());
    }

    // Clean up
    manager.setRelayOnly(false);
    manager.clearTurnServers();
}

TEST(ICEConfigManagerTest, IceCandidatePoolSize) {
    auto& manager = ICEConfigManager::instance();

    manager.setIceCandidatePoolSize(5);
    EXPECT_EQ(manager.getIceCandidatePoolSize(), 5);

    auto config = manager.getConfiguration();
    EXPECT_EQ(config.ice_candidate_pool_size, 5);

    // Reset
    manager.setIceCandidatePoolSize(10);
}

TEST(DefaultStunServersTest, HasServers) {
    const auto& servers = getDefaultStunServers();
    EXPECT_FALSE(servers.empty());

    // All should be STUN servers
    for (const auto& server : servers) {
        EXPECT_TRUE(server.isStun());
        EXPECT_FALSE(server.isTurn());
    }
}

}  // namespace
}  // namespace cells::net
