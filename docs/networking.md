# Networking & P2P Collaboration

## Implementation Status

**Current state:** Core networking implemented in C++ with cross-platform support.

### C++ Core Layer (`core/net/`)

| Component | Status | Source Files |
|-----------|--------|--------------|
| HTTP Abstraction | ✅ Implemented | `core/net/include/HttpRequest.h`, `core/net/common/HttpRequest.cc` |
| WebSocket Abstraction | ✅ Implemented | `core/net/include/WSConnection.h`, `core/net/common/WSConnection.cc` |
| WebRTC Abstraction | ✅ Implemented | `core/net/include/RTCPeerConnection.h`, `core/net/include/RTCDataChannel.h` |
| Signaling Client | ✅ Implemented | `core/net/include/SignalingClient.h`, `core/net/common/SignalingClient.cc` |
| Sync Client | ✅ Implemented | `core/net/include/SyncClient.h`, `core/net/common/SyncClient.cc` |
| Presence Manager | ✅ Implemented | `core/net/include/Presence.h`, `core/net/common/Presence.cc` |

### Platform Implementations

| Platform | Status | Source Files |
|----------|--------|--------------|
| Web (Emscripten) | ✅ Implemented | `core/net/web/` (HttpRequest, WebSocket, WebRTC bindings) |
| Apple (iOS/macOS) | ✅ Implemented | `core/net/apple/` (HttpRequest, WebSocket; WebRTC sources exist, build uses libdatachannel) |
| Linux | ✅ Implemented | `core/net/native/` (HTTP via libcurl; WS/RTC via libdatachannel) |
| Windows | ✅ HTTP/WS | `core/net/windows/` (HTTP + WebSocket via WinHTTP OS stack; WebRTC not yet) |

### TypeScript Layer (`apps/wasm/src/`)

| Component | Status | Source Files |
|-----------|--------|--------------|
| C++ Sync Adapter | ✅ Implemented | `cpp-sync-adapter.ts` (thin wrapper over C++) |
| Presence Broadcast | ✅ Implemented | `presence-broadcast.ts` (sends cursor/selection updates) |
| Presence UI | ✅ Implemented | `presence.ts`, `grid-presence-renderer.ts` (renders remote cursors) |
| Collab UI | ✅ Implemented | `collab-ui.ts` (connection status indicator) |
| RTC Proxy | ✅ Implemented | `rtc-proxy.ts` (WebRTC JS interop) |

The P2P collaboration layer is fully functional. Sync logic lives in C++ (`core/net/`) for cross-platform reuse. TypeScript handles UI rendering only. Presence data (cursors, selections) is broadcast in real-time but intentionally ephemeral - it is never stored in files or the Workbook.

---

## Overview

Cells uses **WebRTC for peer-to-peer collaboration**, combined with CRDTs for conflict-free merging. This avoids the need for always-on relay servers.

```
┌─────────────┐                              ┌─────────────┐
│  Client A   │◄────── WebRTC DataChannel ──►│  Client B   │
│  (iOS)      │         (encrypted P2P)      │  (macOS)    │
└─────────────┘                              └─────────────┘
       │                                            │
       │         ┌─────────────────────┐           │
       └────────►│   Signaling Server  │◄──────────┘
                 │   (connection setup │
                 │    only - stateless)│
                 └─────────────────────┘
```

## Why P2P + WebRTC?

| Aspect | Server-Relayed | P2P (WebRTC) |
|--------|---------------|--------------|
| Latency | 50-200ms | 10-50ms (direct) |
| Server costs | High | Low (signaling only) |
| Offline | Limited | Full (CRDT sync later) |
| Privacy | Data on server | Direct between peers |
| Scaling | Server bottleneck | Scales with peers |

## WebRTC Components

1. **DataChannel**: Binary/text messaging for CRDT operations
2. **ICE**: Finds the best path between peers (direct, STUN, or TURN relay)
3. **DTLS**: Automatic end-to-end encryption

### Connection Types

- **Direct** (~70%): Local network or public IP
- **STUN-assisted** (~20%): NAT traversal via hole-punching
- **TURN relay** (~10%): Fallback when P2P impossible

## Infrastructure Requirements

### 1. Signaling Server (Required)

Exchanges connection metadata (SDP offers/answers, ICE candidates). Does not see document data.

Options: Self-hosted (Node.js/Go), Serverless (Cloudflare Workers), Firebase, PeerJS Cloud

### 2. STUN Server (Required)

Helps peers discover their public IP/port. Use free public servers:
- `stun:stun.l.google.com:19302`
- `stun:stun.cloudflare.com:3478`

### 3. TURN Server (Recommended)

Relay fallback for enterprise networks. Options: Twilio, Cloudflare Calls, self-hosted coturn, Xirsys

## Connection Flow

```
Client A              Signaling              Client B
    │                    │                       │
    │── join(room) ─────►│                       │
    │                    │── peer-joined(A) ────►│
    │                    │◄─── offer(SDP) ───────│
    │◄── offer(SDP) ─────│                       │
    │── answer(SDP) ────►│── answer(SDP) ───────►│
    │◄════════ ICE candidates (trickled) ═══════►│
    │                    │                       │
    │◄═══════════ P2P DataChannel ══════════════►│
```

## Network Topology

### Mesh (2-6 peers)
Full mesh: every peer connects to every other peer.
- Connections: n(n-1)/2
- 4 peers = 6 connections

### Star (6-20 peers)
One peer acts as hub, relays to others.
- Connections: n-1

### Hybrid (Recommended)
Auto-select based on peer count.

## Message Protocol

See `docs/sync-protocol.md` for the complete wire format specification.

| Message Type | Channel | Purpose |
|--------------|---------|---------|
| `hello` | operations | Initial handshake, exchange HLC and op count |
| `sync-request` | operations | Request ops since HLC |
| `sync-response` | operations | Response with requested ops |
| `operations` | operations | Batch of CRDT ops (real-time broadcast) |
| `ack` | operations | Acknowledge received ops |
| `ping/pong` | operations | Latency measurement |
| `presence` | presence | Cursor, selection, editing state |

## Sync Protocol

```
Initial connection:
  A ── hello(hlc=100, op_count=5) ──► B
  A ◄─ hello(hlc=150, op_count=8) ─── B
  A ── sync-request(since=100) ──────► B    // "Send me ops since my clock"
  A ◄─ sync-response([...]) ────────── B

Ongoing:
  A ── operations(batch=[op]) ──────► B   // Real-time as user edits
  A ◄─ ack(hlc=...) ────────────────── B   // Confirm receipt
```

## Offline & Reconnection

CRDT enables full offline editing:
1. Apply edits to local state immediately
2. Queue operations for sync when back online
3. On reconnect: send pending ops, request missed ops

Reconnection uses exponential backoff: 1s, 2s, 4s, 8s... max 30s.

## Sharing & Discovery

| Method | Use Case |
|--------|----------|
| Link-based | `cells://doc/abc123` - anyone with link can join |
| QR Code | Local sharing, no signaling needed |
| Nearby (Bonjour) | Automatic local network discovery |

## Access Control

| Level | Permissions |
|-------|-------------|
| Owner | Full control |
| Editor | Can edit |
| Commenter | Can comment only |
| Viewer | Read-only |

## Security

- WebRTC provides DTLS encryption by default
- Optional application-level encryption (AES-GCM)
- Operation validation: HLC bounds, rate limiting
- Access control enforced on send and receive

## Cost Estimates

### Self-Hosted
| Component | Cost |
|-----------|------|
| Signaling | ~$5/mo (small VPS) |
| STUN | $0 (public servers) |
| TURN | ~$20/mo (coturn VPS) |

### Managed
- Twilio TURN: ~$0.40/GB (fallback traffic only)
- Cloudflare: ~$5/mo

## C++ API

### SyncClient

The main entry point for sync functionality:

```cpp
#include "core/net/include/SyncClient.h"

// Create sync client for a workbook
SyncClientConfig config;
config.signaling_url = "wss://cells.example.com/ws";
auto sync = std::make_unique<SyncClient>(workbook, config);
sync->setDelegate(myDelegate);

// Start sync (joins room, connects to peers)
sync->startSync("room-id", "my-peer-id");  // peer_id auto-generated if empty

// Check state
SyncClientState state = sync->getState();  // OFFLINE, CONNECTING, SYNCING, ONLINE, RECONNECTING

// Broadcast local operations after editing
sync->broadcastOperations();

// Process outgoing messages (call periodically)
sync->processOutgoing();

// Stop sync and disconnect
sync->stopSync();
```

### SyncClientDelegate

Implement to receive sync events:

```cpp
class MySyncDelegate : public SyncClientDelegate {
    void syncClientStateDidChange(SyncClient& client, SyncClientState state) override;
    void syncClientPeerDidChange(SyncClient& client, const PeerInfo& peer) override;
    void syncClientPeerDidDisconnect(SyncClient& client, const std::string& peer_id) override;
    void syncClientDataDidChange(SyncClient& client) override;  // Trigger UI refresh
    void syncClientDidError(SyncClient& client, const std::string& error) override;
    void syncClientLatencyDidUpdate(SyncClient& client, const std::string& peer_id,
                                    int latency_ms) override;
    void syncClientPresenceDidUpdate(SyncClient& client, const std::string& peer_id,
                                     const PresenceData& presence) override;
    void syncClientPresenceDidRemove(SyncClient& client, const std::string& peer_id) override;
};
```

### PresenceManager

Manages cursor/selection presence:

```cpp
#include "core/net/include/Presence.h"

// Get presence manager from sync client
PresenceManager* presence = syncClient->getPresenceManager();

// Update local presence (called on cursor/selection changes)
presence->setCurrentSheet("sheet123");
presence->setCursor(0, 0);  // col, row (zero-based)
presence->setSelection({0, 0}, {5, 10});  // start, end
presence->setMousePosition(150.5, 200.0);  // x, y
presence->setEditing(0, 0, "=SUM(");  // col, row, text

// Clear presence states
presence->clearCursor();
presence->clearSelection();
presence->clearMousePosition();
presence->clearEditing();

// Get remote presences
auto remotes = presence->getRemotePeers();
for (const auto& [peerId, data] : remotes) {
    if (data.has_cursor) {
        // Render remote cursor at (data.cursor.col, data.cursor.row)
    }
}

// Get peers on a specific sheet
auto peersOnSheet = presence->getPeersOnSheet("sheet123");
```

## Platform Libraries

| Platform | Library | Notes |
|----------|---------|-------|
| iOS/macOS HTTP/WS | Foundation (NSURLSession) | OS stack in `core/net/apple/` |
| Windows HTTP/WS | WinHTTP | OS stack in `core/net/windows/` (Windows 8+ for WebSocket) |
| Web | Browser APIs | Via Emscripten bindings (`core/net/web/`) |
| Linux HTTP | libcurl | `core/net/native/HttpRequest_curl.cc` |
| Native WebRTC / Linux WS | [libdatachannel](https://github.com/paullouisageneau/libdatachannel) | `core/net/native/` (Windows WebRTC still TODO) |
| All | `core/net/` | Cross-platform C++ abstraction layer |
