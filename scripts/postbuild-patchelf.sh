#!/usr/bin/env bash
# postbuild-patchelf.sh
# Removes libnode.so.109 NEEDED entry from built .node files to fix ABI mismatch.
# Idempotent: safe to run multiple times.
set -euo pipefail

# Check if patchelf is available
if ! command -v patchelf &> /dev/null; then
    echo "postbuild-patchelf: patchelf not found in PATH, skipping (install via: sudo apt install patchelf)"
    exit 0
fi

# Only run on x86_64
ARCH="$(uname -m)"
if [[ "$ARCH" != "x86_64" ]]; then
    echo "postbuild-patchelf: skipping on $ARCH (only needed for x86_64)"
    exit 0
fi

# Find all .node files in build/Release/
BUILD_DIR="build/Release"
if [[ ! -d "$BUILD_DIR" ]]; then
    echo "postbuild-patchelf: $BUILD_DIR does not exist, skipping"
    exit 0
fi

PATCHED_COUNT=0
SKIPPED_COUNT=0

while IFS= read -r -d '' node_file; do
    # Check if libnode.so.109 is in NEEDED entries
    if patchelf --print-needed "$node_file" 2>/dev/null | grep -q "libnode.so.109"; then
        echo "postbuild-patchelf: removing libnode.so.109 from $node_file"
        patchelf --remove-needed libnode.so.109 "$node_file"
        PATCHED_COUNT=$((PATCHED_COUNT + 1))
    else
        SKIPPED_COUNT=$((SKIPPED_COUNT + 1))
    fi
done < <(find "$BUILD_DIR" -maxdepth 1 -name "*.node" -print0)

if [[ $PATCHED_COUNT -gt 0 || $SKIPPED_COUNT -gt 0 ]]; then
    echo "postbuild-patchelf: processed $(($PATCHED_COUNT + $SKIPPED_COUNT)) .node files ($PATCHED_COUNT patched, $SKIPPED_COUNT clean)"
fi

exit 0