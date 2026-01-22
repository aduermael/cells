# Networking & P2P Collaboration

## Implementation Status

**Current state (December 2024):** Core networking implemented in C++ with cross-platform support.

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
| Web (Emscripten) | ✅ Implemented | `core/net/web/*.cc` |
| Apple (iOS/macOS) | 📋 Planned | `core/net/apple/*.mm` (stubs) |

### JavaScript Layer (`apps/wasm/static/shared/`)

| Component | Status | Source Files |
|-----------|--------|--------------|
| C++ Sync Adapter | ✅ Implemented | `cpp-sync-adapter.js` (thin wrapper over C++) |
| Presence UI | ✅ Implemented | `presence.js` (renders remote cursors) |
| Collab UI | ✅ Implemented | `collab-ui.js` (status indicator) |

The P2P collaboration layer is fully functional. Sync logic lives in C++ (`core/net/`) for cross-platform reuse. JavaScript is now a thin wrapper that handles UI rendering only. Presence data (cursors, selections) is broadcast in real-time but intentionally ephemeral - it is never stored in files or the Workbook.

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

| Message Type | Purpose |
|--------------|---------|
| `operations` | Batch of CRDT ops |
| `syncRequest` | Request ops since HLC |
| `syncResponse` | Response with ops |
| `presence` | Cursor, selection, name |
| `hello` | Initial handshake |
| `ping/pong` | Keep-alive |

## Sync Protocol

```
Initial connection:
  A ── hello(hlc=100) ──► B
  A ◄─ hello(hlc=150) ─── B
  A ── syncRequest(100) ─► B    // "Send me ops since my clock"
  A ◄─ syncResponse([...]) ── B

Ongoing:
  A ── operations([op]) ──► B   // Real-time as user edits
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

// Create sync client with callbacks
auto sync = SyncClient::create();
sync->setDelegate(myDelegate);

// Connect to a room
sync->connect("wss://cells.example.com", "room-id", "my-peer-id");

// Check state
SyncState state = sync->getState();  // DISCONNECTED, CONNECTING, CONNECTED

// Push local operations
sync->pushOperations(operations);

// Disconnect
sync->disconnect();
```

### SyncClientDelegate

Implement to receive sync events:

```cpp
class MySyncDelegate : public SyncClientDelegate {
    void syncClientStateDidChange(SyncClient* client, SyncState state) override;
    void syncClientPeerDidJoin(SyncClient* client, const std::string& peerId) override;
    void syncClientPeerDidLeave(SyncClient* client, const std::string& peerId) override;
    void syncClientDidReceiveOperations(SyncClient* client,
                                         const std::vector<Operation>& ops) override;
    void syncClientPresenceDidChange(SyncClient* client,
                                      const std::string& peerId,
                                      const PresenceData& presence) override;
};
```

### PresenceManager

Manages cursor/selection presence:

```cpp
#include "core/net/include/Presence.h"

// Update local presence (called on cursor move)
PresenceData presence;
presence.cursor_cell = "abc123";
presence.selection_start = "abc123";
presence.selection_end = "def456";
presenceManager->updateLocalPresence(presence);

// Get remote presences
auto remotes = presenceManager->getRemotePresences();
for (const auto& [peerId, data] : remotes) {
    // Render remote cursor at data.cursor_cell
}
```

## Platform Libraries

| Platform | Library | Notes |
|----------|---------|-------|
| iOS/macOS | [stasel/WebRTC](https://github.com/stasel/WebRTC) | BSD-3 license, prebuilt XCFramework |
| Web | Native RTCPeerConnection | Via Emscripten bindings |
| All | `core/net/` | Cross-platform C++ abstraction |
