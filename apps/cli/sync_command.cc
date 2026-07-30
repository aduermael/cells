// Sync command implementation
// Joins a room and logs all operations received from peers

#include "sync_command.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "core/cells/id.h"
#include "core/cells/model.h"
#include "core/cells/operation.h"
#include "core/cells/parser.h"
#include "core/cells/serializer.h"
#include "core/cells/sync_manager.h"
#include "core/net/include/SyncClient.h"
#include "core/net/include/URL.h"

namespace cells::cli {

namespace {

// Global flag for graceful shutdown
std::atomic<bool> g_shutdown_requested{false};

// Signal handler for Ctrl+C
void signal_handler(int /*signal*/) { g_shutdown_requested = true; }

// Helper to read file contents
std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return "";
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// Helper to write file contents
bool write_file(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }
    file << content;
    return file.good();
}

// Helper to format operation payload for display
std::string formatPayload(const std::string& payload) {
    // If payload is short, show it inline
    if (payload.length() <= 40) {
        return payload;
    }
    // Truncate long payloads
    return payload.substr(0, 37) + "...";
}

// SyncClientDelegate that logs operations to stdout
class SyncLogger : public net::SyncClientDelegate {
public:
    explicit SyncLogger(const SyncOptions& opts) : opts_(opts) {}

    void syncClientStateDidChange(net::SyncClient& client, net::SyncClientState state) override {
        (void)client;
        if (!opts_.quiet && !opts_.ops_only) {
            std::cerr << statePrefix(state) << net::syncClientStateToString(state) << "\n";
        }
    }

    void syncClientPeerDidChange(net::SyncClient& client, const net::PeerInfo& peer) override {
        (void)client;
        if (!opts_.quiet && !opts_.ops_only) {
            std::cerr << "Peer " << peer.id;
            if (peer.is_connected) {
                std::cerr << " connected";
            }
            if (peer.is_synced) {
                std::cerr << " (synced)";
            }
            std::cerr << "\n";
        }
        peers_seen_++;
    }

    void syncClientPeerDidDisconnect(net::SyncClient& client,
                                     const std::string& peer_id) override {
        (void)client;
        if (!opts_.quiet && !opts_.ops_only) {
            std::cerr << "Peer left: " << peer_id << "\n";
        }
    }

    void syncClientDataDidChange(net::SyncClient& client) override {
        (void)client;
        // Data change notification (operations are logged separately)
    }

    void syncClientDidError(net::SyncClient& client, const std::string& error) override {
        (void)client;
        std::cerr << "Error: " << error << "\n";
    }

    void syncClientPresenceDidUpdate(net::SyncClient& client, const std::string& peer_id,
                                     const net::PresenceData& presence) override {
        (void)client;
        if (opts_.verbose && !opts_.ops_only) {
            std::cerr << "Presence update from " << peer_id << ": "
                      << (presence.name.empty() ? "(anonymous)" : presence.name) << "\n";
        }
    }

    void syncClientDidReceiveOperations(net::SyncClient& client, const std::string& peer_id,
                                        const std::vector<Operation>& operations) override {
        (void)client;
        ops_received_ += operations.size();

        // Format and print each operation
        for (const auto& op : operations) {
            std::cout << "\033[1m" << opTypeToString(op.type) << "\033[0m\n";
            std::cout << "  ├─ hlc: " << op.hlc.toString() << "\n";
            std::cout << "  ├─ target: " << op.target_id.toString() << "\n";
            std::cout << "  ├─ from: " << peer_id << "\n";
            if (!op.payload.empty()) {
                std::cout << "  └─ payload: " << formatPayload(op.payload) << "\n";
            } else {
                std::cout << "  └─ payload: (none)\n";
            }
        }
    }

    void printSummary() const {
        if (!opts_.ops_only) {
            std::cerr << "\nSummary:\n";
            std::cerr << "  Peers seen: " << peers_seen_ << "\n";
            std::cerr << "  Operations received: " << ops_received_ << "\n";
        }
    }

private:
    std::string statePrefix(net::SyncClientState state) const {
        switch (state) {
            case net::SyncClientState::CONNECTING: return "Connecting... ";
            case net::SyncClientState::SYNCING: return "Syncing... ";
            case net::SyncClientState::ONLINE: return "Online: ";
            case net::SyncClientState::RECONNECTING: return "Reconnecting... ";
            case net::SyncClientState::OFFLINE: return "Offline: ";
        }
        return "";
    }

    const SyncOptions& opts_;
    int peers_seen_{0};
    uint64_t ops_received_{0};
};

}  // namespace

int run_sync_command(const SyncOptions& opts) {
    // Parse the URL
    auto url_opt = net::URL::parse(opts.url);
    if (!url_opt.has_value()) {
        std::cerr << "Error: Invalid URL: " << opts.url << "\n";
        return 1;
    }

    const net::URL& url = url_opt.value();

    // Extract room ID from query string
    std::string room_id = url.getQueryParam("room");
    if (room_id.empty()) {
        // Try path as room ID
        std::string path = url.getPath();
        if (path.length() > 1 && path[0] == '/') {
            room_id = path.substr(1);
        }
    }

    if (room_id.empty()) {
        std::cerr << "Error: No room ID found in URL. Use ?room=<id> or /<room-id>\n";
        return 1;
    }

    // Construct signaling WebSocket URL
    std::string ws_scheme = url.isSecure() ? "wss" : "ws";
    net::URL signaling_url(ws_scheme, url.getHost(), url.getEffectivePort(), "/ws");

    if (!opts.quiet) {
        std::cerr << "Connecting to " << signaling_url.toString() << "...\n";
        std::cerr << "Room: " << room_id << "\n";
    }

    // Create or load workbook
    std::unique_ptr<Workbook> workbook;

    if (!opts.workbook_file.empty()) {
        std::string content = read_file(opts.workbook_file);
        if (content.empty()) {
            // --apply: create empty workbook if file is missing so agents can
            // "join, sync, save on exit" without a prior create step.
            // --send still requires an existing file to broadcast.
            if (opts.apply && !opts.send) {
                // Empty workbook, no Sheet1 — prepareWorkbookForSync (via
                // startSync) publishes nothing; remote state is pulled, or a
                // default sheet is minted only if truly alone later.
                workbook = std::make_unique<Workbook>();
                if (!opts.quiet) {
                    std::cerr << "Created empty workbook for --apply: " << opts.workbook_file
                              << "\n";
                }
            } else {
                std::cerr << "Error: Could not read file: " << opts.workbook_file << "\n";
                return 1;
            }
        } else {
            ParseResult result = parse(content);
            if (!result.ok()) {
                std::cerr << "Error: " << result.error->toString() << "\n";
                return 1;
            }
            workbook = std::move(result.workbook);

            if (!opts.quiet) {
                std::cerr << "Loaded workbook: " << opts.workbook_file << "\n";
            }
        }
    } else {
        // Empty workbook for join: no local Sheet1 (shared join policy).
        workbook = std::make_unique<Workbook>();
    }

    // Create sync client
    net::SyncClientConfig config;
    config.signaling_url = signaling_url.toString();

    net::SyncClient sync_client(workbook.get(), config);

    // Set up logging delegate
    SyncLogger logger(opts);
    sync_client.setDelegate(&logger);

    // Install signal handler for graceful shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Start sync
    sync_client.startSync(room_id);

    // If --send, broadcast current workbook state
    if (opts.send) {
        // Queue operations for all cells
        sync_client.broadcastOperations();
        if (!opts.quiet) {
            std::cerr << "Broadcasting workbook cells...\n";
        }
    }

    // Main loop - process messages and wait for shutdown
    if (!opts.quiet) {
        std::cerr << "\nListening for operations... (Ctrl+C to exit)\n";
    }

    // Set a name for presence
    sync_client.setLocalName("CLI User");

    while (!g_shutdown_requested) {
#if defined(__APPLE__)
        // Pump the main run loop to process incoming WebSocket/WebRTC callbacks
        // The Apple networking implementations dispatch callbacks to the main queue
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
#else
        // Sleep briefly to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
#endif

        // Process outgoing messages (sync operations)
        sync_client.processOutgoing();

        // Process presence updates (cursor/selection info)
        sync_client.processPresenceUpdates();
    }

    // Graceful shutdown
    if (!opts.quiet) {
        std::cerr << "\nShutting down...\n";
    }

    sync_client.stopSync();

    // If --apply, save the workbook
    if (opts.apply && !opts.workbook_file.empty()) {
        std::string content = serialize(*workbook);
        if (!write_file(opts.workbook_file, content)) {
            std::cerr << "Error: Could not write file: " << opts.workbook_file << "\n";
            return 1;
        }
        if (!opts.quiet) {
            std::cerr << "Saved workbook: " << opts.workbook_file << "\n";
        }
    }

    // Print summary
    logger.printSummary();

    return 0;
}

}  // namespace cells::cli
