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
# Static files from apps/wasm/static/
COPY apps/wasm/static/index.html dist/
COPY apps/wasm/static/shared/ dist/shared/
# Shared assets (favicons, icons)
COPY apps/shared/favicons/ dist/favicons/
COPY apps/shared/icon.svg dist/
# Built JS files from apps/wasm/dist/
COPY apps/wasm/dist/main.js dist/
COPY apps/wasm/dist/cells_wasm_bin.js dist/
COPY apps/wasm/dist/cells_wasm_bin.wasm dist/
COPY apps/wasm/dist/worker.js dist/

# Expose the port
EXPOSE 8080

# Run the server with collaboration enabled
CMD ["./server", "-port", "8080", "-dir", "dist", "-enable-collab"]
