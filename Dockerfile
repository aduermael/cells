# Build stage for Go server
FROM golang:1.22-alpine AS builder

WORKDIR /build

# Copy Go module files
COPY tools/serve/go.mod tools/serve/go.sum ./
RUN go mod download

# Copy Go source files
COPY tools/serve/*.go ./

# Build the server binary
RUN CGO_ENABLED=0 GOOS=linux go build -ldflags="-s -w" -o server .

# Runtime stage
FROM alpine:3.20

WORKDIR /app

# Install ca-certificates for HTTPS (if needed in the future)
RUN apk --no-cache add ca-certificates

# Copy the server binary
COPY --from=builder /build/server .

# Copy the WASM distribution files (only the web assets, not source code)
COPY dist/index.html dist/
COPY dist/cells_wasm_bin.js dist/
COPY dist/cells_wasm_bin.wasm dist/
COPY dist/worker.js dist/
COPY dist/client.js dist/
COPY dist/cells.d.ts dist/
COPY dist/shared/ dist/shared/

# Expose the port
EXPOSE 8080

# Run the server with collaboration enabled
CMD ["./server", "-port", "8080", "-dir", "dist", "-enable-collab"]
