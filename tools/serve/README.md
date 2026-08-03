# Cells Server

Static file server for Cells WASM distribution with WebSocket signaling for real-time collaboration.

## Requirements

- Go 1.22 or later

## Dependencies

- `github.com/gorilla/websocket` - WebSocket support for signaling

## Usage

```bash
# From repository root
go run tools/serve/main.go [options]

# Or from tools/serve directory
go run main.go [options]
```

## Options

| Flag | Default | Description |
|------|---------|-------------|
| `-port` | `8081` | Port to listen on |
| `-dir` | `dist` | Directory to serve |
| `-enable-collab` | `false` | Enable collaboration WebSocket endpoint at `/ws` |
| `-max-room-size` | `10` | Maximum number of peers per room |
| `-room-timeout` | `1h` | Timeout for empty rooms before cleanup |
| `-signaling-verbose` | `false` | Log full SDP/ICE JSON (default: compact summaries) |

### Signaling log tags (always on when collab is enabled)

| Prefix | What it shows |
|--------|----------------|
| `[JOIN]` / `[LEAVE]` | Peer enter/exit, room size, peer list, rejoin |
| `[JOINED]` | Existing-peer list sent to the joiner |
| `[NOTIFY]` | `peer-joined` / `peer-left` broadcasts |
| `[MSG]` | offer/answer/ice with **summaries** (sdp type/size, ice typ, `a_prefix`) |
| `[ORDER]` | **ice before offer** warnings (common CLI↔browser bug) |
| `[RELAY]` | Relay ok/fail (missing target is an error, not silent) |
| `[PING]` / `[WS]` / `[ERR]` | Connection health and protocol errors |

Example compact ICE line:

```text
[MSG] type=ice-candidate from=CLI to=Browser room=abc bytes=180 ice_typ=host proto=udp mid=0 mline=0 a_prefix=false cand_bytes=64
[ORDER] WARN ice_before_offer count=9 from=CLI to=Browser room=abc
```

## Collaboration

When `-enable-collab` is set, the server provides a WebSocket endpoint at `/ws` for WebRTC signaling:

```bash
# Run with collaboration enabled
go run main.go -enable-collab -port 8081
```

Clients connect to `ws://localhost:8081/ws` with query parameters:
- `room`: Room ID to join
- `peer_id`: Unique peer identifier

Or send a join message after connecting:
```json
{"type": "join", "room": "room-id", "peer_id": "peer-id"}
```

### Signaling Messages

**Client -> Server:**
- `join` - Join a room
- `leave` - Leave the room
- `offer` - WebRTC offer (with `target` peer ID)
- `answer` - WebRTC answer (with `target` peer ID)
- `ice-candidate` - ICE candidate (with `target` peer ID)

**Server -> Client:**
- `peer-list` - List of peers already in the room
- `peer-joined` - New peer joined the room
- `peer-left` - Peer left the room
- `offer` - Relayed offer (with `from` peer ID)
- `answer` - Relayed answer (with `from` peer ID)
- `ice-candidate` - Relayed ICE candidate (with `from` peer ID)
- `error` - Error message

## Development

```bash
# Install dependencies
cd tools/serve
go mod tidy

# Run server
go run main.go -port 8081

# Run server with collaboration
go run main.go -enable-collab -port 8081

# Run tests
go test ./...
```
