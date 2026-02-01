#!/bin/bash
# Run all unit tests

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BIN_DIR="$PROJECT_ROOT/test/build/bin"

echo "=== Running Papyrix Unit Tests ==="
echo "Binary directory: $BIN_DIR"
echo ""

if [ ! -d "$BIN_DIR" ]; then
    echo "ERROR: Build directory not found. Run build_tests.sh first."
    exit 1
fi

FAILED=0
PASSED=0
TOTAL=0

for test_exe in "$BIN_DIR"/*; do
    if [ -x "$test_exe" ] && [ -f "$test_exe" ]; then
        TEST_NAME=$(basename "$test_exe")
        echo "----------------------------------------"
        echo "Running: $TEST_NAME"
        echo "----------------------------------------"

        TOTAL=$((TOTAL + 1))

        if "$test_exe"; then
            PASSED=$((PASSED + 1))
        else
            FAILED=$((FAILED + 1))
            echo "FAILED: $TEST_NAME (exit code: $?)"
        fi
        echo ""
    fi
done

echo "========================================"
echo "=== Test Run Summary ==="
echo "========================================"
echo "Total test suites: $TOTAL"
echo "Passed: $PASSED"
echo "Failed: $FAILED"
echo "========================================"

if [ $FAILED -gt 0 ]; then
    exit 1
fi
exit 0
