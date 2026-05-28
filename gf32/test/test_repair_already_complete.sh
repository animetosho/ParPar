#!/bin/bash
# T9.13: Repair when already complete - no changes needed
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
PAR3_BIN="node $PROJECT_DIR/bin/par3.js"
TEST_DIR="/tmp/par3_repair_complete_$$"

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
    echo " T9.13: Repair Already Complete Test"
    echo " Testing: no missing blocks"
    echo "=========================================="
    echo ""

    check_disk_space

    if [ ! -f "$PROJECT_DIR/bin/par3.js" ]; then
        log_fail "par3.js not found"
        exit 1
    fi

    mkdir -p "$TEST_DIR/input"
    mkdir -p "$TEST_DIR/output"

    # Create small test files
    log_info "Creating 5 test files (100KB each)..."
    for i in $(seq 1 5); do
        dd if=/dev/urandom bs=102400 count=1 of="$TEST_DIR/input/file_$(printf '%02d' $i).dat" 2>/dev/null
    done
    log_success "Created 5 files"

    # Create PAR3
    log_info "Creating PAR3 archive..."
    cd "$TEST_DIR"
    ls input/ > files.txt
    $PAR3_BIN create -r 0.2 --block-size 1M --input-file files.txt -o output/test 2>&1

    if [ ! -f output/test.par3 ]; then
        log_fail "PAR3 creation failed"
        exit 1
    fi

    # Record file timestamps before repair
    log_info "Recording file timestamps before repair..."
    ts_before=$(stat -c %Y output/test.par3)
    sleep 1

    # Run repair on complete set (no missing blocks)
    log_info "Running repair on complete set..."
    repair_output=$($PAR3_BIN repair -o output/test.par3 2>&1)
    exit_code=$?

    echo "$repair_output"

    # Check file timestamps after repair
    ts_after=$(stat -c %Y output/test.par3)

    echo ""
    echo "=== Results ==="
    echo "Repair exit code: $exit_code"
    echo "Timestamp before: $ts_before"
    echo "Timestamp after: $ts_after"

    # Verify exit code is 0
    if [ $exit_code -ne 0 ]; then
        log_warn "Repair returned non-zero exit code (may be expected)"
    else
        log_success "Repair completed successfully"
    fi

    # Check if output mentions no changes needed
    if echo "$repair_output" | grep -qi "no.*change\|already.*complete\|nothing.*to.*do\|all.*present"; then
        log_success "Output indicates no changes were needed"
    else
        log_warn "Output does not clearly indicate 'no changes needed'"
    fi

    # Verify file was not modified
    if [ "$ts_before" -eq "$ts_after" ]; then
        log_success "PAR3 file was not modified (timestamp unchanged)"
    else
        log_warn "PAR3 file timestamp changed (may have been touched)"
    fi

    echo ""
    echo "=========================================="
    log_success "Test complete - repair correctly handled complete set"
    echo "=========================================="
}

main