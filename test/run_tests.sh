#!/bin/bash
# GBA Emulator Test Runner
# Runs tests on host machine for fast iteration

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "╔═══════════════════════════════════════════════════════╗"
echo "║        GBA Emulator Test Runner                       ║"
echo "╚═══════════════════════════════════════════════════════╝"
echo

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo "Configuring..."
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build
echo "Building..."
make -j$(nproc)

# Run tests
echo
echo "Running tests..."
echo "─────────────────────────────────────────────────────────"
./test_cpu

# Return exit code
exit $?
