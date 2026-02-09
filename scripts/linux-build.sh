#!/bin/bash
# Build Cells CLI on Linux (glibc) using Docker
# This script builds the full CLI with networking/sync support

set -e

cd "$(dirname "$0")/.."

# Create temporary build context with only needed files
BUILD_CONTEXT=$(mktemp -d)
trap "rm -rf $BUILD_CONTEXT" EXIT

echo "Creating build context..."
# Copy essential files for Bazel build
rsync -a --exclude='.git' \
         --exclude='bazel-*' \
         --exclude='node_modules' \
         --exclude='dist' \
         --exclude='.vscode' \
         --exclude='.idea' \
         . "$BUILD_CONTEXT/"

# Remove the .dockerignore since it's designed for the Go server, not Bazel builds
rm -f "$BUILD_CONTEXT/.dockerignore"

# Use the Dockerfile from the original location
DOCKERFILE="Dockerfile.linux-build"

echo "Building Docker image..."
docker build -f "$DOCKERFILE" -t cells-linux-build "$BUILD_CONTEXT"

echo "Running build..."
docker run --rm --name cells-linux-build-$$ cells-linux-build

echo "Build complete!"
