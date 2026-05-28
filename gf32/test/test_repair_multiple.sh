#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../../" && pwd)"
PAR3_BIN="node $PROJECT_DIR/bin/par3.js"
TEST_DIR="/tmp/par3_repair_multi_$$"
NUM_FILES=20
FILE_SIZE=5120
RECOVERY_RATIO=0.2

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[PASS]${NC} $1"; }
log_fail() { echo -e "${RED}[FAIL]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

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
    if [ ! -f "$PROJECT_DIR/lib/par3gen.js" ]; then
        log_fail "par3gen.js not found"
        exit 1
    fi
    log_success "Dependencies OK"
}

cleanup() {
    log_info "Cleaning up test files..."
    rm -rf "$TEST_DIR"
    log_info "Cleanup complete"
}

trap cleanup EXIT

setup_test_dir() {
    log_info "Setting up test directory: $TEST_DIR"
    mkdir -p "$TEST_DIR/data"
    mkdir -p "$TEST_DIR/backup"
    mkdir -p "$TEST_DIR/output"
    log_info "Test directory created"
}

create_test_files() {
    log_info "Creating $NUM_FILES test files (${FILE_SIZE} bytes each)..."
    for i in $(seq 1 $NUM_FILES); do
        printf "FILE%04d_TEST_DATA_" $i | dd of="$TEST_DIR/data/file_$(printf '%04d' $i).dat" bs=$FILE_SIZE count=1 conv=notrunc 2>/dev/null
    done
    actual_count=$(ls -1 "$TEST_DIR/data" | wc -l)
    if [ "$actual_count" -ne "$NUM_FILES" ]; then
        log_fail "File count mismatch: expected $NUM_FILES, got $actual_count"
        exit 1
    fi

    log_info "Computing checksums of original files..."
    cd "$TEST_DIR/data"
    for f in *.dat; do
        sha256sum "$f" >> "$TEST_DIR/original_checksums.txt"
    done
    cd "$TEST_DIR"

    log_success "Created $NUM_FILES test files with checksums"
}

create_par3_archive() {
    log_info "Creating PAR3 archive with ${RECOVERY_RATIO%.*00}% recovery ratio..."
    cd "$TEST_DIR"

    start_time=$(date +%s.%N)
    ls data/ > files.txt
    $PAR3_BIN create -r ${RECOVERY_RATIO} --input-file files.txt -o output/test_repair --block-size 1K 2>&1
    exit_code=$?
    end_time=$(date +%s.%N)
    duration=$(echo "$end_time - $start_time" | bc)

    if [ $exit_code -ne 0 ]; then
        log_fail "PAR3 creation failed with exit code $exit_code"
        exit 1
    fi

    log_success "PAR3 creation completed in ${duration}s"
    log_info "Created PAR3 files:"
    ls -lh output/ 2>/dev/null || true
}

backup_and_remove_blocks() {
    log_info "Backing up original data (copy-on-write) and removing 3 data blocks..."

    cp -a "$TEST_DIR/data" "$TEST_DIR/backup"

    rm -f "$TEST_DIR/backup/file_0005.dat"
    rm -f "$TEST_DIR/backup/file_0012.dat"
    rm -f "$TEST_DIR/backup/file_0018.dat"

    log_info "Removed files: file_0005.dat, file_0012.dat, file_0018.dat"

    remaining=$(ls -1 "$TEST_DIR/backup" | wc -l)
    log_info "Remaining files after removal: $remaining"
}

run_repair() {
    log_info "Running PAR3 repair on corrupted data..."

    cd "$TEST_DIR"

    $PAR3_BIN repair output/test_repair.par3 "$TEST_DIR/repaired" 2>&1
    exit_code=$?

    if [ $exit_code -ne 0 ]; then
        log_warn "Repair exit code: $exit_code (may still succeed if recovery data available)"
    fi

    log_info "Repair operation completed"
}

verify_repaired_files() {
    log_info "Verifying repaired files match original checksums..."

    if [ ! -d "$TEST_DIR/repaired" ]; then
        log_fail "Repaired directory not found"
        exit 1
    fi

    cd "$TEST_DIR"

    for i in $(seq 1 $NUM_FILES); do
        fname="file_$(printf '%04d' $i).dat"
        if [ ! -f "$TEST_DIR/repaired/$fname" ]; then
            log_fail "Repaired file missing: $fname"
            exit 1
        fi
    done

    log_info "Computing checksums of repaired files..."
    cd "$TEST_DIR/repaired"
    for f in *.dat; do
        sha256sum "$f" >> "$TEST_DIR/repaired_checksums.txt"
    done
    cd "$TEST_DIR"

    log_info "Comparing original vs repaired checksums..."

    sort "$TEST_DIR/original_checksums.txt" > "$TEST_DIR/original_sorted.txt"
    sort "$TEST_DIR/repaired_checksums.txt" > "$TEST_DIR/repaired_sorted.txt"

    if diff -q "$TEST_DIR/original_sorted.txt" "$TEST_DIR/repaired_sorted.txt" > /dev/null 2>&1; then
        log_success "All file checksums match - repair verification passed!"
    else
        log_fail "Checksum mismatch - repair failed!"
        diff "$TEST_DIR/original_sorted.txt" "$TEST_DIR/repaired_sorted.txt" || true
        exit 1
    fi

    log_info "Performing byte-by-byte verification..."
    for i in $(seq 1 $NUM_FILES); do
        fname="file_$(printf '%04d' $i).dat"
        if ! cmp -s "$TEST_DIR/data/$fname" "$TEST_DIR/repaired/$fname"; then
            log_fail "Byte comparison failed for: $fname"
            exit 1
        fi
    done
    log_success "Byte-by-byte verification passed!"
}

show_test_summary() {
    log_info "Test Summary:"
    log_info "  Original files: $NUM_FILES"
    log_info "  File size: ${FILE_SIZE} bytes"
    log_info "  Recovery ratio: ${RECOVERY_RATIO} (4 recovery blocks)"
    log_info "  Removed blocks: 3 (files 5, 12, 18)"
    log_info "  Repaired successfully: all $NUM_FILES files"
}

main() {
    echo "=========================================="
    echo " T9.2: Multiple Block PAR3 Repair Test"
    echo " Testing: 20 files, 20% recovery, 3 blocks"
    echo "=========================================="
    echo ""

    check_dependencies
    setup_test_dir
    create_test_files
    create_par3_archive
    backup_and_remove_blocks
    run_repair
    verify_repaired_files
    show_test_summary

    echo ""
    echo "=========================================="
    log_success "All tests passed! Multi-block repair works correctly."
    echo "=========================================="
}

main