#!/bin/bash
# Full GDB Integration Test for dmlog_monitor
#
# This is a comprehensive end-to-end test that validates the GDB backend by:
# 1. Starting a real test application under gdbserver
# 2. Connecting dmlog_monitor via GDB Remote Serial Protocol
# 3. Reading actual dmlog buffers from the running application
# 4. Verifying that the correct log messages are received
#
# WHY THIS TEST IS NOT RUN ON EVERY CI BUILD:
# - Requires gdbserver and gdb to be installed
# - Involves complex multi-process coordination (gdbserver, gdb, test app, monitor)
# - Requires getting runtime memory addresses from the running process
# - Can be flaky due to timing issues with process startup/shutdown
# - Takes longer to run than basic tests
#
# WHEN THIS TEST RUNS:
# - Automatically on all pull requests
# - Manually via workflow_dispatch with run_full_gdb_test option
# - Can be run manually by developers: ./tests/test_monitor_gdb.sh
#
# The basic test (test_monitor_gdb_basic.sh) runs on every build and validates
# that the GDB backend compiles and handles connections correctly without
# requiring the complex gdbserver setup.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
TEST_APP="${BUILD_DIR}/tests/test_monitor_app"
MONITOR="${BUILD_DIR}/tools/monitor/dmlog_monitor"
GDB_PORT=1234
TEST_OUTPUT="/tmp/dmlog_monitor_test_output.txt"
APP_OUTPUT="/tmp/dmlog_test_app_output.txt"
GDB_CMDS="/tmp/dmlog_gdb_commands.txt"

echo "=== dmlog_monitor GDB Integration Test ==="

# Check if test app and monitor exist
if [ ! -f "$TEST_APP" ]; then
    echo "ERROR: Test application not found at $TEST_APP"
    exit 1
fi

if [ ! -f "$MONITOR" ]; then
    echo "ERROR: dmlog_monitor not found at $MONITOR"
    exit 1
fi

# Check if gdbserver is available
if ! command -v gdbserver &> /dev/null; then
    echo "ERROR: gdbserver not found. Please install gdbserver (e.g., apt-get install gdbserver)"
    exit 1
fi

# Check if gdb is available (needed to start the program)
if ! command -v gdb &> /dev/null; then
    echo "WARNING: gdb not found. Installing..."
    sudo apt-get install -y gdb >/dev/null 2>&1 || {
        echo "ERROR: Failed to install gdb"
        exit 1
    }
fi

echo "1. Starting test application under gdbserver in multi mode..."
# Start gdbserver in multi mode
gdbserver --multi :${GDB_PORT} > "$APP_OUTPUT" 2>&1 &
GDBSERVER_PID=$!

# Give gdbserver time to start
sleep 2

# Check if gdbserver is still running  
if ! kill -0 $GDBSERVER_PID 2>/dev/null; then
    echo "ERROR: gdbserver failed to start"
    cat "$APP_OUTPUT"
    exit 1
fi

echo "   gdbserver started in multi mode (PID: $GDBSERVER_PID) on port $GDB_PORT"

# Connect with GDB to start the program
echo "   Connecting with GDB to start test application..."
cat > "$GDB_CMDS" << EOF
set pagination off
set confirm off
target extended-remote :1234
file $TEST_APP
set remote exec-file $TEST_APP
run
EOF

# Run GDB in background to keep program running
(gdb -x "$GDB_CMDS" 2>&1 | tee /tmp/gdb_session.log) &
GDB_PID=$!

# Give time for program to start and write test messages
sleep 5

# Get the process ID from gdbserver output
APP_PID=$(grep -oP 'Process .* created; pid = \K\d+' "$APP_OUTPUT" | tail -1)

if [ -z "$APP_PID" ]; then
    echo "ERROR: Could not find application PID"
    cat "$APP_OUTPUT"
    kill $GDBSERVER_PID 2>/dev/null || true
    kill $GDB_PID 2>/dev/null || true
    exit 1
fi

echo "   Application running (PID: $APP_PID)"

# Get the buffer address using /proc/pid/maps and nm
BUFFER_OFFSET=$(nm "$TEST_APP" | grep ' [BbDd] test_buffer' | awk '{print "0x" $1}')
BASE_ADDR=$(grep -m 1 " $(basename "$TEST_APP")\$" /proc/$APP_PID/maps | awk '{print $1}' | cut -d'-' -f1)

if [ -z "$BASE_ADDR" ] || [ -z "$BUFFER_OFFSET" ]; then
    echo "ERROR: Could not calculate buffer address"
    echo "Base: $BASE_ADDR, Offset: $BUFFER_OFFSET"
    kill $GDBSERVER_PID 2>/dev/null || true
    kill $GDB_PID 2>/dev/null || true
    exit 1
fi

# Calculate actual runtime address
BASE_DEC=$((16#${BASE_ADDR}))
OFFSET_DEC=$((16#${BUFFER_OFFSET#0x}))
ACTUAL_ADDR_DEC=$((BASE_DEC + OFFSET_DEC))
BUFFER_ADDR=$(printf "0x%x" $ACTUAL_ADDR_DEC)

echo "   Base address: 0x$BASE_ADDR, Offset: $BUFFER_OFFSET"
echo "   dmlog buffer address: $BUFFER_ADDR"
echo "   Program is running, monitor can now connect..."

# Give time for the program to write test messages
sleep 2

echo "2. Connecting dmlog_monitor to gdbserver..."
# Run dmlog_monitor with timeout to capture logs for 5 seconds
timeout 5 "$MONITOR" --gdb --port $GDB_PORT --addr $BUFFER_ADDR --verbose > "$TEST_OUTPUT" 2>&1 || true

echo "3. Analyzing results..."

# Stop gdbserver, GDB, and test app
kill $GDB_PID 2>/dev/null || true
kill $GDBSERVER_PID 2>/dev/null || true
wait $GDBSERVER_PID 2>/dev/null || true
wait $GDB_PID 2>/dev/null || true

echo ""
echo "=== Test Application Output ==="
cat "$APP_OUTPUT"
echo ""
echo "=== Monitor Output ==="
cat "$TEST_OUTPUT"
echo ""

# Verify expected messages
echo "4. Verifying expected log messages..."

EXPECTED_MSGS=(
    "Test message 1: Hello from dmlog!"
    "Test message 2: GDB server integration test"
    "Test message 3: This is line three"
)

PASS_COUNT=0
FAIL_COUNT=0

for msg in "${EXPECTED_MSGS[@]}"; do
    if grep -q "$msg" "$TEST_OUTPUT"; then
        echo "   ✓ Found: '$msg'"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo "   ✗ Missing: '$msg'"
        FAIL_COUNT=$((FAIL_COUNT + 1))
    fi
done

echo ""
if [ $FAIL_COUNT -eq 0 ]; then
    echo "=== TEST PASSED ==="
    echo "All $PASS_COUNT expected messages found!"
    exit 0
else
    echo "=== TEST FAILED ==="
    echo "Passed: $PASS_COUNT, Failed: $FAIL_COUNT"
    exit 1
fi
