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
       │                     (operations - reliable)                         │
       │◄═══════════════════ WebRTC DataChannel ═══════════════════════════►│
       │                     (presence - unreliable)                         │
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

#### peer-list
List of currently connected peers (optional, sent after joining).

```json
{
  "type": "peer-list",
  "peers": ["peer_abc", "peer_def"]
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
  "message": "Room not found"
}
```

---

## Sync Protocol (DataChannel: "operations")

Sync messages are sent over the WebRTC DataChannel labeled "operations" (reliable, ordered).

### hello
Initial handshake when DataChannel opens. Peers exchange their current HLC and operation count.

```json
{
  "type": "hello",
  "peer_id": "abc12345",
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
      "op": "CELL_SET_VALUE",
      "target": "N3f8hJ2w",
      "sheet": "sH3et001",
      "payload": {"value": "Hello"}
    }
  ],
  "complete": true
}
```

### operations
Batch of new operations (broadcast after local edits).

```json
{
  "type": "operations",
  "batch": [
    {
      "hlc": "1703123456790.0.abc12345",
      "op": "CELL_SET_VALUE",
      "target": "K9x2mP4q",
      "sheet": "sH3et001",
      "payload": {"value": "=A1+1"}
    }
  ]
}
```

### ack
Acknowledgment of received operations. Sent after receiving an operations batch to confirm receipt.

```json
{
  "type": "ack",
  "hlc": "1703123456790.0.abc12345"
}
```

### ping / pong
Latency measurement.

```json
{
  "type": "ping",
  "ts": 1703123456789
}
```

```json
{
  "type": "pong",
  "ts": 1703123456789
}
```

---

## Operation Format

Each operation in the sync protocol has the following structure:

```json
{
  "hlc": "1703123456789.0.abc12345",
  "op": "CELL_SET_VALUE",
  "target": "N3f8hJ2w",
  "sheet": "sH3et001",
  "payload": {"value": "Hello"}
}
```

| Field | Type | Description |
|-------|------|-------------|
| `hlc` | string | Hybrid Logical Clock timestamp: `wall_ms.logical.node_id` |
| `op` | string | Operation type (see below) |
| `target` | string | 8-char base62 ID of target entity |
| `sheet` | string | 8-char base62 ID of sheet context (optional, omitted for workbook-level ops) |
| `payload` | object | JSON object with operation-specific data |

### Operation Types

#### Cell Operations
| Type | Code | Description | Payload |
|------|------|-------------|---------|
| `CELL_SET_VALUE` | 0 | Set cell value | `{"value":"..."}` |
| `CELL_CLEAR` | 1 | Clear cell | `{}` |
| `CELL_SET_STYLE` | 2 | Set cell style (content-addressed) | `{"style":"<base64>"}` |
| `CELL_SET_FORMAT` | 3 | Set cell number format | `{"format_id":"..."}` |

#### Column Operations
| Type | Code | Description | Payload |
|------|------|-------------|---------|
| `COL_INSERT` | 10 | Insert column | `{"after":"..."}` |
| `COL_DELETE` | 11 | Delete column | `{}` |
| `COL_MOVE` | 12 | Move column | `{"after":"..."}` |
| `COL_RESIZE` | 13 | Resize width | `{"size":100}` |
| `COL_RENAME` | 14 | Rename column | `{"name":"..."}` |

#### Row Operations
| Type | Code | Description | Payload |
|------|------|-------------|---------|
| `ROW_INSERT` | 15 | Insert row | `{"after":"..."}` |
| `ROW_DELETE` | 16 | Delete row | `{}` |
| `ROW_MOVE` | 17 | Move row | `{"after":"..."}` |
| `ROW_RESIZE` | 18 | Resize row height | `{"size":24}` |

#### Axis Operations (apply to both columns and rows)
| Type | Code | Description | Payload |
|------|------|-------------|---------|
| `AXIS_SET_HIDDEN` | 19 | Set hidden state | `{"hidden":true}` |
| `AXIS_SET_STYLE` | 52 | Set axis default style (content-addressed) | `{"style":"<base64>"}` |
| `AXIS_SET_FORMAT` | 53 | Set axis default format | `{"format_id":"..."}` |

#### Sheet Operations
| Type | Code | Description | Payload |
|------|------|-------------|---------|
| `SHEET_CREATE` | 20 | Create sheet | `{"name":"..."}` |
| `SHEET_DELETE` | 21 | Delete sheet | `{}` |
| `SHEET_RENAME` | 22 | Rename sheet | `{"name":"..."}` |

#### Workbook Operations
| Type | Code | Description | Payload |
|------|------|-------------|---------|
| `WORKBOOK_RENAME` | 30 | Rename workbook | `{"name":"..."}` |

#### Format Operations
| Type | Code | Description | Payload |
|------|------|-------------|---------|
| `FORMAT_DEFINE` | 40 | Define custom number format | `{"id":"...","format":"..."}` |

#### Named Range Operations
| Type | Code | Description | Payload |
|------|------|-------------|---------|
| `NAMED_RANGE_DEFINE` | 50 | Define a named range | `{"name":"...","range":"..."}` |
| `NAMED_RANGE_DELETE` | 51 | Delete a named range | `{}` |

#### Range Operations
| Type | Code | Description | Payload |
|------|------|-------------|---------|
| `RANGE_ADD` | 60 | Add a new range | `{"start_col":"...","start_row":"...","end_col":"...","end_row":"...","flags":1}` |
| `RANGE_REMOVE` | 61 | Remove range | `{}` |
| `RANGE_UPDATE_CORNERS` | 62 | Update range bounds | `{"start_col":"...","start_row":"...","end_col":"...","end_row":"..."}` |
| `RANGE_UPDATE_FLAGS` | 63 | Update range flags | `{"flags":3}` |
| `RANGE_SET_STYLE` | 64 | Set range style (content-addressed) | `{"style":"<base64>"}` |

#### Content-Addressed Style Format

Style operations use base64-encoded binary data in the `"style"` field. The binary format uses a flag-based encoding:
- 2 flag bytes indicate which properties are present
- Property data follows in flag order

Example: `{"style":"BEAB"}` sets bold to true (~4 bytes encoded).

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

Presence messages are sent over the WebRTC DataChannel labeled "presence" (unreliable, unordered for low latency).

### presence
Current cursor/selection state for a user.

```json
{
  "type": "presence",
  "peer_id": "user_xyz",
  "name": "Happy Penguin",
  "color": "#E53935",
  "sheet_id": "sH3et001",
  "cursor": { "col": 0, "row": 0 },
  "selection": {
    "start": { "col": 0, "row": 0 },
    "end": { "col": 1, "row": 4 }
  },
  "mouse": { "x": 150.5, "y": 200.0 },
  "editing": { "col": 0, "row": 0, "text": "=SUM(" },
  "timestamp": 1703123456789
}
```

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | Always `"presence"` |
| `peer_id` | string | Unique peer identifier (8-char base62) |
| `name` | string | Display name (e.g., "Happy Penguin") |
| `color` | string | Cursor color (hex, e.g., "#E53935") |
| `sheet_id` | string | Current sheet ID |
| `cursor` | object/null | `{col, row}` zero-based indices, or `null` if not set |
| `selection` | object/null | `{start: {col, row}, end: {col, row}}`, or `null` if not set |
| `mouse` | object/null | `{x, y}` position relative to grid, or `null` if not set |
| `editing` | object/null | `{col, row, text}` cell being edited, or `null` if not editing |
| `timestamp` | number | Update timestamp (ms since epoch) |

### Presence Throttling

- Updates are throttled to max 5/second (200ms interval)
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
| RTC Peer Connection | `core/net/include/RTCPeerConnection.h` | `core/net/common/RTCPeerConnection.cc` |
| RTC Data Channel | `core/net/include/RTCDataChannel.h` | `core/net/common/RTCDataChannel.cc` |
| WebSocket Connection | `core/net/include/WSConnection.h` | `core/net/common/WSConnection.cc` |
