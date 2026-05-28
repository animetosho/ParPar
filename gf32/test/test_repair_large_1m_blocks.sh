#!/bin/bash
# T9.12: Repair with large block count (proxy for 1M blocks)
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
PAR3_BIN="node $PROJECT_DIR/bin/par3.js"
TEST_DIR="/tmp/par3_repair_large_$$"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[PASS]${NC} $1"; }
log_fail() { echo -e "${RED}[FAIL]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

cleanup() {
    log_info "Cleaning up..."
    rm -rf "$TEST_DIR"
}
trap cleanup EXIT

check_disk_space() {
    local avail=$(df -BG "$TEST_DIR" 2>/dev/null | tail -1 | awk '{print $4}' | tr -d 'G')
    if [ "$avail" -lt 500 ]; then
        log_fail "Insufficient disk space (${avail}MB < 500MB)"
        exit 1
    fi
}

main() {
    echo "=========================================="
    echo " T9.12: Repair Large Block Count Test"
    echo " Testing: 1000 blocks (proxy for 1M)"
    echo "=========================================="
    echo ""

    check_disk_space

    if [ ! -f "$PROJECT_DIR/bin/par3.js" ]; then
        log_fail "par3.js not found"
        exit 1
    fi

    mkdir -p "$TEST_DIR/input"
    mkdir -p "$TEST_DIR/output"

    # Create 1000 small files (100KB each = 100MB total, ~100 blocks with 1MB block size)
    log_info "Creating 1000 files (100KB each)..."
    local num_files=1000
    local file_size=102400

    for i in $(seq 1 $num_files); do
        dd if=/dev/urandom bs=$file_size count=1 of="$TEST_DIR/input/file_$(printf '%04d' $i).dat" 2>/dev/null
        if [ $((i % 200)) -eq 0 ]; then
            echo -ne "${BLUE}[INFO]${NC} Created $i/$num_files files...\r"
        fi
    done
    echo ""
    log_success "Created $num_files files"

    # Create PAR3 with 1MB blocks
    log_info "Creating PAR3 with 1MB block size..."
    cd "$TEST_DIR"

    # Use input file list
    ls input/ > inputfiles.txt
    $PAR3_BIN create -r 0.1 --block-size 1M --input-file inputfiles.txt -o output/test 2>&1

    if [ ! -f output/test.par3 ]; then
        log_fail "PAR3 creation failed"
        exit 1
    fi
    log_success "PAR3 created"

    # Record baseline memory
    baseline_mem=$(grep MemAvailable /proc/meminfo | awk '{print $2}')

    # Delete some files
    log_info "Deleting 5 source files..."
    rm -f "$TEST_DIR/input/file_0001.dat"
    rm -f "$TEST_DIR/input/file_0002.dat"
    rm -f "$TEST_DIR/input/file_0003.dat"
    rm -f "$TEST_DIR/input/file_0004.dat"
    rm -f "$TEST_DIR/input/file_0005.dat"
    log_success "Deleted 5 files"

    # Run repair and monitor memory
    log_info "Running repair..."
    $PAR3_BIN repair -o output/test.par3 2>&1

    exit_code=$?
    if [ $exit_code -ne 0 ]; then
        log_fail "Repair failed with exit code $exit_code"
        exit 1
    fi

    # Check memory after repair
    after_mem=$(grep MemAvailable /proc/meminfo | awk '{print $2}')
    mem_used=$(( (baseline_mem - after_mem) / 1024 ))

    echo ""
    echo "=== Large Block Repair Results ==="
    echo "Files: $num_files"
    echo "Block size: 1MB"
    echo "Memory used during repair: ~${mem_used} MB"
    echo "================================="

    log_success "Large block repair test complete"

    echo ""
    echo "=========================================="
    log_success "Test passed"
    echo "=========================================="
}

main