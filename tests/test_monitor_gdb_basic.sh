#!/bin/bash
# Basic integration test for dmlog_monitor GDB backend
# This test validates that the monitor can be built and accepts GDB parameters

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
MONITOR="${BUILD_DIR}/tools/monitor/dmlog_monitor"

echo "=== dmlog_monitor GDB Backend Basic Test ==="

# Check if monitor exists
if [ ! -f "$MONITOR" ]; then
    echo "ERROR: dmlog_monitor not found at $MONITOR"
    exit 1
fi

echo "1. Testing --gdb flag recognition..."
# Test that --gdb flag is recognized and changes the backend
OUTPUT=$("$MONITOR" --gdb --help 2>&1 || true)

if echo "$OUTPUT" | grep -q "GDB server"; then
    echo "   ✓ --gdb flag recognized"
else
    echo "   ✗ --gdb flag not properly recognized"
    exit 1
fi

echo "2. Testing GDB backend connection attempt..."
# Try to connect to non-existent GDB server (should fail gracefully)
timeout 2 "$MONITOR" --gdb --port 9999 --addr 0x20010000 2>&1 | grep -q "Failed to connect to GDB server" && {
    echo "   ✓ GDB backend connection attempted"
} || {
    echo "   ✗ GDB backend connection not attempted properly"
    exit 1
}

echo "3. Testing default OpenOCD backend..."
# Try to connect to non-existent OpenOCD (should fail gracefully)
timeout 2 "$MONITOR" --port 9998 --addr 0x20010000 2>&1 | grep -q "Failed to connect to OpenOCD" && {
    echo "   ✓ OpenOCD backend works as default"
} || {
    echo "   ✗ OpenOCD backend not working properly"
    exit 1
}

echo ""
echo "=== Basic Tests PASSED ==="
echo "Note: Full GDB integration test with gdbserver requires more complex setup"
echo "      See test_monitor_gdb.sh for the full integration test"
exit 0
