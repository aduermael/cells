#!/bin/bash
# Build full CLI binary for Alpine Linux (musl libc) using Docker
# Includes networking/sync support (libcurl, libdatachannel, OpenSSL)
# Output: dist/cli/cells-alpine
set -euo pipefail

# Get repository root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

IMAGE_NAME="cells-alpine-build"
CONTAINER_NAME="cells-alpine-build-$$"

echo "Building Alpine Linux CLI..."

# Swap dockerignore files for the build
# The default .dockerignore is for the Go server; we need different excludes for CLI
cleanup() {
    if [ -f .dockerignore.server ]; then
        mv .dockerignore.server .dockerignore
    fi
    rm -f Dockerfile.alpine-build.dockerignore
}
trap cleanup EXIT

if [ -f .dockerignore ]; then
    mv .dockerignore .dockerignore.server
fi

# Create a minimal dockerignore for CLI build
cat > .dockerignore << 'EOF'
# Git
.git
.gitignore

# Bazel build artifacts (will be regenerated)
bazel-*
.bazelrc.user

# IDE
.vscode
.idea
*.swp
*.swo

# Large test data not needed for CLI build
testdata/xlsx/*.xlsx
testdata/csv/*.csv

# Node modules
node_modules
**/node_modules

# Distribution artifacts
dist/

# Documentation
docs/

# Plans
plans/

# Cache directories
.cache/
EOF

# Build the Docker image
echo "Step 1/3: Building Docker image..."
docker build -f Dockerfile.alpine-build -t "$IMAGE_NAME" .

# Run the build inside the container
echo "Step 2/3: Building CLI binary inside container..."
docker run --name "$CONTAINER_NAME" "$IMAGE_NAME"

# Copy the binary out
echo "Step 3/3: Extracting binary..."
mkdir -p dist/cli
docker cp "$CONTAINER_NAME:/build/bin/cells" dist/cli/cells-alpine

# Clean up container
docker rm "$CONTAINER_NAME" > /dev/null

echo ""
echo "Built: dist/cli/cells-alpine"
echo ""
echo "Verify with: file dist/cli/cells-alpine"
