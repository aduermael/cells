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

## Development

```bash
# Install dependencies
cd tools/serve
go mod tidy

# Run server
go run main.go -port 8081

# Run tests
go test ./...
```
