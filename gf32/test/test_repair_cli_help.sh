#!/bin/bash
# T9.8: CLI repair help - Verify par3 repair --help shows all options
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
PAR3_BIN="node $PROJECT_DIR/bin/par3.js"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[PASS]${NC} $1"; }
log_fail() { echo -e "${RED}[FAIL]${NC} $1"; }

main() {
    echo "=========================================="
    echo " T9.8: CLI Repair Help Test"
    echo " Testing: par3 repair --help"
    echo "=========================================="
    echo ""

    if [ ! -f "$PROJECT_DIR/bin/par3.js" ]; then
        log_fail "par3.js not found"
        exit 1
    fi

    log_info "Running: par3 repair --help"
    echo ""

    help_output=$($PAR3_BIN repair --help 2>&1)
    exit_code=$?

    if [ $exit_code -ne 0 ]; then
        log_fail "repair --help returned exit code $exit_code"
        echo "$help_output"
        exit 1
    fi

    echo "$help_output"
    echo ""

    echo "$help_output" | grep -q "block-size" && log_success "Found: block-size" || { log_fail "Missing: block-size"; exit 1; }
    echo "$help_output" | grep -q "recovery-slices" && log_success "Found: recovery-slices" || { log_fail "Missing: recovery-slices"; exit 1; }
    echo "$help_output" | grep -q "gf-method" && log_success "Found: gf-method" || { log_fail "Missing: gf-method"; exit 1; }
    echo "$help_output" | grep -q "threads" && log_success "Found: threads" || { log_fail "Missing: threads"; exit 1; }
    echo "$help_output" | grep -q "output" && log_success "Found: output" || { log_fail "Missing: output"; exit 1; }
    echo "$help_output" | grep -q "output-dir" && log_success "Found: output-dir" || { log_fail "Missing: output-dir"; exit 1; }
    echo "$help_output" | grep -q "recurse" && log_success "Found: recurse" || { log_fail "Missing: recurse"; exit 1; }
    echo "$help_output" | grep -q "skip-symlinks" && log_success "Found: skip-symlinks" || { log_fail "Missing: skip-symlinks"; exit 1; }
    echo "$help_output" | grep -q "input-file" && log_success "Found: input-file" || { log_fail "Missing: input-file"; exit 1; }
    echo "$help_output" | grep -q "memory-limit" && log_success "Found: memory-limit" || { log_fail "Missing: memory-limit"; exit 1; }
    echo "$help_output" | grep -q "help" && log_success "Found: help" || { log_fail "Missing: help"; exit 1; }

    echo ""
    echo "=========================================="
    log_success "All help options verified!"
    echo "=========================================="
}

main