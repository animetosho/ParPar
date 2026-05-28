#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../../" && pwd)"
PAR3_BIN="node $PROJECT_DIR/bin/par3.js"
TEST_DIR="/tmp/par3_repair_max_damage_$$"
NUM_FILES=100
FILE_SIZE=1024
RECOVERY_RATIO=0.05

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
    log_info "Deleting 5 input files (5% damage = maximum recoverable)..."
    cd "$TEST_DIR"

    for f in $(ls input/ | shuf -n 5); do
        rm "input/$f"
        log_info "  Deleted: $f"
    done

    log_info "Remaining files: $(ls input/ | wc -l)"
}

run_par3_verify() {
    log_info "Verifying PAR3 archive..."
    cd "$TEST_DIR"

    if $PAR3_BIN verify output/test.par3 2>&1; then
        log_info "Verify reported success"
    else
        log_info "Verify detected damage (expected)"
    fi
}

run_par3_repair() {
    log_info "Running PAR3 repair..."
    cd "$TEST_DIR"

    $PAR3_BIN repair output/test.par3 -o output/repaired 2>&1

    if [ $? -ne 0 ]; then
        log_fail "PAR3 repair failed"
        exit 1
    fi

    log_success "PAR3 repair completed"
}

verify_reconstruction() {
    log_info "Verifying full reconstruction..."
    cd "$TEST_DIR"

    reconstructed_count=$(ls -1 output/repaired/ 2>/dev/null | wc -l)

    if [ "$reconstructed_count" -ne "$NUM_FILES" ]; then
        log_fail "Reconstruction incomplete: expected $NUM_FILES, got $reconstructed_count"
        exit 1
    fi

    for i in $(seq 1 $NUM_FILES); do
        filename=$(printf 'file_%04d.dat' $i)
        if [ ! -f "output/repaired/$filename" ]; then
            log_fail "Missing reconstructed file: $filename"
            exit 1
        fi
        expected_content=$(printf "file%04dX" $i)
        actual_content=$(cat "output/repaired/$filename")
        if [ "$actual_content" != "$expected_content" ]; then
            log_fail "Content mismatch for $filename"
            exit 1
        fi
    done

    log_success "Full reconstruction verified: all $NUM_FILES files recovered"
}

run_par3_verify() {
    log_info "Verifying PAR3 archive (should detect missing blocks)..."
    cd "$TEST_DIR"

    if $PAR3_BIN verify output/test.par3 2>&1; then
        log_info "Verify reported success"
    else
        log_info "Verify detected damage (expected)"
    fi
}

run_par3_repair() {
    log_info "Running PAR3 repair..."
    cd "$TEST_DIR"

    $PAR3_BIN repair output/test.par3 -o output/repaired 2>&1

    if [ $? -ne 0 ]; then
        log_fail "PAR3 repair failed"
        exit 1
    fi

    log_success "PAR3 repair completed"
}

verify_reconstruction() {
    log_info "Verifying full reconstruction..."
    cd "$TEST_DIR"

    original_count=$NUM_FILES
    reconstructed_count=$(ls -1 output/repaired/ 2>/dev/null | wc -l)

    if [ "$reconstructed_count" -ne "$original_count" ]; then
        log_fail "Reconstruction incomplete: expected $original_count, got $reconstructed_count"
        exit 1
    fi

    for i in $(seq 1 $NUM_FILES); do
        filename=$(printf 'file_%04d.dat' $i)
        if [ ! -f "output/repaired/$filename" ]; then
            log_fail "Missing reconstructed file: $filename"
            exit 1
        fi
        expected_content=$(printf "file%04dX" $i)
        actual_content=$(cat "output/repaired/$filename")
        if [ "$actual_content" != "$expected_content" ]; then
            log_fail "Content mismatch for $filename"
            exit 1
        fi
    done

    log_success "Full reconstruction verified: all $original_count files recovered"
}

main() {
    echo "=========================================="
    echo " T9.4: Max Damage Repair Test"
    echo " Testing: 100 files, 5% damage (5 blocks)"
    echo "=========================================="
    echo ""

    check_dependencies
    setup_test_dir
    create_test_files
    run_par3_create
    damage_files
    run_par3_verify
    run_par3_repair
    verify_reconstruction

    echo ""
    echo "=========================================="
    log_success "Max damage repair test passed!"
    echo "=========================================="
}

main
