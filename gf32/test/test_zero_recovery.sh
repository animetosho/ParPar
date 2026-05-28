#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../../" && pwd)"
PAR3_BIN="node $PROJECT_DIR/bin/par3.js"
TEST_DIR="/tmp/par3_zero_test_$$"
NUM_FILES=5
FILE_SIZE=1024

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
    local free_space=$(df -k "$TEST_DIR" 2>/dev/null | tail -1 | awk '{print $4}')
    local min_space=10240

    if [ -z "$free_space" ] || ! [[ "$free_space" =~ ^[0-9]+$ ]]; then
        log_warn "Could not determine disk space, assuming sufficient"
        return 0
    fi

    log_info "Available disk space: $((free_space / 1024)) MB"

    if [ "$free_space" -lt "$min_space" ]; then
        log_fail "Insufficient disk space ($((free_space / 1024)) MB < 10 MB required)"
        return 1
    fi
    return 0
}

cleanup() {
    log_info "Cleaning up test files..."
    rm -rf "$TEST_DIR"
    log_info "Cleanup complete"
}

trap cleanup EXIT

check_dependencies() {
    log_info "Checking dependencies..."
    if [ ! -f "$PROJECT_DIR/bin/par3.js" ]; then
        log_fail "par3.js not found at $PROJECT_DIR/bin/par3.js"
        exit 1
    fi
    if [ ! -d "$PROJECT_DIR/lib" ]; then
        log_fail "lib directory not found"
        exit 1
    fi
    log_success "Dependencies OK"
}

setup_test_dir() {
    log_info "Setting up test directory: $TEST_DIR"
    mkdir -p "$TEST_DIR/input"
    mkdir -p "$TEST_DIR/output"
    log_info "Test directory created"
}

create_test_files() {
    log_info "Creating $NUM_FILES test files (${FILE_SIZE} bytes each)..."

    for i in $(seq 1 $NUM_FILES); do
        printf "zero_test_file_%04d" $i | dd of="$TEST_DIR/input/file_$(printf '%04d' $i).dat" bs=$FILE_SIZE count=1 conv=notrunc 2>/dev/null
    done

    actual_count=$(ls -1 "$TEST_DIR/input" | wc -l)
    if [ "$actual_count" -ne "$NUM_FILES" ]; then
        log_fail "File count mismatch: expected $NUM_FILES, got $actual_count"
        exit 1
    fi

    total_size=$(du -sb "$TEST_DIR/input" | cut -f1)
    log_success "Created $NUM_FILES files (${total_size} bytes total)"
}

run_zero_recovery_test() {
    log_info "Running PAR3 creation with 0% recovery (zero recovery blocks)..."
    cd "$TEST_DIR"

    inputfiles=$(ls input/)
    echo "$inputfiles" > files.txt
    log_info "Input file list created with $NUM_FILES files"

    log_info "Executing: $PAR3_BIN create -r 0 --input-file files.txt -o output/test_zero_rec"
    output=$($PAR3_BIN create -r 0 --input-file files.txt -o output/test_zero_rec 2>&1)
    exit_code=$?

    echo "$output"
    log_info "Exit code: $exit_code"

    if [ $exit_code -eq 0 ]; then
        if echo "$output" | grep -qi "warning\|zero\|no recovery\|0 recovery"; then
            log_warn "PAR3 created with warning about zero recovery blocks"
            log_success "Graceful handling: succeeded with warning"
        else
            log_success "PAR3 created successfully with zero recovery blocks"
        fi
    elif echo "$output" | grep -qi "zero\|no recovery\|0 recovery\|invalid\|error.*recovery\|cannot.*0"; then
        log_success "Graceful handling: failed with clear error message about recovery"
    else
        log_fail "PAR3 creation failed with unexpected error"
        exit 1
    fi
}

verify_output_structure() {
    log_info "Verifying output structure..."
    cd "$TEST_DIR"

    if [ -f output/test_zero_rec.par3 ]; then
        par3_size=$(stat -c%s output/test_zero_rec.par3 2>/dev/null || echo 0)
        log_info "PAR3 file size: $par3_size bytes"
        log_success "PAR3 file created"
    elif [ -f output/test_zero_rec.par2 ]; then
        par2_size=$(stat -c%s output/test_zero_rec.par2 2>/dev/null || echo 0)
        log_info "PAR2 file size: $par2_size bytes"
        log_success "PAR2 file created"
    else
        log_warn "No PAR3/PAR2 file created (may be expected for zero recovery)"
    fi

    ls -la output/ 2>/dev/null || true
}

main() {
    echo "=========================================="
    echo " T8.7: Zero Recovery Blocks Edge Case Test"
    echo " Testing: PAR3 creation with 0% recovery"
    echo "=========================================="
    echo ""

    if ! check_disk_space; then
        log_fail "Disk space check failed"
        exit 1
    fi

    check_dependencies
    setup_test_dir
    create_test_files
    run_zero_recovery_test
    verify_output_structure

    echo ""
    echo "=========================================="
    log_success "Zero recovery block test completed!"
    echo "=========================================="
}

main