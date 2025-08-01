#!/bin/bash

# Test script for enhanced MDT timing logging
# Tests the new OPEN operation classification we just implemented
# **MODIFIED to prompt for the number of files to create**

TESTDIR="/mnt/lustre/enhanced_timing_test_$(date +%s)"
LOGFILE="/tmp/enhanced_timing_test_$(date +%s).log"

echo "=== Enhanced MDT Timing Test ===" | tee -a $LOGFILE
echo "Started at $(date)" | tee -a $LOGFILE
echo "Test directory: $TESTDIR" | tee -a $LOGFILE
echo "Testing enhanced logging that distinguishes CREATE vs OPEN operations" | tee -a $LOGFILE
echo "-------------" | tee -a $LOGFILE

# Function to show current operation counts with enhanced details
show_enhanced_counts() {
    local test_name="$1"
    local create_count=$(sudo dmesg | grep "MDT_TIMING.*Operation CREATE" | wc -l)
    local open_file_count=$(sudo dmesg | grep "MDT_TIMING.*Operation OPEN_" | wc -l)
    local open_total_count=$(sudo dmesg | grep "MDT_TIMING.*Operation OPEN" | wc -l)

    echo "$test_name:" | tee -a $LOGFILE
    echo "  CREATE operations: $create_count" | tee -a $LOGFILE
    echo "  OPEN_* operations: $open_file_count" | tee -a $LOGFILE
    echo "  Total OPEN operations: $open_total_count" | tee -a $LOGFILE
    echo "---" | tee -a $LOGFILE
}

# Clear kernel log
sudo dmesg -c > /dev/null
echo "Cleared kernel log buffer" | tee -a $LOGFILE

echo "=== STEP 1: Create Single Directory ===" | tee -a $LOGFILE
echo "Creating test directory (should show CREATE operation)" | tee -a $LOGFILE
mkdir -p $TESTDIR
show_enhanced_counts "After directory creation"

# *** NEW: Prompt user for the number of files ***
read -p "Enter the number of files to create: " NUM_FILES

# Validate input - check if it's a positive integer
if ! [[ "$NUM_FILES" =~ ^[0-9]+$ ]] || [ "$NUM_FILES" -le 0 ]; then
    echo "Invalid input: Please enter a positive number." | tee -a $LOGFILE
    exit 1
fi

echo "=== STEP 2: Create $NUM_FILES Files ===" | tee -a $LOGFILE
echo "Creating $NUM_FILES files (should show OPEN_CREATE_NEW or OPEN_FILE_OP operations)" | tee -a $LOGFILE

start_time=$(date +%s.%N)

# *** MODIFIED: Loop uses the user-provided NUM_FILES ***
for i in $(seq 1 $NUM_FILES); do
    filename="${TESTDIR}/enhanced_test_file_${i}"
    # Reduced verbosity for large numbers, but log every 100th
    if (( i % 100 == 1 )) || [ "$NUM_FILES" -lt 100 ]; then
        echo "Creating file: $filename" | tee -a $LOGFILE
    fi
    touch "$filename"
done

show_enhanced_counts "After creating $NUM_FILES files"

end_time=$(date +%s.%N)
duration=$(echo "$end_time - $start_time" | bc)
echo "File creation completed in ${duration} seconds" | tee -a $LOGFILE

echo "=== ENHANCED TIMING ANALYSIS ===" | tee -a $LOGFILE
echo "All enhanced MDT_TIMING entries:" | tee -a $LOGFILE
sudo dmesg | grep "MDT_TIMING" | tee -a $LOGFILE

echo "-------------" | tee -a $LOGFILE
echo "MDT_TIMING_DEBUG entries (if any):" | tee -a $LOGFILE
sudo dmesg | grep "MDT_TIMING_DEBUG" | tee -a $LOGFILE

echo "-------------" | tee -a $LOGFILE
echo "=== FINAL ANALYSIS ===" | tee -a $LOGFILE

# Count different operation types
final_create=$(sudo dmesg | grep "MDT_TIMING.*Operation CREATE" | wc -l)
final_open_enhanced=$(sudo dmesg | grep "MDT_TIMING.*Operation OPEN_" | wc -l)
final_open_total=$(sudo dmesg | grep "MDT_TIMING.*Operation OPEN" | wc -l)

echo "FINAL COUNTS:" | tee -a $LOGFILE
echo "  CREATE operations: $final_create (expected: 1 directory)" | tee -a $LOGFILE
# *** MODIFIED: Use NUM_FILES in expected count ***
echo "  Enhanced OPEN operations: $final_open_enhanced (expected: $NUM_FILES files)" | tee -a $LOGFILE
echo "  Total OPEN operations: $final_open_total" | tee -a $LOGFILE

echo "-------------" | tee -a $LOGFILE
echo "VERIFICATION:" | tee -a $LOGFILE

# Verify directory creation logging
if [ $final_create -eq 1 ]; then
    echo "✓ Directory creation logging: PASS (1 CREATE operation)" | tee -a $LOGFILE
else
    echo "✗ Directory creation logging: FAIL (expected 1, got $final_create)" | tee -a $LOGFILE
fi

# *** MODIFIED: Use NUM_FILES in verification ***
# Verify file creation logging
if [ $final_open_enhanced -eq $NUM_FILES ]; then
    echo "✓ File creation logging: PASS ($NUM_FILES enhanced OPEN operations)" | tee -a $LOGFILE
else
    echo "✗ File creation logging: CHECK (expected $NUM_FILES, got $final_open_enhanced enhanced OPEN operations)" | tee -a $LOGFILE
fi

# Check for enhanced operation details
create_new_count=$(sudo dmesg | grep "MDT_TIMING.*OPEN_CREATE_NEW" | wc -l)
file_op_count=$(sudo dmesg | grep "MDT_TIMING.*OPEN_FILE_OP" | wc -l)

echo "-------------" | tee -a $LOGFILE
echo "ENHANCED OPERATION DETAILS:" | tee -a $LOGFILE
echo "  OPEN_CREATE_NEW: $create_new_count" | tee -a $LOGFILE
echo "  OPEN_FILE_OP: $file_op_count" | tee -a $LOGFILE

if [ $create_new_count -gt 0 ]; then
    echo "✓ Enhanced logging: Detailed file creation classification working!" | tee -a $LOGFILE
elif [ $file_op_count -gt 0 ]; then
    echo "✓ Enhanced logging: Basic file operation classification working!" | tee -a $LOGFILE
else
    echo "? Enhanced logging: Check if enhancement is working properly" | tee -a $LOGFILE
fi

echo "-------------" | tee -a $LOGFILE
echo "PERFORMANCE SUMMARY:" | tee -a $LOGFILE

# Extract timing information for analysis
if [ $final_create -gt 0 ]; then
    avg_create_time=$(sudo dmesg | grep "MDT_TIMING.*Operation CREATE" | sed 's/.*took \([0-9]*\) microseconds.*/\1/' | awk '{sum+=$1} END {if(NR>0) print sum/NR; else print 0}')
    echo "  Average CREATE operation time: ${avg_create_time} microseconds" | tee -a $LOGFILE
fi

if [ $final_open_enhanced -gt 0 ]; then
    avg_open_time=$(sudo dmesg | grep "MDT_TIMING.*Operation OPEN" | sed 's/.*took \([0-9]*\) microseconds.*/\1/' | awk '{sum+=$1} END {if(NR>0) print sum/NR; else print 0}')
    echo "  Average OPEN operation time: ${avg_open_time} microseconds" | tee -a $LOGFILE
fi

echo "-------------" | tee -a $LOGFILE
echo "Created files verification:" | tee -a $LOGFILE
ls -la "$TESTDIR"/ | tee -a $LOGFILE

echo "-------------" | tee -a $LOGFILE
echo "Test completed successfully!" | tee -a $LOGFILE
echo "Results saved to: $LOGFILE"
echo "Enhanced timing data shows distinction between:"
echo "  - CREATE operations (directories)"
echo "  - OPEN_* operations (files)"

# Optional: Clean up (comment out if you want to keep the files)
# echo "Cleaning up test directory..."
# rm -rf "$TESTDIR"
