# Networking & P2P Collaboration

## Overview

Cells uses **WebRTC for peer-to-peer collaboration**, combined with CRDTs for conflict-free merging. This is the modern approach to real-time collaboration, avoiding the need for always-on relay servers.

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

### vs Traditional Server-Relayed (Google Sheets)

| Aspect | Server-Relayed | P2P (WebRTC) |
|--------|---------------|--------------|
| Latency | 50-200ms (round-trip) | 10-50ms (direct) |
| Server costs | High (relays all data) | Low (signaling only) |
| Offline | Limited | Full (CRDT sync later) |
| Privacy | Data on server | Direct between peers |
| Scaling | Server bottleneck | Scales with peers |
| Complexity | Simple | More connection logic |

### Modern Apps Using This Approach

- **Figma**: CRDT-based, P2P for cursors
- **Excalidraw**: WebRTC + Yjs CRDT
- **Liveblocks**: P2P-capable CRDT
- **PeerJS apps**: Full P2P
- **WebTorrent**: P2P file sharing

## WebRTC Architecture

### Components

```
┌─────────────────────────────────────────────────────────────────┐
│                         WebRTC Stack                             │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐  │
│  │  DataChannel    │  │  ICE Agent      │  │  DTLS/SRTP      │  │
│  │  (our data)     │  │  (connectivity) │  │  (encryption)   │  │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

1. **DataChannel**: Binary/text messaging (we use this for CRDT ops)
2. **ICE**: Finds the best path between peers (direct, STUN, or TURN relay)
3. **DTLS**: Automatic end-to-end encryption

### Connection Types (ICE)

```
Direct Connection (~70% of cases):
  Peer A ◄──────────────────────► Peer B
          Local network / Public IP

STUN-assisted (~20% of cases):
  Peer A ◄──────────────────────► Peer B
          NAT traversal via STUN hole-punching

TURN relay (~10% of cases, fallback):
  Peer A ◄────► TURN Server ◄────► Peer B
          When P2P impossible (symmetric NAT)
```

## Infrastructure Requirements

### 1. Signaling Server (Required)

Exchanges connection metadata (SDP offers/answers, ICE candidates). **Does not see any document data.**

```
Options:
├── Self-hosted (Node.js/Go) - ~$5/month VPS
├── Serverless (Cloudflare Workers, AWS Lambda)
├── Firebase Realtime Database (free tier works)
└── PeerJS Cloud (free, rate-limited)
```

**Minimal Signaling Server** (Node.js example):

```javascript
// signaling-server.js
const WebSocket = require('ws');
const wss = new WebSocket.Server({ port: 8080 });

const rooms = new Map();  // room_id -> Set<WebSocket>

wss.on('connection', (ws) => {
  ws.on('message', (data) => {
    const msg = JSON.parse(data);

    switch (msg.type) {
      case 'join':
        // Join a document room
        if (!rooms.has(msg.room)) rooms.set(msg.room, new Set());
        rooms.get(msg.room).add(ws);
        ws.room = msg.room;
        ws.peerId = msg.peerId;

        // Notify others in room
        broadcast(msg.room, ws, {
          type: 'peer-joined',
          peerId: msg.peerId
        });
        break;

      case 'signal':
        // Forward WebRTC signaling to specific peer
        const target = findPeer(msg.room, msg.targetPeerId);
        if (target) {
          target.send(JSON.stringify({
            type: 'signal',
            fromPeerId: ws.peerId,
            signal: msg.signal
          }));
        }
        break;
    }
  });

  ws.on('close', () => {
    if (ws.room && rooms.has(ws.room)) {
      rooms.get(ws.room).delete(ws);
      broadcast(ws.room, ws, {
        type: 'peer-left',
        peerId: ws.peerId
      });
    }
  });
});
```

### 2. STUN Server (Required)

Helps peers discover their public IP/port. Use free public servers:

```
stun:stun.l.google.com:19302
stun:stun1.l.google.com:19302
stun:stun.cloudflare.com:3478
stun:stun.stunprotocol.org:3478
```

### 3. TURN Server (Recommended)

Relay fallback when P2P fails. Options:

```
Options:
├── Twilio Network Traversal - Pay per GB (~$0.40/GB)
├── Cloudflare Calls - Included in Workers
├── Self-hosted coturn - ~$20/month VPS
└── Xirsys - Free tier available
```

**TURN is important for enterprise networks** (symmetric NAT, firewalls).

## Connection Flow

### Establishing a P2P Connection

```
┌──────────────┐         ┌──────────────┐         ┌──────────────┐
│   Client A   │         │   Signaling  │         │   Client B   │
│   (joiner)   │         │    Server    │         │  (existing)  │
└──────┬───────┘         └──────┬───────┘         └──────┬───────┘
       │                        │                        │
       │──── join(room) ───────►│                        │
       │                        │──── peer-joined(A) ───►│
       │                        │                        │
       │                        │◄─── offer(SDP) ────────│
       │◄─── offer(SDP) ────────│                        │
       │                        │                        │
       │──── answer(SDP) ──────►│                        │
       │                        │──── answer(SDP) ──────►│
       │                        │                        │
       │◄─────────── ICE candidates ───────────────────►│
       │              (trickled via signaling)           │
       │                        │                        │
       │◄═══════════════════════════════════════════════►│
       │         Direct P2P DataChannel established      │
       │              (signaling no longer needed)       │
```

### Swift Implementation (Apple Platforms)

```swift
import WebRTC

class P2PConnection: NSObject {
    private var peerConnection: RTCPeerConnection?
    private var dataChannel: RTCDataChannel?
    private let factory: RTCPeerConnectionFactory

    private let config: RTCConfiguration = {
        let config = RTCConfiguration()
        config.iceServers = [
            RTCIceServer(urlStrings: ["stun:stun.l.google.com:19302"]),
            RTCIceServer(
                urlStrings: ["turn:your-turn-server.com:3478"],
                username: "user",
                credential: "pass"
            )
        ]
        config.sdpSemantics = .unifiedPlan
        config.continualGatheringPolicy = .gatherContinually
        return config
    }()

    init() {
        RTCInitializeSSL()
        let encoderFactory = RTCDefaultVideoEncoderFactory()
        let decoderFactory = RTCDefaultVideoDecoderFactory()
        factory = RTCPeerConnectionFactory(
            encoderFactory: encoderFactory,
            decoderFactory: decoderFactory
        )
        super.init()
    }

    func createOffer() async throws -> RTCSessionDescription {
        let constraints = RTCMediaConstraints(
            mandatoryConstraints: nil,
            optionalConstraints: nil
        )

        peerConnection = factory.peerConnection(
            with: config,
            constraints: constraints,
            delegate: self
        )

        // Create data channel for CRDT operations
        let channelConfig = RTCDataChannelConfiguration()
        channelConfig.isOrdered = true  // Important for CRDT
        dataChannel = peerConnection?.dataChannel(
            forLabel: "crdt",
            configuration: channelConfig
        )
        dataChannel?.delegate = self

        let offer = try await peerConnection!.offer(for: constraints)
        try await peerConnection!.setLocalDescription(offer)
        return offer
    }

    func handleAnswer(_ answer: RTCSessionDescription) async throws {
        try await peerConnection?.setRemoteDescription(answer)
    }

    func handleOffer(_ offer: RTCSessionDescription) async throws -> RTCSessionDescription {
        let constraints = RTCMediaConstraints(
            mandatoryConstraints: nil,
            optionalConstraints: nil
        )

        peerConnection = factory.peerConnection(
            with: config,
            constraints: constraints,
            delegate: self
        )

        try await peerConnection?.setRemoteDescription(offer)
        let answer = try await peerConnection!.answer(for: constraints)
        try await peerConnection!.setLocalDescription(answer)
        return answer
    }

    func sendOperation(_ op: Data) {
        let buffer = RTCDataBuffer(data: op, isBinary: true)
        dataChannel?.sendData(buffer)
    }
}

extension P2PConnection: RTCPeerConnectionDelegate {
    func peerConnection(_ peerConnection: RTCPeerConnection,
                       didGenerate candidate: RTCIceCandidate) {
        // Send candidate to peer via signaling server
        signalingServer.send(candidate: candidate)
    }

    func peerConnection(_ peerConnection: RTCPeerConnection,
                       didChange state: RTCIceConnectionState) {
        switch state {
        case .connected:
            print("P2P connected!")
        case .disconnected, .failed:
            // Attempt reconnection
            attemptReconnect()
        default:
            break
        }
    }

    // ... other delegate methods
}

extension P2PConnection: RTCDataChannelDelegate {
    func dataChannel(_ dataChannel: RTCDataChannel,
                    didReceiveMessageWith buffer: RTCDataBuffer) {
        // Received CRDT operation from peer
        let operation = try? Operation.deserialize(buffer.data)
        crdtEngine.applyRemote(operation)
    }
}
```

## Network Topology

### Mesh (Default for Small Groups)

For 2-6 peers, full mesh works well:

```
      A ◄───► B
      ▲╲    ╱▲
      │ ╲  ╱ │
      │  ╲╱  │
      │  ╱╲  │
      │ ╱  ╲ │
      ▼╱    ╲▼
      C ◄───► D

Connections: n(n-1)/2
4 peers = 6 connections
6 peers = 15 connections
```

### Star (For Larger Groups)

One peer acts as hub:

```
        B
        │
        ▼
   C ◄─ A ─► D
        ▲
        │
        E

Hub (A) relays to all others
Connections: n-1
```

### Hybrid (Recommended)

```swift
enum TopologyStrategy {
    case mesh      // < 6 peers
    case star      // 6-20 peers (best-connected = hub)
    case cluster   // > 20 peers (multiple sub-meshes)
}

func selectTopology(peerCount: Int) -> TopologyStrategy {
    switch peerCount {
    case 0..<6: return .mesh
    case 6..<20: return .star
    default: return .cluster
    }
}
```

## Message Protocol

### CRDT Operations over DataChannel

```swift
enum P2PMessage: Codable {
    // CRDT sync
    case operations([Operation])      // Batch of CRDT ops
    case syncRequest(hlcFrom: HLC)    // Request ops since HLC
    case syncResponse([Operation])    // Response to sync request

    // Presence (ephemeral)
    case presence(Presence)           // Cursor, selection, name
    case presenceRequest              // Ask all peers for presence

    // Connection management
    case hello(peerId: UUID, documentId: UUID, hlcClock: HLC)
    case goodbye
    case ping
    case pong
}

// Binary encoding for efficiency
extension P2PMessage {
    func encode() -> Data {
        // Use MessagePack or CBOR for compact binary
        let encoder = MessagePackEncoder()
        return try! encoder.encode(self)
    }

    static func decode(_ data: Data) -> P2PMessage? {
        let decoder = MessagePackDecoder()
        return try? decoder.decode(P2PMessage.self, from: data)
    }
}
```

### Sync Protocol

```
Initial connection:
  A ──── hello(hlc=100) ────► B
  A ◄─── hello(hlc=150) ───── B
  A ──── syncRequest(100) ──► B    // "Send me ops since my clock"
  A ◄─── syncResponse([...]) ── B  // Ops 100-150
  A ──── syncResponse([...]) ─► B  // Ops 0-100 (B might not have)

Ongoing:
  A ──── operations([op]) ────► B  // Real-time as user edits
  A ◄─── operations([op]) ───── B
```

## Presence & Cursors

Presence is ephemeral (not persisted), sent frequently:

```swift
struct Presence: Codable {
    let peerId: UUID
    let userName: String
    let color: UInt32          // RGBA

    let sheetId: UUID          // Which sheet they're on
    let selection: Selection?  // Current selection
    let viewportCenter: (Int, Int)?  // For "follow" feature

    let timestamp: Date        // For staleness detection
}

// Send presence updates throttled (max 10/sec)
class PresenceManager {
    private var lastSent: Date = .distantPast
    private let throttle: TimeInterval = 0.1  // 100ms

    func broadcastPresence(_ presence: Presence) {
        guard Date().timeIntervalSince(lastSent) > throttle else { return }
        lastSent = Date()

        for peer in connectedPeers {
            peer.send(.presence(presence))
        }
    }
}
```

## Offline & Reconnection

### Offline Editing

CRDT enables full offline editing:

```swift
class OfflineManager {
    private var pendingOps: [Operation] = []

    func applyLocalEdit(_ op: Operation) {
        // Apply to local state immediately
        crdtEngine.apply(op)

        // Queue for sync when back online
        pendingOps.append(op)
        persistPendingOps()
    }

    func onReconnect(peer: P2PConnection) {
        // Send all pending ops
        peer.send(.operations(pendingOps))

        // Request any ops we missed
        peer.send(.syncRequest(hlcFrom: lastKnownPeerHLC))
    }
}
```

### Reconnection Strategy

```swift
class ReconnectionManager {
    private var retryCount = 0
    private let maxRetries = 10

    func scheduleReconnect() {
        guard retryCount < maxRetries else {
            // Give up, show offline indicator
            return
        }

        // Exponential backoff: 1s, 2s, 4s, 8s... max 30s
        let delay = min(pow(2.0, Double(retryCount)), 30.0)
        retryCount += 1

        DispatchQueue.main.asyncAfter(deadline: .now() + delay) {
            self.attemptConnect()
        }
    }

    func onConnected() {
        retryCount = 0
    }
}
```

## Sharing & Discovery

### Document Sharing

Options for how users share documents:

```
1. Link-based (simple):
   cells://doc/abc123
   - Document ID in URL
   - Signaling server maps ID to peers
   - Anyone with link can join

2. QR Code (local sharing):
   - Encode peer ID + offer SDP in QR
   - Scan to connect directly
   - No signaling server needed

3. Nearby (Apple Multipeer):
   - Automatic discovery on local network
   - Uses Bonjour/mDNS
   - Falls back to WebRTC for remote
```

### Access Control

```swift
enum AccessLevel: Codable {
    case owner      // Full control
    case editor     // Can edit
    case commenter  // Can comment only
    case viewer     // Read-only
}

struct DocumentAccess {
    let documentId: UUID
    let accessList: [UUID: AccessLevel]  // peerId -> level

    func canEdit(peerId: UUID) -> Bool {
        guard let level = accessList[peerId] else { return false }
        return level == .owner || level == .editor
    }
}

// Enforce on both send and receive
func handleIncomingOperation(_ op: Operation, from peerId: UUID) {
    guard documentAccess.canEdit(peerId: peerId) else {
        // Reject - peer doesn't have edit permission
        return
    }
    crdtEngine.apply(op)
}
```

## Security

### End-to-End Encryption

WebRTC provides encryption by default (DTLS-SRTP), but for additional security:

```swift
// Optional: Application-level encryption
class E2EEncryption {
    // Each document has a shared secret (derived from invite link)
    private let documentKey: SymmetricKey

    func encrypt(_ data: Data) -> Data {
        let nonce = AES.GCM.Nonce()
        let sealed = try! AES.GCM.seal(data, using: documentKey, nonce: nonce)
        return sealed.combined!
    }

    func decrypt(_ data: Data) -> Data? {
        let box = try? AES.GCM.SealedBox(combined: data)
        return try? AES.GCM.open(box!, using: documentKey)
    }
}
```

### Validation

```swift
func validateIncomingOperation(_ op: Operation) -> Bool {
    // 1. Check HLC is reasonable (not too far in future)
    guard op.timestamp.wallTime < Date().timeIntervalSince1970 + 60 else {
        return false
    }

    // 2. Check operation is well-formed
    guard op.isValid() else { return false }

    // 3. Rate limiting (prevent spam)
    guard !rateLimiter.isOverLimit(peerId: op.nodeId) else {
        return false
    }

    return true
}
```

## Platform-Specific Notes

### iOS/macOS (WebRTC)

```swift
// Use Google's WebRTC iOS SDK
// SPM: https://github.com/nicologhielmetti/AltWebRTC

dependencies: [
    .package(url: "https://github.com/nicologhielmetti/AltWebRTC", from: "1.0.0")
]
```

### Web (Native WebRTC)

```typescript
// WebRTC is built into all modern browsers
const pc = new RTCPeerConnection(config);
const dc = pc.createDataChannel('crdt', { ordered: true });
```

### Local Network Optimization

For same-network peers, skip STUN/TURN entirely:

```swift
// iOS: Use Multipeer Connectivity for local discovery
import MultipeerConnectivity

class LocalDiscovery: NSObject, MCNearbyServiceBrowserDelegate {
    // Discover peers on local network
    // Exchange WebRTC offers via Multipeer
    // Then establish WebRTC for cross-platform compatibility
}
```

## Costs Estimate

### Self-Hosted

| Component | Cost | Notes |
|-----------|------|-------|
| Signaling | ~$5/mo | Small VPS or serverless |
| STUN | $0 | Use public servers |
| TURN | ~$20/mo | Self-hosted coturn on VPS |
| **Total** | **~$25/mo** | For thousands of users |

### Managed Services

| Service | Cost | Notes |
|---------|------|-------|
| Twilio TURN | ~$0.40/GB | Only fallback traffic |
| Cloudflare | ~$5/mo | Workers + Calls |
| Firebase | Free tier | Signaling only |

**Typical usage**: With 90% direct P2P, TURN costs are minimal.

## Libraries & SDKs

### Apple Platforms
- **WebRTC.framework**: Google's official SDK
- **AltWebRTC**: SPM-compatible fork
- **MultipeerConnectivity**: For local discovery

### Signaling
- **Socket.IO**: Popular, has Swift client
- **Starscream**: Pure Swift WebSocket
- **URLSessionWebSocketTask**: Native iOS 13+

### Serialization
- **MessagePack**: Compact binary (SwiftMsgPack)
- **CBOR**: Alternative binary format
- **Protobuf**: If schema needed
