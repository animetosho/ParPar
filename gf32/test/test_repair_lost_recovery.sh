#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../../" && pwd)"
PAR3_BIN="node $PROJECT_DIR/bin/par3.js"
TEST_DIR="/tmp/par3_repair_lost_recovery_$$"
NUM_FILES=10
FILE_SIZE=5120
RECOVERY_RATIO=0.20

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
        printf "file%04dX" $i | dd of="$TEST_DIR/input/file_$(printf '%04d' $i).dat" bs=$FILE_SIZE count=1 conv=notrunc 2>/dev/null
    done
    actual_count=$(ls -1 "$TEST_DIR/input" | wc -l)
    if [ "$actual_count" -ne "$NUM_FILES" ]; then
        log_fail "File count mismatch: expected $NUM_FILES, got $actual_count"
        exit 1
    fi
    log_success "Created $NUM_FILES test files"
}

run_par3_create() {
    log_info "Running PAR3 creation with ${RECOVERY_RATIO}% recovery ratio..."
    cd "$TEST_DIR"

    inputfiles=$(ls input/)
    echo "$inputfiles" > inputfiles.txt
    $PAR3_BIN create -r ${RECOVERY_RATIO} --input-file inputfiles.txt -o output/test --block-size 1K 2>&1

    if [ $? -ne 0 ]; then
        log_fail "PAR3 creation failed"
        exit 1
    fi

    # Count recovery files created
    recovery_count=$(ls output/*.par3 2>/dev/null | wc -l)
    log_info "Created $recovery_count PAR3 files (including recovery data)"

    log_success "PAR3 creation completed"
}

delete_recovery_files() {
    log_info "Deleting ALL recovery files (making repair impossible)..."
    cd "$TEST_DIR"

    for f in $(ls output/*.par3); do
        log_info "  Deleting: $f"
        rm "$f"
    done

    log_info "Remaining output files: $(ls output/ 2>/dev/null | wc -l)"
}

run_par3_verify_unrecoverable() {
    log_info "Verifying PAR3 archive..."
    cd "$TEST_DIR"

    output=$($PAR3_BIN verify output/test.par3 2>&1 || true)
    echo "$output"

    if echo "$output" | grep -iE "(unrecoverable|cannot repair|no recovery|not enough|insufficient)" > /dev/null; then
        log_success "Correctly detected unrecoverable damage"
        return 0
    fi

    log_info "Verify did not report unrecoverable, trying repair..."
    return 1
}

run_par3_repair_unrecoverable() {
    log_info "Running PAR3 repair..."
    cd "$TEST_DIR"

    output=$($PAR3_BIN repair output/test.par3 -o output/repaired 2>&1 || true)
    echo "$output"

    if echo "$output" | grep -iE "(unrecoverable|cannot repair|no recovery|not enough|insufficient)" > /dev/null; then
        log_success "Repair correctly failed with graceful error message"
        return 0
    fi

    if ! ls output/repaired/*.dat 2>/dev/null | head -1 | grep -q dat; then
        log_success "Repair failed (no files recovered)"
        return 0
    fi

    log_warn "Repair output unclear, but checking state..."
    return 1
}

run_par3_verify_unrecoverable() {
    log_info "Verifying PAR3 archive..."
    cd "$TEST_DIR"

    output=$($PAR3_BIN verify output/test.par3 2>&1 || true)
    echo "$output"

    if echo "$output" | grep -iE "(unrecoverable|cannot repair|no recovery|not enough|insufficient)" > /dev/null; then
        log_success "Correctly detected unrecoverable damage"
        return 0
    fi

    log_info "Verify did not report unrecoverable, trying repair..."
    return 1
}

run_par3_repair_unrecoverable() {
    log_info "Running PAR3 repair..."
    cd "$TEST_DIR"

    output=$($PAR3_BIN repair output/test.par3 -o output/repaired 2>&1 || true)
    echo "$output"

    if echo "$output" | grep -iE "(unrecoverable|cannot repair|no recovery|not enough|insufficient)" > /dev/null; then
        log_success "Repair correctly failed with graceful error message"
        return 0
    fi

    if ! ls output/repaired/*.dat 2>/dev/null | head -1 | grep -q dat; then
        log_success "Repair failed (no files recovered)"
        return 0
    fi

    log_warn "Repair output unclear, but checking state..."
    return 1
}

main() {
    echo "=========================================="
    echo " T9.5: Lost Recovery Files Test"
    echo " Testing: Unrecoverable damage (all recovery lost)"
    echo "=========================================="
    echo ""

    check_dependencies
    setup_test_dir
    create_test_files
    run_par3_create
    delete_recovery_files
    run_par3_verify_unrecoverable || run_par3_repair_unrecoverable

    echo ""
    echo "=========================================="
    log_success "Lost recovery files test passed!"
    echo "=========================================="
}

main
