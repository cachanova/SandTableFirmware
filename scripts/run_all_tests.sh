#!/bin/bash

# Build the native environment
echo "Building native test environment..."
pio run -e native > /dev/null
if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

PROG=".pio/build/native/program"
FAIL_COUNT=0

# 1. Run synthetic unit tests (no arguments)
echo "========================================"
echo "Running Synthetic Unit Tests"
echo "========================================"
if timeout 30s $PROG; then
    echo "Synthetic Tests: PASS"
else
    echo "Synthetic Tests: FAIL"
    exit 1
fi

# 2. Run pattern file tests
PATTERN_DIR="test/test_motion/patterns"
echo ""
echo "========================================"
echo "Running Pattern File Tests"
echo "========================================"

# Check if directory exists
if [ ! -d "$PATTERN_DIR" ]; then
    echo "Pattern directory $PATTERN_DIR not found!"
    exit 1
fi

count=0
for f in "$PATTERN_DIR"/*.thr; do
    filename=$(basename "$f")
    # Run with timeout to prevent hangs
    # Redirect stdout to suppress verbose output, but keep stderr
    if timeout 180s $PROG "$f" > /dev/null; then
        echo "PASS: $filename"
    else
        echo "FAIL: $filename"
        exit 1
    fi
    count=$((count + 1))
done

echo ""
echo "========================================"
echo "Summary"
echo "========================================"
echo "Tested $count pattern files."

if [ $FAIL_COUNT -eq 0 ]; then
    echo "ALL TESTS PASSED"
    exit 0
else
    echo "$FAIL_COUNT TESTS FAILED"
    exit 1
fi
