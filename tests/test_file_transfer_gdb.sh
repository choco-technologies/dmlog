#!/bin/bash
# End-to-end test for file transfer using GDB server

set -e

echo "File Transfer GDB Test"
echo "======================"
echo

# Navigate to build directory
cd "$(dirname "$0")/../build/tests"

# Find dmlog address in the binary
DMLOG_ADDR=$(nm test_file_transfer_e2e | grep " B log_buffer" | awk '{print "0x" $1}')
echo "Found dmlog buffer at: $DMLOG_ADDR"

# Start gdbserver in background
echo "Starting gdbserver..."
gdbserver --once localhost:2345 ./test_file_transfer_e2e &
GDBSERVER_PID=$!

# Wait for gdbserver to start
sleep 1

# Start monitor in background
echo "Starting monitor..."
cd ../../tools
./dmlog_monitor --gdb localhost:2345 --address "$DMLOG_ADDR" &
MONITOR_PID=$!

# Wait for monitor to connect
sleep 2

# Continue execution in gdb
echo "Continuing execution in GDB..."
echo -e "target remote localhost:2345\ncontinue\nquit" | gdb -q -batch -x /dev/stdin ./tests/test_file_transfer_e2e 2>/dev/null &

# Wait for test to complete
echo "Waiting for test to complete..."
wait $GDBSERVER_PID 2>/dev/null || true

# Give monitor time to process final messages
sleep 2

# Stop monitor
kill $MONITOR_PID 2>/dev/null || true
wait $MONITOR_PID 2>/dev/null || true

echo
echo "Test completed!"
echo

# Check if files were transferred
if [ -f "../tests/received_from_fw.txt" ]; then
    echo "✓ File received from firmware:"
    cat ../tests/received_from_fw.txt
    echo
else
    echo "✗ File from firmware not found"
fi

if [ -f "../tests/received_from_pc.txt" ]; then
    echo "✓ File received by firmware:"
    cat ../tests/received_from_pc.txt
    echo
else
    echo "✗ File received by firmware not found"
fi

echo "Test complete!"
