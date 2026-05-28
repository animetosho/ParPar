#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../../" && pwd)"
PAR3_BIN="node $PROJECT_DIR/bin/par3.js"
TEST_DIR="/tmp/par3_repair_single_$$"
NUM_FILES=10
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
    mkdir -p "$TEST_DIR/input"
    mkdir -p "$TEST_DIR/original"
    mkdir -p "$TEST_DIR/output"
}

create_test_files() {
    log_info "Creating $NUM_FILES test files (${FILE_SIZE} bytes each)..."
    for i in $(seq 1 $NUM_FILES); do
        filename=$(printf "file_%02d.dat" $i)
        printf "TESTFILE%02d" $i > "$TEST_DIR/input/$filename"
        dd if=/dev/urandom bs=1 count=$((FILE_SIZE - 10)) 2>/dev/null >> "$TEST_DIR/input/$filename"
        cp "$TEST_DIR/input/$filename" "$TEST_DIR/original/$filename"
    done
    actual_count=$(ls -1 "$TEST_DIR/input" | wc -l)
    if [ "$actual_count" -ne "$NUM_FILES" ]; then
        log_fail "File count mismatch: expected $NUM_FILES, got $actual_count"
        exit 1
    fi
    log_success "Created $NUM_FILES test files"
}

compute_checksum() {
    local file="$1"
    md5sum "$file" | awk '{print $1}'
}

run_par3_create() {
    log_info "Running PAR3 creation with ${RECOVERY_RATIO}% recovery ratio..."
    start_time=$(date +%s.%N)
    cd "$TEST_DIR"
    ls input/ > files.txt
    $PAR3_BIN create -r ${RECOVERY_RATIO} --input-file files.txt -o output/test_repair 2>&1
    exit_code=$?
    end_time=$(date +%s.%N)
    duration=$(echo "$end_time - $start_time" | bc)
    if [ $exit_code -ne 0 ]; then
        log_fail "PAR3 creation failed with exit code $exit_code"
        exit 1
    fi
    log_success "PAR3 creation completed in ${duration}s"
}

verify_par3_created() {
    log_info "Verifying PAR3 archive was created..."
    if [ ! -f output/test_repair.par3 ]; then
        log_fail "Main PAR3 file not created"
        exit 1
    fi
    par3_size=$(stat -c%s output/test_repair.par3)
    log_info "PAR3 file size: $par3_size bytes"
    output_count=$(ls -1 output/*.par3 2>/dev/null | wc -l)
    log_info "Output PAR3 files created: $output_count"
    if [ $output_count -eq 0 ]; then
        log_fail "No PAR3 output files created"
        exit 1
    fi
    log_success "PAR3 archive verified"
}

delete_data_block() {
    log_info "Deleting one data block to simulate corruption..."
    target_file="$TEST_DIR/input/file_05.dat"
    target_basename="file_05.dat"
    if [ ! -f "$target_file" ]; then
        log_fail "Target file for deletion not found: $target_file"
        exit 1
    fi
    original_checksum=$(compute_checksum "$TEST_DIR/original/$target_basename")
    log_info "Original checksum of $target_basename: $original_checksum"
    rm "$target_file"
    log_info "Deleted: $target_basename"
    if [ -f "$target_file" ]; then
        log_fail "File still exists after deletion"
        exit 1
    fi
    echo "$target_basename" > "$TEST_DIR/deleted_file.txt"
    echo "$original_checksum" > "$TEST_DIR/original_checksum.txt"
    log_success "Data block deleted successfully"
}

run_par3_repair() {
    log_info "Running PAR3 repair to recover missing block..."
    cd "$TEST_DIR"
    $PAR3_BIN repair output/test_repair.par3 2>&1
    exit_code=$?
    if [ $exit_code -ne 0 ]; then
        log_fail "PAR3 repair failed with exit code $exit_code"
        exit 1
    fi
    log_success "PAR3 repair completed"
}

verify_repaired_file() {
    log_info "Verifying repaired file matches original..."
    deleted_file=$(cat "$TEST_DIR/deleted_file.txt")
    expected_checksum=$(cat "$TEST_DIR/original_checksum.txt")
    if [ ! -f "$TEST_DIR/input/$deleted_file" ]; then
        log_fail "Repaired file not found: $deleted_file"
        exit 1
    fi
    repaired_checksum=$(compute_checksum "$TEST_DIR/input/$deleted_file")
    log_info "Original checksum: $expected_checksum"
    log_info "Repaired checksum: $repaired_checksum"
    if [ "$repaired_checksum" != "$expected_checksum" ]; then
        log_fail "Checksum mismatch! File was not repaired correctly"
        log_fail "Expected: $expected_checksum"
        log_fail "Got:      $repaired_checksum"
        exit 1
    fi
    log_success "Repaired file verification passed"
}

verify_all_files_intact() {
    log_info "Verifying all other files are still intact..."
    for i in $(seq 1 $NUM_FILES); do
        filename=$(printf "file_%02d.dat" $i)
        if [ "$filename" = "$(cat $TEST_DIR/deleted_file.txt)" ]; then
            continue
        fi
        original_checksum=$(compute_checksum "$TEST_DIR/original/$filename")
        current_checksum=$(compute_checksum "$TEST_DIR/input/$filename")
        if [ "$original_checksum" != "$current_checksum" ]; then
            log_fail "Checksum mismatch for $filename"
            log_fail "Expected: $original_checksum"
            log_fail "Got:      $current_checksum"
            exit 1
        fi
    done
    log_success "All files intact"
}

main() {
    echo "=========================================="
    echo " T9.1: PAR3 Single Block Repair Test"
    echo " Testing: Repair single missing block"
    echo "=========================================="
    echo ""
    check_dependencies
    setup_test_dir
    create_test_files
    run_par3_create
    verify_par3_created
    delete_data_block
    run_par3_repair
    verify_repaired_file
    verify_all_files_intact
    echo ""
    echo "=========================================="
    log_success "All tests passed!"
    echo "=========================================="
}

main
