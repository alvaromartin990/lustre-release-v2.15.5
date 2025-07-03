#!/bin/bash

# Simple Rename Operations Test Script for MDT Timing
# Tests directory and file rename operations to capture RENAME timing data

TESTDIR="/mnt/lustre/rename_test_$(date +%s)"
LOGFILE="/tmp/rename_test_$(date +%s).log"

echo "========================================================================" | tee -a $LOGFILE
echo "=== Simple Rename Operations Test for MDT Timing ===" | tee -a $LOGFILE
echo "========================================================================" | tee -a $LOGFILE
echo "Started at $(date)" | tee -a $LOGFILE
echo "Test directory: $TESTDIR" | tee -a $LOGFILE
echo "" | tee -a $LOGFILE

# Function to show kernel log analysis
show_analysis() {
    local step_name="$1"
    echo "" | tee -a $LOGFILE
    echo "=== $step_name ===" | tee -a $LOGFILE
    
    # Count all MDT_TIMING entries
    local total_timing=$(sudo dmesg | grep "MDT_TIMING" | wc -l)
    echo "Total MDT_TIMING entries: $total_timing" | tee -a $LOGFILE
    
    # Count specific operation types
    local create_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation CREATE" | wc -l)
    local open_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation OPEN" | wc -l)
    local rename_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation RENAME" | wc -l)
    
    echo "Operation breakdown:" | tee -a $LOGFILE
    echo "  CREATE operations: $create_ops" | tee -a $LOGFILE
    echo "  OPEN operations: $open_ops" | tee -a $LOGFILE
    echo "  RENAME operations: $rename_ops ← FOCUS" | tee -a $LOGFILE
    
    # Show recent RENAME operations specifically
    if [ $rename_ops -gt 0 ]; then
        echo "" | tee -a $LOGFILE
        echo "Recent RENAME operations:" | tee -a $LOGFILE
        sudo dmesg | grep "MDT_TIMING.*Operation RENAME" | tail -5 | while read line; do
            echo "  $line" | tee -a $LOGFILE
        done
    fi
    
    echo "---" | tee -a $LOGFILE
}

echo "This script will create directories and files, then test rename operations."
echo ""

# Clear kernel log
echo "Step 1: Clearing kernel log buffer..." | tee -a $LOGFILE
sudo dmesg -c > /dev/null
echo "Kernel log cleared." | tee -a $LOGFILE

# Create test structure
echo "" | tee -a $LOGFILE
echo "Step 2: Creating initial test structure..." | tee -a $LOGFILE

# Create root test directory
mkdir -p "$TESTDIR"
echo "Created root directory: $TESTDIR" | tee -a $LOGFILE

# Create some subdirectories
mkdir -p "$TESTDIR/dir1"
mkdir -p "$TESTDIR/dir2"
mkdir -p "$TESTDIR/subdir/nested1"
mkdir -p "$TESTDIR/subdir/nested2"
echo "Created initial directories" | tee -a $LOGFILE

# Create some files
touch "$TESTDIR/file1.txt"
touch "$TESTDIR/file2.txt"
touch "$TESTDIR/dir1/file_in_dir1.txt"
touch "$TESTDIR/dir2/file_in_dir2.txt"
touch "$TESTDIR/subdir/nested_file.txt"
echo "Created initial files" | tee -a $LOGFILE

# Add some content to files
echo "Initial content for file1" > "$TESTDIR/file1.txt"
echo "Initial content for file2" > "$TESTDIR/file2.txt"
echo "Content in dir1 file" > "$TESTDIR/dir1/file_in_dir1.txt"
echo "Content in dir2 file" > "$TESTDIR/dir2/file_in_dir2.txt"
echo "Nested file content" > "$TESTDIR/subdir/nested_file.txt"

echo "Test structure created successfully." | tee -a $LOGFILE

show_analysis "After Initial Structure Creation"

# Start rename operations
echo "" | tee -a $LOGFILE
echo "Step 3: Testing FILE rename operations..." | tee -a $LOGFILE

# Rename files
echo "Renaming file1.txt to renamed_file1.txt" | tee -a $LOGFILE
mv "$TESTDIR/file1.txt" "$TESTDIR/renamed_file1.txt"
sleep 0.1

echo "Renaming file2.txt to renamed_file2.txt" | tee -a $LOGFILE
mv "$TESTDIR/file2.txt" "$TESTDIR/renamed_file2.txt"
sleep 0.1

echo "Renaming file in dir1" | tee -a $LOGFILE
mv "$TESTDIR/dir1/file_in_dir1.txt" "$TESTDIR/dir1/renamed_file_in_dir1.txt"
sleep 0.1

echo "Renaming file in dir2" | tee -a $LOGFILE
mv "$TESTDIR/dir2/file_in_dir2.txt" "$TESTDIR/dir2/renamed_file_in_dir2.txt"
sleep 0.1

echo "Renaming nested file" | tee -a $LOGFILE
mv "$TESTDIR/subdir/nested_file.txt" "$TESTDIR/subdir/renamed_nested_file.txt"
sleep 0.1

echo "File renames completed." | tee -a $LOGFILE

show_analysis "After File Renames"

# Test directory renames
echo "" | tee -a $LOGFILE
echo "Step 4: Testing DIRECTORY rename operations..." | tee -a $LOGFILE

echo "Renaming dir1 to renamed_dir1" | tee -a $LOGFILE
mv "$TESTDIR/dir1" "$TESTDIR/renamed_dir1"
sleep 0.1

echo "Renaming dir2 to renamed_dir2" | tee -a $LOGFILE
mv "$TESTDIR/dir2" "$TESTDIR/renamed_dir2"
sleep 0.1

echo "Renaming nested1 to renamed_nested1" | tee -a $LOGFILE
mv "$TESTDIR/subdir/nested1" "$TESTDIR/subdir/renamed_nested1"
sleep 0.1

echo "Renaming nested2 to renamed_nested2" | tee -a $LOGFILE
mv "$TESTDIR/subdir/nested2" "$TESTDIR/subdir/renamed_nested2"
sleep 0.1

echo "Renaming subdir to renamed_subdir" | tee -a $LOGFILE
mv "$TESTDIR/subdir" "$TESTDIR/renamed_subdir"
sleep 0.1

echo "Directory renames completed." | tee -a $LOGFILE

show_analysis "After Directory Renames"

# Test cross-directory moves (which are also renames at the filesystem level)
echo "" | tee -a $LOGFILE
echo "Step 5: Testing cross-directory MOVE operations (also RENAME)..." | tee -a $LOGFILE

echo "Moving renamed_file1.txt into renamed_dir1" | tee -a $LOGFILE
mv "$TESTDIR/renamed_file1.txt" "$TESTDIR/renamed_dir1/moved_file1.txt"
sleep 0.1

echo "Moving renamed_file2.txt into renamed_dir2" | tee -a $LOGFILE
mv "$TESTDIR/renamed_file2.txt" "$TESTDIR/renamed_dir2/moved_file2.txt"
sleep 0.1

echo "Cross-directory moves completed." | tee -a $LOGFILE

show_analysis "After Cross-Directory Moves"

# Final analysis
echo "" | tee -a $LOGFILE
echo "========================================================================" | tee -a $LOGFILE
echo "=== FINAL RESULTS ===" | tee -a $LOGFILE
echo "========================================================================" | tee -a $LOGFILE

# Count final results
local total_timing=$(sudo dmesg | grep "MDT_TIMING" | wc -l)
local rename_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation RENAME" | wc -l)
local create_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation CREATE" | wc -l)
local open_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation OPEN" | wc -l)

echo "OPERATION SUMMARY:" | tee -a $LOGFILE
echo "  Total rename operations performed:" | tee -a $LOGFILE
echo "    File renames: 5" | tee -a $LOGFILE
echo "    Directory renames: 5" | tee -a $LOGFILE
echo "    Cross-directory moves: 2" | tee -a $LOGFILE
echo "    Expected total RENAME operations: 12" | tee -a $LOGFILE
echo "" | tee -a $LOGFILE
echo "  MDT Operations Captured:" | tee -a $LOGFILE
echo "    Total MDT_TIMING entries: $total_timing" | tee -a $LOGFILE
echo "    RENAME operations: $rename_ops ← KEY METRIC" | tee -a $LOGFILE
echo "    CREATE operations: $create_ops" | tee -a $LOGFILE
echo "    OPEN operations: $open_ops" | tee -a $LOGFILE

echo "" | tee -a $LOGFILE
echo "VERIFICATION:" | tee -a $LOGFILE

if [ $total_timing -gt 0 ]; then
    echo "✓ SUCCESS: MDT timing logging is working!" | tee -a $LOGFILE
    
    if [ $rename_ops -gt 0 ]; then
        echo "✓ EXCELLENT: RENAME operations detected!" | tee -a $LOGFILE
        echo "  Expected: ~12 RENAME operations" | tee -a $LOGFILE
        echo "  Actual: $rename_ops RENAME operations" | tee -a $LOGFILE
        
        if [ $rename_ops -ge 10 ]; then
            echo "✓ GREAT: Good number of RENAME operations captured!" | tee -a $LOGFILE
        elif [ $rename_ops -ge 5 ]; then
            echo "✓ GOOD: Some RENAME operations captured!" | tee -a $LOGFILE
        else
            echo "ℹ INFO: Few RENAME operations captured." | tee -a $LOGFILE
            echo "  This might be due to operation batching or caching." | tee -a $LOGFILE
        fi
    else
        echo "⚠ WARNING: No RENAME operations detected!" | tee -a $LOGFILE
        echo "  Check if RENAME operation logging is working correctly." | tee -a $LOGFILE
    fi
else
    echo "✗ FAILED: No MDT_TIMING entries found!" | tee -a $LOGFILE
    echo "  Check if the kernel modifications were applied and loaded correctly." | tee -a $LOGFILE
fi

echo "" | tee -a $LOGFILE
echo "ALL RENAME OPERATIONS CAPTURED:" | tee -a $LOGFILE
if [ $rename_ops -gt 0 ]; then
    sudo dmesg | grep "MDT_TIMING.*Operation RENAME" | tee -a $LOGFILE
else
    echo "No RENAME operations found in kernel log." | tee -a $LOGFILE
fi

echo "" | tee -a $LOGFILE
echo "FINAL DIRECTORY STRUCTURE:" | tee -a $LOGFILE
echo "Contents of test directory after all renames:" | tee -a $LOGFILE
find "$TESTDIR" -type f -o -type d | sort | tee -a $LOGFILE

echo "" | tee -a $LOGFILE
echo "========================================================================" | tee -a $LOGFILE
echo "Test completed at $(date)" | tee -a $LOGFILE
echo "Results saved to: $LOGFILE"
echo "========================================================================" | tee -a $LOGFILE

# Clean up
echo "" | tee -a $LOGFILE
echo "Cleaning up test directory..." | tee -a $LOGFILE
rm -rf "$TESTDIR" 2>/dev/null || true
echo "Cleanup completed." | tee -a $LOGFILE

echo ""
echo "=== MONITORING COMMANDS ==="
echo "To see RENAME operations specifically:"
echo "  sudo dmesg | grep 'MDT_TIMING.*Operation RENAME'"
echo ""
echo "To monitor live RENAME operations:"
echo "  sudo dmesg -w | grep 'MDT_TIMING.*RENAME'"
echo ""
echo "To see all timing operations:"
echo "  sudo dmesg | grep 'MDT_TIMING'"
