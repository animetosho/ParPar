#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
PAR3_BIN="node $PROJECT_DIR/bin/par3.js"
TEST_DIR="/tmp/par3_repair_corrupt_$$"
NUM_FILES=10
FILE_SIZE=10240
RECOVERY_RATIO=0.20
CORRUPTION_RATIO=0.10

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
    if [ ! -f "$PROJECT_DIR/lib/par3gen.js" ]; then
        log_fail "par3gen.js not found"
        exit 1
    fi
    log_success "Dependencies OK"
}

setup_test_dir() {
    log_info "Setting up test directory: $TEST_DIR"
    mkdir -p "$TEST_DIR/source"
    mkdir -p "$TEST_DIR/output"
    mkdir -p "$TEST_DIR/damaged"
    mkdir -p "$TEST_DIR/restored"
    log_info "Test directory created"
}

create_test_files() {
    log_info "Creating $NUM_FILES test files (${FILE_SIZE} bytes each)..."
    local start_time=$(date +%s)

    for i in $(seq 1 $NUM_FILES); do
        dd if=/dev/urandom bs=$FILE_SIZE count=1 2>/dev/null > "$TEST_DIR/source/file_$(printf '%04d' $i).dat"
        actual_size=$(stat -c%s "$TEST_DIR/source/file_$(printf '%04d' $i).dat" 2>/dev/null || echo "0")
        if [ "$actual_size" -ne "$FILE_SIZE" ]; then
            log_fail "File $i size mismatch: expected $FILE_SIZE, got $actual_size"
            exit 1
        fi
    done

    echo -ne "\n"
    local end_time=$(date +%s)
    local duration=$((end_time - start_time))

    local actual_count=$(ls -1 "$TEST_DIR/source" | wc -l)
    if [ "$actual_count" -ne "$NUM_FILES" ]; then
        log_fail "File count mismatch: expected $NUM_FILES, got $actual_count"
        exit 1
    fi

    local total_size=$(du -sb "$TEST_DIR/source" | cut -f1)
    log_success "Created $NUM_FILES files (${total_size} bytes total) in ${duration}s"
}

store_original_checksums() {
    log_info "Storing original file checksums..."
    cd "$TEST_DIR/source"
    for i in $(seq 1 $NUM_FILES); do
        checksum=$(md5sum "file_$(printf '%04d' $i).dat" | cut -d' ' -f1)
        echo "$checksum" >> "$TEST_DIR/checksums.txt"
    done
    log_success "Checksums stored"
}

run_par3_create() {
    log_info "Running PAR3 creation with 20% recovery..."
    local start_time=$(date +%s.%N)
    cd "$TEST_DIR"

    ls source/ > files.txt
    if ! $PAR3_BIN create -r ${RECOVERY_RATIO} --input-file files.txt -o output/par3_test --block-size 1K 2>&1; then
        log_fail "PAR3 creation failed"
        exit 1
    fi

    local exit_code=$?
    local end_time=$(date +%s.%N)
    local duration=$(echo "$end_time - $start_time" | bc)

    if [ $exit_code -ne 0 ]; then
        log_fail "PAR3 creation failed with exit code $exit_code"
        exit 1
    fi

    log_success "PAR3 creation completed in ${duration}s"
}

verify_par3_structure() {
    log_info "Verifying PAR3 file structure..."
    cd "$TEST_DIR"

    if [ ! -f output/par3_test.par3 ]; then
        log_fail "Main PAR3 file not created"
        exit 1
    fi

    log_success "Main PAR3 file exists"

    local par3_size=$(stat -c%s output/par3_test.par3)
    log_info "PAR3 file size: $par3_size bytes"

    log_info "Output files:"
    ls -lh output/ 2>/dev/null || true

    local vol_count=$(ls -1 output/*.par3.vol* 2>/dev/null | wc -l || echo "0")
    log_info "Recovery volumes created: $vol_count"

    if [ "$vol_count" -eq 0 ]; then
        log_fail "No recovery volumes created"
        exit 1
    fi

    log_success "PAR3 file structure verified"
}

corrupt_recovery_data() {
    log_info "Corrupting first recovery file with 10% corruption..."

    cd "$TEST_DIR"

    local first_vol=$(ls -1 output/*.par3.vol* 2>/dev/null | head -1)

    if [ -z "$first_vol" ]; then
        log_fail "No recovery volume found to corrupt"
        exit 1
    fi

    log_info "Corrupting: $first_vol"

    local orig_size=$(stat -c%s "$first_vol")
    log_info "Original recovery file size: $orig_size bytes"

    local corrupt_size=$((orig_size * CORRUPTION_RATIO / 100))
    if [ $corrupt_size -lt 512 ]; then
        corrupt_size=512
    fi
    log_info "Corrupting $corrupt_size bytes at offset 100"

    cp "$first_vol" "$TEST_DIR/damaged/$(basename $first_vol).backup"

    dd if=/dev/urandom bs=$corrupt_size count=1 2>/dev/null | \
        dd of="$first_vol" bs=1 seek=100 conv=notrunc 2>/dev/null

    log_info "Corruption applied to $first_vol"

    local new_size=$(stat -c%s "$first_vol")
    if [ "$new_size" -ne "$orig_size" ]; then
        log_fail "Corruption changed file size: $orig_size -> $new_size"
        exit 1
    fi

    log_success "Corruption applied successfully"
}

run_repair() {
    log_info "Running PAR3 repair..."
    cd "$TEST_DIR"

    if $PAR3_BIN repair output/par3_test.par3 restored/ 2>&1; then
        log_success "Repair command completed"
    else
        log_fail "Repair command failed"
        exit 1
    fi
}

verify_repaired_files() {
    log_info "Verifying repaired files..."
    cd "$TEST_DIR/restored"

    local fail_count=0

    for i in $(seq 1 $NUM_FILES); do
        local filename="file_$(printf '%04d' $i).dat"

        if [ ! -f "$filename" ]; then
            log_fail "Missing restored file: $filename"
            fail_count=$((fail_count + 1))
            continue
        fi

        local restored_checksum=$(md5sum "$filename" | cut -d' ' -f1)
        local original_checksum=$(sed -n "${i}p" "$TEST_DIR/checksums.txt")

        if [ "$restored_checksum" = "$original_checksum" ]; then
            log_info "  $filename: checksum OK"
        else
            log_fail "  $filename: checksum mismatch (expected $original_checksum, got $restored_checksum)"
            fail_count=$((fail_count + 1))
        fi
    done

    if [ $fail_count -gt 0 ]; then
        log_fail "$fail_count files failed verification"
        exit 1
    fi

    log_success "All $NUM_FILES files verified successfully"
}

main() {
    echo "=========================================="
    echo " T9.3: PAR3 Repair with Corrupt Recovery"
    echo " Testing: 10 files, 20% recovery, 10% corrupt"
    echo "=========================================="
    echo ""

    check_dependencies
    setup_test_dir
    create_test_files
    store_original_checksums
    run_par3_create
    verify_par3_structure
    corrupt_recovery_data
    run_repair
    verify_repaired_files

    echo ""
    echo "=========================================="
    log_success "All tests passed!"
    echo "=========================================="
}

main
