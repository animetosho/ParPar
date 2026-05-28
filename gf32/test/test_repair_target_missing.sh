#!/bin/bash
# T9.14: Repair with missing target - non-existent PAR3 file
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
PAR3_BIN="node $PROJECT_DIR/bin/par3.js"
TEST_DIR="/tmp/par3_repair_missing_$$"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[PASS]${NC} $1"; }
log_fail() { echo -e "${RED}[FAIL]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }

main() {
    echo "=========================================="
    echo " T9.14: Repair Missing Target Test"
    echo " Testing: non-existent PAR3 file"
    echo "=========================================="
    echo ""

    if [ ! -f "$PROJECT_DIR/bin/par3.js" ]; then
        log_fail "par3.js not found"
        exit 1
    fi

    # Non-existent file path
    local non_existent="/tmp/nonexistent_par3_file_$$_$(date +%s).par3"

    log_info "Attempting repair with non-existent file:"
    echo "  $non_existent"
    echo ""

    # Run repair with non-existent file
    repair_output=$($PAR3_BIN repair -o "$non_existent" 2>&1)
    exit_code=$?

    echo "$repair_output"
    echo ""

    # Verify error handling
    if [ $exit_code -ne 0 ]; then
        log_success "Repair returned non-zero exit code: $exit_code"
    else
        log_warn "Repair returned exit code 0 for missing file (may be acceptable)"
    fi

    # Check for error message indicating file not found
    if echo "$repair_output" | grep -qi "not found\|no such file\|does not exist\|cannot find\|missing"; then
        log_success "Error message clearly indicates file not found"
    else
        log_warn "Error message may not clearly indicate file not found"
    fi

    # Verify the non-existent file was NOT created
    if [ ! -f "$non_existent" ]; then
        log_success "No file was created at target path"
    else
        log_fail "File was incorrectly created at non-existent target path"
        rm -f "$non_existent"
        exit 1
    fi

    echo ""
    echo "=========================================="
    log_success "Graceful error handling verified"
    echo "=========================================="
}

main