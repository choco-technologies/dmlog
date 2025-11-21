#!/bin/bash
# Automated integration test for dmlog with gdbserver and dmlog_monitor
#
# This script runs multiple test scenarios to validate:
# 1. Output path (firmware → PC via dmlog_monitor)
# 2. Input path (PC → firmware via dmlog_monitor)
# 3. Mixed bidirectional communication
# 4. Various buffer sizes and edge cases
#
# The test runs under gdbserver with dmlog_monitor connected via GDB backend

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
TEST_APP="${BUILD_DIR}/tests/test_app_interactive"
MONITOR="${BUILD_DIR}/tools/monitor/dmlog_monitor"
SCENARIOS_DIR="${SCRIPT_DIR}/scenarios"

GDB_PORT=1234
MONITOR_TIMEOUT=30  # 1 minute timeout (fallback - app should exit via "exit" command)

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=== dmlog Automated Integration Test with GDB ==="
echo ""

# Check prerequisites
if [ ! -f "$TEST_APP" ]; then
    echo -e "${RED}ERROR: Test application not found at $TEST_APP${NC}"
    echo "Please build the project first: cd build && make"
    exit 1
fi

if [ ! -f "$MONITOR" ]; then
    echo -e "${RED}ERROR: dmlog_monitor not found at $MONITOR${NC}"
    echo "Please build with DMLOG_BUILD_TOOLS=ON"
    exit 1
fi

if ! command -v gdbserver &> /dev/null; then
    echo -e "${YELLOW}SKIP: gdbserver not found. Install with: apt-get install gdbserver${NC}"
    exit 0
fi

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Cleanup function
cleanup() {
    local gdbserver_pid=$1
    local monitor_pid=$2
    
    if [ -n "$monitor_pid" ] && kill -0 $monitor_pid 2>/dev/null; then
        kill $monitor_pid 2>/dev/null || true
        wait $monitor_pid 2>/dev/null || true
    fi
    
    if [ -n "$gdbserver_pid" ] && kill -0 $gdbserver_pid 2>/dev/null; then
        kill $gdbserver_pid 2>/dev/null || true
        wait $gdbserver_pid 2>/dev/null || true
    fi
}

# Prepare test files for file transfer tests
prepare_test_files() {
    local scenario_file=$1
    
    # Check if scenario has file send operations and prepare test files
    if grep -q "<send_file:" "$scenario_file"; then
        # Create test files that will be sent from firmware
        grep "<send_file:" "$scenario_file" | while IFS=: read -r _ fw_path rest; do
            fw_path=$(echo "$fw_path" | sed 's/>//')
            echo "Test file content from firmware" > "$fw_path"
            echo "This file was created for file transfer testing" >> "$fw_path"
            echo "Line 3 of test content" >> "$fw_path"
        done
    fi
    
    # Check if scenario has file receive operations and prepare test files
    if grep -q "<recv_file:" "$scenario_file"; then
        # Create test files that will be received from PC
        grep "<recv_file:" "$scenario_file" | while IFS=: read -r _ _ pc_path; do
            pc_path=$(echo "$pc_path" | sed 's/>//')
            echo "Test file content from PC" > "$pc_path"
            echo "This file was created on PC for transfer to firmware" >> "$pc_path"
            echo "Line 3 of PC test content" >> "$pc_path"
        done
    fi
}

# Run a single test scenario
run_test() {
    local scenario_file=$1
    local scenario_name=$(basename "$scenario_file" .txt)
    local buffer_size=${2:-4096}
    
    TESTS_RUN=$((TESTS_RUN + 1))
    
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "Test $TESTS_RUN: $scenario_name (buffer size: $buffer_size)"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    
    local test_output="/tmp/dmlog_test_${scenario_name}_output.txt"
    local app_output="/tmp/dmlog_test_${scenario_name}_app.txt"
    local expected_output="/tmp/dmlog_test_${scenario_name}_expected.txt"
    local input_data="/tmp/dmlog_test_${scenario_name}_input.txt"
    
    # Prepare test files for file transfers
    prepare_test_files "$scenario_file"
    
    # Generate expected output from scenario file
    # Skip comments and convert special markers to expected output
    awk '
        /^#/ { next }
        /^$/ { next }
        /<user_input>/ { 
            print "Received: Test input line " ++input_count
            next 
        }
        /<send_file:/ {
            print "Sending file: " $0
            gsub(/.*<send_file:/, "")
            gsub(/:/, " -> ")
            gsub(/>.*/, "")
            print $0
            print "File sent successfully"
            next
        }
        /<recv_file:/ {
            print "Receiving file: " $0
            gsub(/.*<recv_file:/, "")
            gsub(/:/, " <- ")
            gsub(/>.*/, "")
            print $0
            print "File received successfully"
            next
        }
        { print }
    ' "$scenario_file" > "$expected_output"
    
    # Count number of input requests
    local input_count=$(grep -c "<user_input>" "$scenario_file" || echo "0")
    
    echo "Step 1: Starting gdbserver with test application..."
    gdbserver --once :${GDB_PORT} "$TEST_APP" "$scenario_file" "$buffer_size" > "$app_output" 2>&1 &
    local GDBSERVER_PID=$!
    
    # Wait for gdbserver to be ready
    sleep 2
    
    if ! kill -0 $GDBSERVER_PID 2>/dev/null; then
        echo -e "${RED}✗ FAILED: gdbserver failed to start${NC}"
        cat "$app_output"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
    
    echo "   gdbserver started (PID: $GDBSERVER_PID)"
    
    # Get buffer address from test app using nm
    # Since we build with -no-pie, the address is fixed and predictable
    local BUFFER_ADDR=$(nm "$TEST_APP" | grep ' [BbDd] g_log_buffer' | awk '{print "0x" $1}')
    
    if [ -z "$BUFFER_ADDR" ]; then
        echo -e "${RED}✗ FAILED: Could not find g_log_buffer symbol in test application${NC}"
        cleanup $GDBSERVER_PID ""
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
    
    echo "   Buffer address: $BUFFER_ADDR"
    
    echo "Step 2: Connecting dmlog_monitor..."
    
    # Prepare input file if test has input requests
    if [ "$input_count" -gt 0 ] 2>/dev/null; then
        echo "   Test requires $input_count input(s), preparing input file..."
        
        # Generate input file with test inputs
        for i in $(seq 1 $input_count); do
            echo "Test input line $i"
        done > "$input_data"
        
        # Add "exit" command as the last input to terminate the app gracefully
        echo "exit" >> "$input_data"
        
        # Run monitor with input file
        timeout $MONITOR_TIMEOUT "$MONITOR" --gdb --port $GDB_PORT --addr $BUFFER_ADDR --time --input-file "$input_data" > "$test_output" 2>&1 &
    else
        # Run monitor without input for output-only tests
        timeout $MONITOR_TIMEOUT "$MONITOR" --gdb --port $GDB_PORT --addr $BUFFER_ADDR > "$test_output" 2>&1 &
    fi
    
    local MONITOR_PID=$!
    
    echo "   Monitor started (PID: $MONITOR_PID), running for ${MONITOR_TIMEOUT}s..."
    
    # Wait for monitor to complete (with timeout)
    wait $MONITOR_PID 2>/dev/null || true
    
    echo "Step 3: Cleaning up..."
    cleanup $GDBSERVER_PID ""
    
    # Wait a bit for port to be released
    sleep 2
    
    echo "Step 4: Validating results..."
    
    # Check if monitor produced output
    if [ ! -s "$test_output" ]; then
        echo -e "${RED}✗ FAILED: Monitor produced no output${NC}"
        echo "Application output:"
        cat "$app_output"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
    
    # Validate expected messages
    local all_found=true
    local found_count=0
    local expected_count=$(wc -l < "$expected_output")
    
    echo "   Checking for expected messages ($expected_count lines)..."
    
    while IFS= read -r expected_line; do
        if [ -n "$expected_line" ]; then
            if grep -qF "$expected_line" "$test_output"; then
                echo -e "   ${GREEN}✓${NC} Found: '$expected_line'"
                found_count=$((found_count + 1))
            else
                echo -e "   ${RED}✗${NC} Missing: '$expected_line'"
                all_found=false
            fi
        fi
    done < "$expected_output"
    
    echo ""
    echo "Summary: Found $found_count/$expected_count expected messages"
    
    if [ "$all_found" = true ]; then
        echo -e "${GREEN}✓ PASSED${NC}: $scenario_name"
        TESTS_PASSED=$((TESTS_PASSED + 1))
        return 0
    else
        echo -e "${RED}✗ FAILED${NC}: $scenario_name"
        echo ""
        echo "Expected output:"
        cat "$expected_output"
        echo ""
        echo "Monitor output:"
        cat "$test_output"
        echo ""
        echo "Application output:"
        cat "$app_output"
        TESTS_FAILED=$((TESTS_FAILED + 1))
        return 1
    fi
}

# Run all test scenarios
echo "Running test scenarios..."
echo ""

# Test 1: Output only
if [ -f "$SCENARIOS_DIR/test_output_only.txt" ]; then
    run_test "$SCENARIOS_DIR/test_output_only.txt" 4096
fi

# Test 2: Single input
if [ -f "$SCENARIOS_DIR/test_input_single.txt" ]; then
    run_test "$SCENARIOS_DIR/test_input_single.txt" 4096
fi

# Test 3: Multiple inputs
if [ -f "$SCENARIOS_DIR/test_input_multiple.txt" ]; then
    run_test "$SCENARIOS_DIR/test_input_multiple.txt" 4096
fi

# Test 4: Complex mixed
if [ -f "$SCENARIOS_DIR/test_mixed_complex.txt" ]; then
    run_test "$SCENARIOS_DIR/test_mixed_complex.txt" 2048
fi

# Test 5: File send (FW → PC)
if [ -f "$SCENARIOS_DIR/test_file_send.txt" ]; then
    run_test "$SCENARIOS_DIR/test_file_send.txt" 4096
fi

# Test 6: File receive (PC → FW)
if [ -f "$SCENARIOS_DIR/test_file_recv.txt" ]; then
    run_test "$SCENARIOS_DIR/test_file_recv.txt" 4096
fi

# Test 7: Bidirectional file transfer
if [ -f "$SCENARIOS_DIR/test_file_bidirectional.txt" ]; then
    run_test "$SCENARIOS_DIR/test_file_bidirectional.txt" 4096
fi

# Print final summary
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "FINAL RESULTS"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Total tests run:    $TESTS_RUN"
echo -e "Tests passed:       ${GREEN}$TESTS_PASSED${NC}"
if [ $TESTS_FAILED -gt 0 ]; then
    echo -e "Tests failed:       ${RED}$TESTS_FAILED${NC}"
else
    echo "Tests failed:       0"
fi
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [ $TESTS_FAILED -gt 0 ]; then
    exit 1
else
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
fi
