#!/bin/bash
# Test the Linux CLI sync command end-to-end
#
# This script:
# 1. Builds the Linux CLI binary in Docker (if needed)
# 2. Extracts the binary from the Docker image
# 3. Starts the Go signaling server with collaboration enabled
# 4. Runs the CLI sync command in Docker against the server
# 5. Verifies the connection works (sees "Connecting..." / "Online" states)
#
# Usage: ./scripts/linux-sync-test.sh [--rebuild] [--skip-build]
#   --rebuild    Force rebuild the Docker image
#   --skip-build Skip building, assume binary exists

set -e

cd "$(dirname "$0")/.."
REPO_ROOT="$(pwd)"

# Parse arguments
REBUILD=false
SKIP_BUILD=false
for arg in "$@"; do
    case $arg in
        --rebuild)
            REBUILD=true
            ;;
        --skip-build)
            SKIP_BUILD=true
            ;;
        --help|-h)
            echo "Usage: $0 [--rebuild] [--skip-build]"
            echo "  --rebuild    Force rebuild the Docker image"
            echo "  --skip-build Skip building, assume binary exists"
            exit 0
            ;;
    esac
done

# Configuration
DOCKER_IMAGE="cells-linux-build"
BINARY_NAME="cells"
EXTRACT_DIR="$REPO_ROOT/.build-output/linux"
EXTRACTED_BINARY="$EXTRACT_DIR/$BINARY_NAME"
SERVER_PORT=8091  # Use non-standard port to avoid conflicts
SERVER_PID=""
TEST_CONTAINER=""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

cleanup() {
    log_info "Cleaning up..."

    # Stop the Go server if running
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        log_info "Stopping signaling server (PID: $SERVER_PID)..."
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi

    # Stop test container if running
    if [ -n "$TEST_CONTAINER" ]; then
        docker rm -f "$TEST_CONTAINER" 2>/dev/null || true
    fi
}

trap cleanup EXIT

# Step 1: Build the Linux CLI binary (if needed)
build_binary() {
    if [ "$SKIP_BUILD" = true ]; then
        log_info "Skipping build (--skip-build)"
        return 0
    fi

    # Check if image exists and we're not forcing rebuild
    if [ "$REBUILD" = false ] && docker image inspect "$DOCKER_IMAGE" >/dev/null 2>&1; then
        log_info "Docker image '$DOCKER_IMAGE' already exists (use --rebuild to force)"
    else
        log_info "Building Linux CLI binary in Docker..."
        "$REPO_ROOT/scripts/linux-build.sh"
    fi
}

# Step 2: Extract the binary from Docker
extract_binary() {
    log_info "Extracting binary from Docker..."

    mkdir -p "$EXTRACT_DIR"

    # Find an existing build container (created by linux-build.sh)
    # The container is named cells-linux-build-<pid>
    local build_container
    build_container=$(docker ps -a --filter "name=cells-linux-build-" --format "{{.Names}}" | head -1)

    if [ -n "$build_container" ]; then
        log_info "Found existing build container: $build_container"
        # Copy the binary from the known location
        docker cp "$build_container:/build/bin/cells" "$EXTRACTED_BINARY" 2>/dev/null || {
            log_error "Failed to extract binary from container $build_container"
            log_info "Trying to find the binary location..."
            docker exec "$build_container" find /build -name "cells" -type f 2>/dev/null || \
                docker exec "$build_container" ls -la /build/bin/ 2>/dev/null || true
            return 1
        }
    else
        log_error "No build container found. Run scripts/linux-build.sh first."
        return 1
    fi

    chmod +x "$EXTRACTED_BINARY"
    log_info "Binary extracted to: $EXTRACTED_BINARY"

    # Verify it's a Linux binary
    file "$EXTRACTED_BINARY" | grep -q "ELF" || {
        log_error "Extracted file is not a Linux ELF binary"
        return 1
    }

    log_info "Binary verified as Linux ELF executable"
}

# Step 3: Start the Go signaling server
start_server() {
    log_info "Starting signaling server on port $SERVER_PORT..."

    # Check if Go is available
    if ! command -v go &> /dev/null; then
        log_error "Go is not installed. Please install Go 1.22+"
        return 1
    fi

    # Start the server in background
    # Use a minimal directory (just needs to exist for the static file server)
    local serve_dir="$REPO_ROOT/.build-output/serve-test"
    mkdir -p "$serve_dir"
    cd "$REPO_ROOT/tools/serve"
    go run . -enable-collab -port "$SERVER_PORT" -dir "$serve_dir" &
    SERVER_PID=$!
    cd "$REPO_ROOT"

    # Wait for server to be ready
    log_info "Waiting for server to start..."
    local retries=30
    while [ $retries -gt 0 ]; do
        if curl -s "http://localhost:$SERVER_PORT/" >/dev/null 2>&1; then
            log_info "Server is ready (PID: $SERVER_PID)"
            return 0
        fi
        sleep 0.5
        retries=$((retries - 1))
    done

    log_error "Server failed to start within timeout"
    return 1
}

# Step 4: Run the sync test in Docker
run_sync_test() {
    log_info "Running sync test in Docker..."

    # Determine the host address to use from inside Docker
    # On Docker Desktop (macOS/Windows), use host.docker.internal
    # On Linux, we need to use the host's IP or --network=host
    local host_addr
    if [[ "$(uname)" == "Darwin" ]] || [[ "$(uname)" == *"MINGW"* ]] || [[ "$(uname)" == *"CYGWIN"* ]]; then
        host_addr="host.docker.internal"
    else
        # On Linux, try to get the docker0 bridge IP or use host network
        host_addr="172.17.0.1"  # Default Docker bridge gateway
    fi

    local test_url="http://${host_addr}:${SERVER_PORT}/test-room-$$"
    log_info "Test URL: $test_url"

    # Create a unique container name
    TEST_CONTAINER="cells-sync-test-$$"

    # Create a test Dockerfile that runs the binary
    local test_dockerfile=$(mktemp)
    cat > "$test_dockerfile" << 'DOCKERFILE'
FROM debian:trixie-slim
RUN apt-get update && apt-get install -y ca-certificates && rm -rf /var/lib/apt/lists/*
COPY cells /usr/local/bin/cells
RUN chmod +x /usr/local/bin/cells
ENTRYPOINT ["/usr/local/bin/cells"]
DOCKERFILE

    # Build a minimal test image with just the binary
    log_info "Building test container..."
    docker build -t cells-sync-test -f "$test_dockerfile" "$EXTRACT_DIR" >/dev/null
    rm -f "$test_dockerfile"

    # Run the sync command with a timeout
    # We expect it to connect and show "Connecting..." then "Online"
    # The test passes if we see these state transitions
    log_info "Running sync command (will timeout after 10 seconds)..."

    local output_file=$(mktemp)
    local exit_code=0

    # Run docker in background and capture output
    docker run --rm \
        --name "$TEST_CONTAINER" \
        --add-host=host.docker.internal:host-gateway \
        cells-sync-test \
        sync "$test_url" > "$output_file" 2>&1 &

    local docker_pid=$!

    # Wait up to 10 seconds for output, then kill the container
    sleep 10
    docker stop "$TEST_CONTAINER" 2>/dev/null || true
    wait $docker_pid 2>/dev/null || exit_code=$?

    # Clear the container name since it's done
    TEST_CONTAINER=""

    # Check the output for expected state transitions
    log_info "Analyzing output..."

    local saw_connecting=false
    local saw_online=false
    local saw_error=false

    if grep -q "Connecting" "$output_file"; then
        saw_connecting=true
        log_info "  - Saw 'Connecting' state"
    fi

    if grep -q "Online" "$output_file"; then
        saw_online=true
        log_info "  - Saw 'Online' state"
    fi

    if grep -qi "error" "$output_file"; then
        saw_error=true
        log_warn "  - Saw error in output"
        grep -i "error" "$output_file" | head -5
    fi

    # Show full output for debugging
    echo ""
    echo "=== Full Output ==="
    cat "$output_file"
    echo "==================="
    echo ""

    rm -f "$output_file"

    # Determine test result
    # Success: saw connecting state (WebSocket/signaling works)
    # Bonus: saw online state (full connection established)
    if [ "$saw_connecting" = true ]; then
        if [ "$saw_online" = true ]; then
            log_info "TEST PASSED: CLI connected successfully and reached Online state"
            return 0
        else
            log_info "TEST PASSED: CLI connected to signaling server (Connecting state seen)"
            log_warn "Note: Did not reach Online state (may need another peer)"
            return 0
        fi
    else
        log_error "TEST FAILED: CLI did not show Connecting state"
        if [ "$saw_error" = true ]; then
            log_error "Errors were detected in the output"
        fi
        return 1
    fi
}

# Main execution
main() {
    log_info "=== Linux CLI Sync End-to-End Test ==="
    echo ""

    # Step 1: Build
    build_binary

    # Step 2: Extract
    extract_binary

    # Step 3: Start server
    start_server

    # Step 4: Run test
    if run_sync_test; then
        echo ""
        log_info "=== All tests passed! ==="
        exit 0
    else
        echo ""
        log_error "=== Tests failed ==="
        exit 1
    fi
}

main "$@"
