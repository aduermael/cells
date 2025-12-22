# Networking & P2P Collaboration

## Implementation Status

**Current state (December 2024):** Core networking implemented.

| Component | Status | Source Files |
|-----------|--------|--------------|
| WebRTC DataChannel | ✅ Implemented | `apps/wasm/static/shared/webrtc-manager.js` |
| Signaling client | ✅ Implemented | `apps/wasm/static/shared/signaling-client.js` |
| STUN/TURN integration | ✅ Implemented | `apps/wasm/static/shared/ice-config.js` |
| Sync protocol | ✅ Implemented | `core/cells/sync_manager.h`, `apps/wasm/static/shared/collab-manager.js` |
| Presence/cursors | ✅ Implemented | `apps/wasm/static/shared/presence.js` (ephemeral, never persisted) |

The P2P collaboration layer is fully functional. Presence data (cursors, selections) is broadcast in real-time but intentionally ephemeral - it is never stored in files or the Workbook.

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

## Platform Libraries

| Platform | Library |
|----------|---------|
| iOS/macOS | WebRTC.framework, MultipeerConnectivity |
| Web | Native RTCPeerConnection |
| Signaling | Socket.IO, native WebSocket |
| Serialization | MessagePack, CBOR |
