#!/bin/bash
# Build all unit tests

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$TEST_DIR/build"

echo "=== Building Papyrix Unit Tests ==="
echo "Test directory: $TEST_DIR"
echo "Build directory: $BUILD_DIR"

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake (from test directory)
echo ""
echo "=== Configuring with CMake ==="
cmake "$TEST_DIR" -DCMAKE_BUILD_TYPE=Debug

# Build
echo ""
echo "=== Building ==="
cmake --build . --parallel

echo ""
echo "=== Build Complete ==="
echo "Test binaries are in: $BUILD_DIR/bin/"
ls -la "$BUILD_DIR/bin/" 2>/dev/null || echo "No binaries found yet."
