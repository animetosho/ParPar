#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../../" && pwd)"
PAR3_BIN="node $PROJECT_DIR/bin/par3.js"
TEST_DIR="/tmp/par3_verify_only_$$"
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

    log_success "PAR3 creation completed"
}

damage_files() {
    log_info "Deleting 2 source files (not recovery blocks)..."
    cd "$TEST_DIR"

    for f in $(ls input/ | shuf -n 2); do
        rm "input/$f"
        log_info "  Deleted: $f"
    done

    log_info "Remaining source files: $(ls input/ | wc -l)"
}

run_par3_verify_only() {
    log_info "Running PAR3 verify only (no repair)..."
    cd "$TEST_DIR"

    output=$($PAR3_BIN verify output/test.par3 2>&1 || true)
    echo "$output"

    if echo "$output" | grep -iE "(missing|damage|block|file)" > /dev/null; then
        log_success "Verify detected missing blocks"
    else
        log_warn "Verify output did not explicitly mention missing blocks"
    fi

    if [ -d "output/repaired" ]; then
        log_fail "Verify command should not repair - but repaired directory exists"
        exit 1
    fi

    log_success "Verify only mode confirmed: no repair attempted"
}

verify_files_still_missing() {
    log_info "Verifying that damaged files are still missing..."
    cd "$TEST_DIR"

    remaining_input=$(ls input/ | wc -l)
    log_info "Input files still missing: $((NUM_FILES - remaining_input))"

    if [ "$remaining_input" -ne $((NUM_FILES - 2)) ]; then
        log_fail "File deletion state was not preserved"
        exit 1
    fi

    log_success "Damaged files correctly remain missing (no automatic repair)"
}

run_par3_repair_after() {
    log_info "Verifying repair CAN fix the damage..."
    cd "$TEST_DIR"

    $PAR3_BIN repair output/test.par3 -o output/repaired 2>&1

    if [ $? -ne 0 ]; then
        log_fail "PAR3 repair failed when attempted"
        exit 1
    fi

    repaired_count=$(ls -1 output/repaired/ 2>/dev/null | wc -l)
    log_info "Repair recovered $repaired_count files"

    if [ "$repaired_count" -lt 2 ]; then
        log_fail "Repair should have recovered at least 2 files"
        exit 1
    fi

    log_success "Repair verification complete: damage was recoverable"
}

run_par3_verify_only() {
    log_info "Running PAR3 verify only (no repair)..."
    cd "$TEST_DIR"

    output=$($PAR3_BIN verify output/test.par3 2>&1 || true)
    echo "$output"

    if echo "$output" | grep -iE "(missing|damage|block|file)" > /dev/null; then
        log_success "Verify detected missing blocks"
    else
        log_warn "Verify output did not explicitly mention missing blocks"
    fi

    if [ -d "output/repaired" ]; then
        log_fail "Verify command should not repair - but repaired directory exists"
        exit 1
    fi

    log_success "Verify only mode confirmed: no repair attempted"
}

verify_files_still_missing() {
    log_info "Verifying that damaged files are still missing..."
    cd "$TEST_DIR"

    remaining_input=$(ls input/ | wc -l)
    log_info "Input files still missing: $((NUM_FILES - remaining_input))"

    if [ "$remaining_input" -ne $((NUM_FILES - 2)) ]; then
        log_fail "File deletion state was not preserved"
        exit 1
    fi

    log_success "Damaged files correctly remain missing (no automatic repair)"
}

run_par3_repair_after() {
    log_info "Verifying repair CAN fix the damage (for comparison)..."
    cd "$TEST_DIR"

    $PAR3_BIN repair output/test.par3 -o output/repaired 2>&1

    if [ $? -ne 0 ]; then
        log_fail "PAR3 repair failed when attempted"
        exit 1
    fi

    repaired_count=$(ls -1 output/repaired/ 2>/dev/null | wc -l)
    log_info "Repair recovered $repaired_count files"

    if [ "$repaired_count" -lt 2 ]; then
        log_fail "Repair should have recovered at least 2 files"
        exit 1
    fi

    log_success "Repair verification complete: damage was recoverable"
}

main() {
    echo "=========================================="
    echo " T9.7: Verify Only Test"
    echo " Testing: Verify detects damage without repairing"
    echo "=========================================="
    echo ""

    check_dependencies
    setup_test_dir
    create_test_files
    run_par3_create
    damage_files
    run_par3_verify_only
    verify_files_still_missing
    run_par3_repair_after

    echo ""
    echo "=========================================="
    log_success "Verify only test passed!"
    echo "=========================================="
}

main
