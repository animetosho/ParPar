#!/bin/bash
# Build script for par3cmdline binary (cross-compatibility testing)
#
# This builds the par3cmdline binary from source. The binary is used by
# e2e-par3-cross-compat.js to test cross-compatibility between ParPar's
# PAR3 implementation and the reference par3cmdline implementation.
#
# Requirements:
#   - cmake 3.16+
#   - C99 compiler (gcc, clang)
#   - C++20 compiler (g++, clang++)
#
# Usage:
#   ./build.sh
#
# The resulting binary will be placed at:
#   test/fixtures/par3cmdline-bin/par3
#
# Source: https://github.com/Parchive/par3cmdline.git

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="/tmp/par3cmdline-build"

echo "==> Cloning par3cmdline repository..."
if [ -d "$BUILD_DIR" ]; then
	rm -rf "$BUILD_DIR"
fi
git clone --depth=1 https://github.com/Parchive/par3cmdline.git "$BUILD_DIR"

echo "==> Configuring build..."
cmake -D CMAKE_BUILD_TYPE=Release \
      -S "$BUILD_DIR/src" \
      -B "$BUILD_DIR/build"

echo "==> Building..."
cmake --build "$BUILD_DIR/build" --config Release

echo "==> Copying binary to fixtures..."
cp "$BUILD_DIR/build/par3cmd/par3" "$SCRIPT_DIR/par3"
chmod +x "$SCRIPT_DIR/par3"

echo "==> Build complete: $SCRIPT_DIR/par3"
"$SCRIPT_DIR/par3" -V
