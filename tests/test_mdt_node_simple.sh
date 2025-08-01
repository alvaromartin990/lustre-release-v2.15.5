#!/bin/bash

# Simple test script to verify OPEN_FILE_OP operations
# Creates 1 directory and 10 files to test OPEN_FILE_OP logging

TESTDIR="/mnt/lustre/simple_open_test_$(date +%s)"
LOGFILE="/tmp/simple_open_test_$(date +%s).log"

echo "=== Simple OPEN_FILE_OP Test ===" | tee -a $LOGFILE
echo "Started at $(date)" | tee -a $LOGFILE
echo "Test directory: $TESTDIR" | tee -a $LOGFILE
echo "Creating 1 directory and 10 files to test OPEN_FILE_OP operations" | tee -a $LOGFILE
echo "=====================================================================" | tee -a $LOGFILE

# Function to show kernel log analysis
show_analysis() {
    local step_name="$1"
    echo "" | tee -a $LOGFILE
    echo "=== $step_name ===" | tee -a $LOGFILE
    
    # Count all MDT_TIMING entries
    local total_timing=$(sudo dmesg | grep "MDT_TIMING" | wc -l)
    echo "Total MDT_TIMING entries: $total_timing" | tee -a $LOGFILE
    
    # Count OPEN_FILE_OP specifically
    local open_file_ops=$(sudo dmesg | grep "MDT_TIMING.*OPEN_FILE_OP" | wc -l)
    echo "OPEN_FILE_OP operations: $open_file_ops" | tee -a $LOGFILE
    
    # Count other OPEN operations
    local open_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation OPEN" | wc -l)
    echo "Total OPEN operations: $open_ops" | tee -a $LOGFILE
    
    # Count CREATE operations
    local create_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation CREATE" | wc -l)
    echo "CREATE operations: $create_ops" | tee -a $LOGFILE
    
    # Show MDT nodes involved
    echo "MDT nodes performing operations:" | tee -a $LOGFILE
    sudo dmesg | grep "MDT_TIMING.*\[MDT:" | grep -o "\[MDT:[^]]*\]" | sort | uniq -c | tee -a $LOGFILE
    
    echo "---" | tee -a $LOGFILE
}

echo "Step 1: Clearing kernel log buffer..." | tee -a $LOGFILE
sudo dmesg -c > /dev/null

echo "Step 2: Creating test directory..." | tee -a $LOGFILE
mkdir -p "$TESTDIR"
echo "Created directory: $TESTDIR" | tee -a $LOGFILE

# Wait a moment for directory creation to be logged
sleep 1

show_analysis "After directory creation"

echo "" | tee -a $LOGFILE
echo "Step 3: Creating 10 test files..." | tee -a $LOGFILE

# Create 10 files one by one
for i in {1..10}; do
    filename="${TESTDIR}/test_file_${i}.txt"
    echo "Creating file $i: $filename" | tee -a $LOGFILE
    
    # Create the file (should trigger OPEN_FILE_OP)
    touch "$filename"
    
    # Add some content (might trigger additional operations)
    echo "This is test file number $i created at $(date)" > "$filename"
    
    # Small delay to see individual operations
    sleep 0.1
    
    # Show progress every 5 files
    if [ $i -eq 5 ]; then
        show_analysis "After creating 5 files"
    fi
done

echo "" | tee -a $LOGFILE
echo "Step 4: Final analysis..." | tee -a $LOGFILE
show_analysis "After creating all 10 files"

echo "" | tee -a $LOGFILE
echo "=====================================================================" | tee -a $LOGFILE
echo "=== DETAILED KERNEL LOG ANALYSIS ===" | tee -a $LOGFILE

echo "" | tee -a $LOGFILE
echo "All MDT_TIMING entries captured:" | tee -a $LOGFILE
sudo dmesg | grep "MDT_TIMING" | tee -a $LOGFILE

echo "" | tee -a $LOGFILE
echo "=== OPEN_FILE_OP SPECIFIC ENTRIES ===" | tee -a $LOGFILE
local open_file_entries=$(sudo dmesg | grep "MDT_TIMING.*OPEN_FILE_OP")
if [ -n "$open_file_entries" ]; then
    echo "$open_file_entries" | tee -a $LOGFILE
else
    echo "No OPEN_FILE_OP entries found!" | tee -a $LOGFILE
fi

echo "" | tee -a $LOGFILE
echo "=== ALL OPEN OPERATION ENTRIES ===" | tee -a $LOGFILE
local all_open_entries=$(sudo dmesg | grep "MDT_TIMING.*Operation OPEN")
if [ -n "$all_open_entries" ]; then
    echo "$all_open_entries" | tee -a $LOGFILE
else
    echo "No OPEN operation entries found!" | tee -a $LOGFILE
fi

echo "" | tee -a $LOGFILE
echo "=== VERIFICATION RESULTS ===" | tee -a $LOGFILE

# Count results
total_timing=$(sudo dmesg | grep "MDT_TIMING" | wc -l)
open_file_ops=$(sudo dmesg | grep "MDT_TIMING.*OPEN_FILE_OP" | wc -l)
open_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation OPEN" | wc -l)
create_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation CREATE" | wc -l)

echo "SUMMARY:" | tee -a $LOGFILE
echo "  Files created: 10" | tee -a $LOGFILE
echo "  Total MDT_TIMING entries: $total_timing" | tee -a $LOGFILE
echo "  OPEN_FILE_OP operations: $open_file_ops" | tee -a $LOGFILE
echo "  Total OPEN operations: $open_ops" | tee -a $LOGFILE
echo "  CREATE operations: $create_ops" | tee -a $LOGFILE

echo "" | tee -a $LOGFILE
echo "VERIFICATION:" | tee -a $LOGFILE

if [ $total_timing -gt 0 ]; then
    echo "✓ SUCCESS: MDT timing logging is working!" | tee -a $LOGFILE
    
    if [ $open_file_ops -gt 0 ]; then
        echo "✓ EXCELLENT: OPEN_FILE_OP operations detected!" | tee -a $LOGFILE
        echo "  Expected: ~10 OPEN_FILE_OP operations (one per file)" | tee -a $LOGFILE
        echo "  Actual: $open_file_ops OPEN_FILE_OP operations" | tee -a $LOGFILE
        
        if [ $open_file_ops -eq 10 ]; then
            echo "✓ PERFECT: Exactly 10 OPEN_FILE_OP operations as expected!" | tee -a $LOGFILE
        elif [ $open_file_ops -lt 10 ]; then
            echo "ℹ INFO: Fewer OPEN_FILE_OP operations than expected." | tee -a $LOGFILE
            echo "  This might be normal due to caching or operation batching." | tee -a $LOGFILE
        else
            echo "ℹ INFO: More OPEN_FILE_OP operations than expected." | tee -a $LOGFILE
            echo "  This might be due to multiple open/close cycles per file." | tee -a $LOGFILE
        fi
    else
        echo "⚠ WARNING: No OPEN_FILE_OP operations found!" | tee -a $LOGFILE
        
        if [ $open_ops -gt 0 ]; then
            echo "  But $open_ops general OPEN operations were found." | tee -a $LOGFILE
            echo "  Check if OPEN operation classification is working correctly." | tee -a $LOGFILE
        else
            echo "  No OPEN operations found at all." | tee -a $LOGFILE
            echo "  Check if OPEN operation logging is enabled." | tee -a $LOGFILE
        fi
    fi
    
    if [ $create_ops -gt 0 ]; then
        echo "ℹ INFO: $create_ops CREATE operations also detected." | tee -a $LOGFILE
    fi
    
else
    echo "✗ FAILED: No MDT_TIMING entries found!" | tee -a $LOGFILE
    echo "  Check if the kernel modifications were applied and loaded correctly." | tee -a $LOGFILE
fi

echo "" | tee -a $LOGFILE
echo "=====================================================================" | tee -a $LOGFILE
echo "Test completed at $(date)" | tee -a $LOGFILE
echo "Results saved to: $LOGFILE"

# Show created files for verification
echo "" | tee -a $LOGFILE
echo "Created files verification:" | tee -a $LOGFILE
ls -la "$TESTDIR"/ | tee -a $LOGFILE

echo ""
echo "To monitor live OPEN_FILE_OP operations:"
echo "  sudo dmesg -w | grep 'MDT_TIMING.*OPEN_FILE_OP'"
echo ""
echo "To see all OPEN operations:"
echo "  sudo dmesg | grep 'MDT_TIMING.*Operation OPEN'"

# Optional: Clean up (uncomment if you want to remove test files)
# echo "Cleaning up test directory..."
# rm -rf "$TESTDIR" 2>/dev/null || true
