# Sync Protocol Wire Format

This document describes the JSON message formats used for P2P synchronization in Cells.

## Overview

Cells uses two communication layers:

1. **Signaling** (WebSocket) - Connection setup, peer discovery
2. **Sync/Presence** (WebRTC DataChannel) - Operations and cursor data

```
┌─────────────┐    WebSocket     ┌─────────────────┐    WebSocket     ┌─────────────┐
│  Client A   │◄───────────────► │ Signaling Server│ ◄───────────────►│  Client B   │
└─────────────┘                  └─────────────────┘                  └─────────────┘
       │                                                                     │
       │◄═══════════════════ WebRTC DataChannel ═══════════════════════════►│
       │                        (operations)                                 │
       │◄═══════════════════ WebRTC DataChannel ═══════════════════════════►│
       │                        (presence)                                   │
```

## Signaling Protocol

Signaling messages are exchanged via WebSocket with the signaling server.

### Outbound Messages (Client → Server)

#### join
Join a room to collaborate with peers.

```json
{
  "type": "join",
  "room": "abc123",
  "peer_id": "user_xyz"
}
```

#### leave
Leave the current room.

```json
{
  "type": "leave",
  "room": "abc123",
  "peer_id": "user_xyz"
}
```

#### offer
Send WebRTC SDP offer to a specific peer.

```json
{
  "type": "offer",
  "target": "peer_abc",
  "sdp": {
    "type": "offer",
    "sdp": "v=0\r\no=- 12345..."
  }
}
```

#### answer
Send WebRTC SDP answer to a specific peer.

```json
{
  "type": "answer",
  "target": "peer_abc",
  "sdp": {
    "type": "answer",
    "sdp": "v=0\r\no=- 12345..."
  }
}
```

#### ice-candidate
Send ICE candidate to a specific peer.

```json
{
  "type": "ice-candidate",
  "target": "peer_abc",
  "candidate": {
    "candidate": "candidate:1 1 UDP 2130706431...",
    "sdpMid": "0",
    "sdpMLineIndex": 0
  }
}
```

### Inbound Messages (Server → Client)

#### joined
Confirmation that client joined a room, with existing peer list.

```json
{
  "type": "joined",
  "room": "abc123",
  "peers": ["peer_abc", "peer_def"]
}
```

#### peer-joined
A new peer joined the room.

```json
{
  "type": "peer-joined",
  "peer_id": "peer_new"
}
```

#### peer-left
A peer left the room.

```json
{
  "type": "peer-left",
  "peer_id": "peer_old"
}
```

#### offer / answer / ice-candidate
Forwarded from another peer (same format as outbound, with `from` instead of `target`).

```json
{
  "type": "offer",
  "from": "peer_abc",
  "sdp": { ... }
}
```

#### error
Error message from server.

```json
{
  "type": "error",
  "error": "Room not found"
}
```

---

## Sync Protocol (DataChannel: "operations")

Sync messages are sent over the WebRTC DataChannel labeled "operations".

### hello
Initial handshake when DataChannel opens. Peers exchange their current HLC and operation count.

```json
{
  "type": "hello",
  "hlc": "1703123456789.0.abc12345",
  "op_count": 42
}
```

### sync-request
Request operations since a given HLC timestamp.

```json
{
  "type": "sync-request",
  "since_hlc": "1703123400000.0.abc12345"
}
```

### sync-response
Response containing requested operations.

```json
{
  "type": "sync-response",
  "operations": [
    {
      "hlc": "1703123456789.0.abc12345",
      "type": "CELL_SET_VALUE",
      "target": "N3f8hJ2w",
      "payload": "{\"value\":\"Hello\"}"
    }
  ]
}
```

### operations
Batch of new operations (broadcast after local edits).

```json
{
  "type": "operations",
  "operations": [
    {
      "hlc": "1703123456790.0.abc12345",
      "type": "CELL_SET_VALUE",
      "target": "K9x2mP4q",
      "payload": "{\"value\":\"=A1+1\"}"
    }
  ]
}
```

### ping / pong
Latency measurement.

```json
{
  "type": "ping",
  "timestamp": 1703123456789
}
```

```json
{
  "type": "pong",
  "timestamp": 1703123456789
}
```

---

## Operation Format

Each operation in the sync protocol has the following structure:

```json
{
  "hlc": "1703123456789.0.abc12345",
  "type": "CELL_SET_VALUE",
  "target": "N3f8hJ2w",
  "payload": "{\"value\":\"Hello\"}"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `hlc` | string | Hybrid Logical Clock timestamp: `wall_ms.logical.node_id` |
| `type` | string | Operation type (see below) |
| `target` | string | 8-char base62 ID of target entity |
| `payload` | string | JSON payload (operation-specific) |

### Operation Types

#### Cell Operations
| Type | Description | Payload |
|------|-------------|---------|
| `CELL_SET_VALUE` | Set cell value | `{"value":"..."}` |
| `CELL_CLEAR` | Clear cell | `{}` |
| `CELL_SET_STYLE` | Set cell style | `{"bold":true,"italic":false,...}` |

#### Axis Operations (Column/Row)
| Type | Description | Payload |
|------|-------------|---------|
| `DIM_INSERT_AXIS` | Insert column/row | `{"dimension":0,"after":"..."}` |
| `DIM_DELETE_AXIS` | Delete column/row | `{}` |
| `DIM_MOVE_AXIS` | Move column/row | `{"after":"..."}` |
| `DIM_RESIZE_AXIS` | Resize width/height | `{"size":100}` |
| `DIM_RENAME_AXIS` | Rename column/row | `{"name":"..."}` |

#### Sheet Operations
| Type | Description | Payload |
|------|-------------|---------|
| `SHEET_CREATE` | Create sheet | `{"name":"..."}` |
| `SHEET_DELETE` | Delete sheet | `{}` |
| `SHEET_RENAME` | Rename sheet | `{"name":"..."}` |

### HLC Format

The Hybrid Logical Clock (HLC) uniquely identifies each operation:

```
1703123456789.5.abc12345
└─────────────┘ │ └──────┘
      │         │    └── 8-char base62 node ID
      │         └── Logical counter (0-999)
      └── Wall clock (ms since epoch)
```

Properties:
- Monotonically increasing per node
- Partially ordered across nodes
- Can be compared lexicographically as strings

---

## Presence Protocol (DataChannel: "presence")

Presence messages are sent over the WebRTC DataChannel labeled "presence".

### presence
Current cursor/selection state for a user.

```json
{
  "peer_id": "user_xyz",
  "name": "Happy Penguin",
  "color": "#E53935",
  "sheet_id": "sH3et001",
  "has_cursor": true,
  "cursor": {
    "col": "col_A123",
    "row": "row_0001"
  },
  "has_selection": true,
  "selection": {
    "start": { "col": "col_A123", "row": "row_0001" },
    "end": { "col": "col_B234", "row": "row_0005" }
  },
  "has_mouse": false,
  "mouse": { "x": 0.0, "y": 0.0 },
  "timestamp": 1703123456789
}
```

| Field | Type | Description |
|-------|------|-------------|
| `peer_id` | string | Unique peer identifier |
| `name` | string | Display name (e.g., "Happy Penguin") |
| `color` | string | Cursor color (hex, e.g., "#E53935") |
| `sheet_id` | string | Current sheet ID |
| `has_cursor` | bool | Whether cursor is set |
| `cursor` | object | `{col, row}` position |
| `has_selection` | bool | Whether selection is set |
| `selection` | object | `{start, end}` range |
| `has_mouse` | bool | Whether mouse position is set |
| `mouse` | object | `{x, y}` position |
| `timestamp` | number | Update timestamp (ms) |

### Presence Throttling

- Updates are throttled to max 30/second
- Broadcasting continues for 2 seconds after last activity (linger)
- Remote presence fades after 2 seconds without updates

---

## Sync Flow

### Initial Sync

```
Client A                           Client B
    │                                  │
    │◄──── DataChannel opened ────────►│
    │                                  │
    │── hello(hlc=100, op_count=5) ───►│
    │◄── hello(hlc=150, op_count=8) ───│
    │                                  │
    │── sync-request(since=100) ──────►│  A asks for ops since its last known
    │◄── sync-response([...]) ─────────│  B sends all ops since HLC 100
    │                                  │
    │◄── sync-request(since=150) ──────│  B asks for ops since its last known
    │── sync-response([...]) ─────────►│  A sends all ops since HLC 150
    │                                  │
    │══════════ Both synced ══════════│
```

### Real-time Sync

```
Client A                           Client B
    │                                  │
    │   (user edits cell)              │
    │                                  │
    │── operations([op1]) ────────────►│  Immediate broadcast
    │                                  │
    │                 (user edits cell)│
    │                                  │
    │◄── operations([op2]) ────────────│  Immediate broadcast
    │                                  │
```

### Conflict Resolution

Operations are applied using Last-Writer-Wins (LWW) based on HLC:

1. Operations arrive in any order
2. For each cell/entity, keep only the operation with highest HLC
3. Lower HLC operations are discarded
4. Same HLC (different nodes): use node_id as tiebreaker

---

## Implementation Files

| Component | Header | Implementation |
|-----------|--------|----------------|
| Signaling Protocol | `core/net/include/SignalingProtocol.h` | `core/net/common/SignalingProtocol.cc` |
| Signaling Client | `core/net/include/SignalingClient.h` | `core/net/common/SignalingClient.cc` |
| Sync Client | `core/net/include/SyncClient.h` | `core/net/common/SyncClient.cc` |
| Sync Manager | `core/cells/sync_manager.h` | `core/cells/sync_manager.cc` |
| Presence | `core/net/include/Presence.h` | `core/net/common/Presence.cc` |
| Operation | `core/cells/operation.h` | `core/cells/operation.cc` |
| HLC | `core/cells/hlc.h` | `core/cells/hlc.cc` |
