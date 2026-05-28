#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../../" && pwd)"
PAR3_BIN="node $PROJECT_DIR/bin/par3.js"
TEST_DIR="/tmp/par3_repair_missing_source_$$"
NUM_FILES=20
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
    log_info "Deleting 10 source files (50% loss)..."
    cd "$TEST_DIR"

    for f in $(ls input/ | shuf -n 10); do
        rm "input/$f"
        log_info "  Deleted: $f"
    done

    log_info "Remaining source files: $(ls input/ | wc -l)"
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

verify_partial_reconstruction() {
    log_info "Verifying partial reconstruction..."
    cd "$TEST_DIR"

    recovered_count=$(ls -1 output/repaired/ 2>/dev/null | wc -l)
    log_info "Recovered files: $recovered_count"

    if [ "$recovered_count" -lt 5 ]; then
        log_fail "Too few files recovered: $recovered_count"
        exit 1
    fi

    if [ "$recovered_count" -gt 15 ]; then
        log_fail "Too many files recovered: $recovered_count (something is wrong)"
        exit 1
    fi

    valid_count=0
    for f in output/repaired/*.dat; do
        if [ -f "$f" ]; then
            filename=$(basename "$f")
            filenum=$(echo "$filename" | sed 's/file_0*\([0-9]*\).dat/\1/')
            expected_content=$(printf "file%04dX" $filenum)
            actual_content=$(cat "$f")
            if [ "$actual_content" = "$expected_content" ]; then
                valid_count=$((valid_count + 1))
            fi
        fi
    done

    log_info "Files with valid content: $valid_count / $recovered_count"

    if [ "$valid_count" -ne "$recovered_count" ]; then
        log_fail "Some recovered files have invalid content"
        exit 1
    fi

    log_success "Partial reconstruction verified: $recovered_count files recovered"
    log_info "Note: 10 files remain missing (expected with 50% loss and 20% recovery)"
}

run_par3_repair() {
    log_info "Running PAR3 repair (should recover available files)..."
    cd "$TEST_DIR"

    $PAR3_BIN repair output/test.par3 -o output/repaired 2>&1

    if [ $? -ne 0 ]; then
        log_fail "PAR3 repair failed"
        exit 1
    fi

    log_success "PAR3 repair completed"
}

verify_partial_reconstruction() {
    log_info "Verifying partial reconstruction..."
    cd "$TEST_DIR"

    recovered_count=$(ls -1 output/repaired/ 2>/dev/null | wc -l)
    log_info "Recovered files: $recovered_count"

    if [ "$recovered_count" -lt 5 ]; then
        log_fail "Too few files recovered: $recovered_count"
        exit 1
    fi

    if [ "$recovered_count" -gt 15 ]; then
        log_fail "Too many files recovered: $recovered_count (something is wrong)"
        exit 1
    fi

    valid_count=0
    for f in output/repaired/*.dat; do
        if [ -f "$f" ]; then
            filename=$(basename "$f")
            filenum=$(echo "$filename" | sed 's/file_0*\([0-9]*\).dat/\1/')
            expected_content=$(printf "file%04dX" $filenum)
            actual_content=$(cat "$f")
            if [ "$actual_content" = "$expected_content" ]; then
                valid_count=$((valid_count + 1))
            fi
        fi
    done

    log_info "Files with valid content: $valid_count / $recovered_count"

    if [ "$valid_count" -ne "$recovered_count" ]; then
        log_fail "Some recovered files have invalid content"
        exit 1
    fi

    log_success "Partial reconstruction verified: $recovered_count files recovered"
    log_info "Note: $missing_expected files remain missing (expected with 50% loss and 20% recovery)"
}

main() {
    echo "=========================================="
    echo " T9.6: Missing Source Files Test"
    echo " Testing: 20 files, 50% loss, 20% recovery"
    echo "=========================================="
    echo ""

    check_dependencies
    setup_test_dir
    create_test_files
    run_par3_create
    damage_files
    run_par3_repair
    verify_partial_reconstruction

    echo ""
    echo "=========================================="
    log_success "Missing source files repair test passed!"
    echo "=========================================="
}

main
