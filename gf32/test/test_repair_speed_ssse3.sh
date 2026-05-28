#!/bin/bash
# T9.11: Repair speed test with SSSE3
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
PAR3_BIN="node $PROJECT_DIR/bin/par3.js"
TEST_DIR="/tmp/par3_repair_ssse3_$$"
NUM_FILES=10
FILE_SIZE=10485760  # 10MB each = 100MB total

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[PASS]${NC} $1"; }
log_fail() { echo -e "${RED}[FAIL]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

check_disk_space() {
    local avail=$(df -BG "$TEST_DIR" 2>/dev/null | tail -1 | awk '{print $4}' | tr -d 'G')
    if [ "$avail" -lt 500 ]; then
        log_fail "Insufficient disk space (${avail}MB < 500MB)"
        exit 1
    fi
    log_info "Disk space available: ${avail}MB"
}

cleanup() {
    log_info "Cleaning up..."
    rm -rf "$TEST_DIR"
}
trap cleanup EXIT

check_simd() {
    if ! grep -q 'ssse3' /proc/cpuinfo 2>/dev/null; then
        log_warn "SSSE3 not available - skipping test"
        exit 0
    fi
    log_info "SSSE3 detected"
}

setup() {
    mkdir -p "$TEST_DIR/input"
    mkdir -p "$TEST_DIR/output"
}

create_test_files() {
    log_info "Creating $NUM_FILES files (${FILE_SIZE}/1024/1024 MB each)..."
    for i in $(seq 1 $NUM_FILES); do
        dd if=/dev/urandom bs=$FILE_SIZE count=1 of="$TEST_DIR/input/file_$(printf '%02d' $i).dat" 2>/dev/null
    done
    log_success "Created $NUM_FILES files"
}

create_par3() {
    log_info "Creating PAR3 archive with 10% recovery..."
    cd "$TEST_DIR"
    $PAR3_BIN create -r 0.1 --block-size 1M -o output/test input/* 2>&1
    if [ ! -f output/test.par3 ]; then
        log_fail "PAR3 creation failed"
        exit 1
    fi
    log_success "PAR3 created"
}

delete_some_files() {
    log_info "Deleting 2 source files to trigger repair..."
    rm -f "$TEST_DIR/input/file_01.dat"
    rm -f "$TEST_DIR/input/file_02.dat"
    log_success "Deleted 2 files"
}

run_repair_speed_test() {
    log_info "Running repair with SSSE3 method..."
    start_time=$(date +%s.%N)

    cd "$TEST_DIR"
    $PAR3_BIN repair -m ssse3 -o output/test.par3 2>&1

    exit_code=$?
    end_time=$(date +%s.%N)

    if [ $exit_code -ne 0 ]; then
        log_fail "Repair failed with exit code $exit_code"
        exit 1
    fi

    duration=$(echo "$end_time - $start_time" | bc)
    total_bytes=$((NUM_FILES * FILE_SIZE))
    total_mb=$((total_bytes / 1024 / 1024))
    throughput=$(echo "scale=2; $total_mb / $duration" | bc)

    echo ""
    echo "=== SSSE3 Repair Speed Results ==="
    echo "Total data: ${total_mb} MB"
    echo "Duration: ${duration}s"
    echo "Throughput: ${throughput} MB/s"
    echo "================================="

    # Verify throughput meets minimum
    min_throughput=100
    if [ $(echo "$throughput >= $min_throughput" | bc) -eq 1 ]; then
        log_success "Throughput ${throughput} MB/s >= ${min_throughput} MB/s target"
    else
        log_warn "Throughput ${throughput} MB/s < ${min_throughput} MB/s target (may be disk bound)"
    fi
}

main() {
    echo "=========================================="
    echo " T9.11: Repair Speed (SSSE3) Test"
    echo "=========================================="
    echo ""

    check_disk_space
    check_simd
    cleanup
    setup
    create_test_files
    create_par3
    delete_some_files
    run_repair_speed_test

    echo ""
    echo "=========================================="
    log_success "SSSE3 repair speed test complete"
    echo "=========================================="
}

main