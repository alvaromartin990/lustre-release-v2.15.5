#!/bin/bash

# Super Simple Rename Test with OPEN_FILE_OP Tracking
# Creates 1 directory with 5 files, renames all files, then renames the directory
# Tracks all simultaneous operations including OPEN_FILE_OP

TESTDIR="/mnt/lustre/simple_rename_$(date +%s)"
LOGFILE="/tmp/simple_rename_$(date +%s).log"

echo "======================================================" | tee -a $LOGFILE
echo "=== Rename Test with OPEN_FILE_OP Tracking ===" | tee -a $LOGFILE
echo "======================================================" | tee -a $LOGFILE
echo "Started at $(date)" | tee -a $LOGFILE
echo "Test directory: $TESTDIR" | tee -a $LOGFILE
echo "" | tee -a $LOGFILE

# Function to show detailed analysis
show_analysis() {
    local step_name="$1"
    echo "" | tee -a $LOGFILE
    echo "=== $step_name ===" | tee -a $LOGFILE
    
    # Count all operation types
    local total_timing=$(sudo dmesg | grep "MDT_TIMING" | wc -l)
    local create_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation CREATE" | wc -l)
    local open_file_ops=$(sudo dmesg | grep "MDT_TIMING.*OPEN_FILE_OP" | wc -l)
    local open_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation OPEN" | wc -l)
    local rename_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation RENAME" | wc -l)
    local setattr_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation SETATTR" | wc -l)
    local link_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation LINK" | wc -l)
    local unlink_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation UNLINK" | wc -l)
    
    echo "OPERATION COUNTS:" | tee -a $LOGFILE
    echo "  Total MDT_TIMING entries: $total_timing" | tee -a $LOGFILE
    echo "  CREATE operations: $create_ops" | tee -a $LOGFILE
    echo "  OPEN_FILE_OP operations: $open_file_ops ← FILE OPERATIONS" | tee -a $LOGFILE
    echo "  Total OPEN operations: $open_ops" | tee -a $LOGFILE
    echo "  RENAME operations: $rename_ops ←" | tee -a $LOGFILE
    echo "  SETATTR operations: $setattr_ops" | tee -a $LOGFILE
    echo "  LINK operations: $link_ops" | tee -a $LOGFILE
    echo "  UNLINK operations: $unlink_ops" | tee -a $LOGFILE
    
    # Show recent operations with timestamps if available
    echo "" | tee -a $LOGFILE
    echo "RECENT OPERATIONS (last 10):" | tee -a $LOGFILE
    sudo dmesg | grep "MDT_TIMING" | tail -10 | while read line; do
        echo "  $line" | tee -a $LOGFILE
    done
    
    # Show simultaneous operation patterns
    echo "" | tee -a $LOGFILE
    echo "SIMULTANEOUS OPERATION ANALYSIS:" | tee -a $LOGFILE
    if [ $open_file_ops -gt 0 ] && [ $create_ops -gt 0 ]; then
        echo "  ✓ File creation triggers both CREATE and OPEN_FILE_OP" | tee -a $LOGFILE
    fi
    if [ $rename_ops -gt 0 ]; then
        echo "  ✓ RENAME operations detected" | tee -a $LOGFILE
    fi
    if [ $setattr_ops -gt 0 ]; then
        echo "  ✓ SETATTR operations (likely from file content writing)" | tee -a $LOGFILE
    fi
    
    echo "---" | tee -a $LOGFILE
}

# Clear kernel log
echo "Step 1: Clearing kernel log..." | tee -a $LOGFILE
sudo dmesg -c > /dev/null
echo "Kernel log cleared." | tee -a $LOGFILE

# Create directory and files
echo "" | tee -a $LOGFILE
echo "Step 2: Creating test directory and 5 files..." | tee -a $LOGFILE

mkdir -p "$TESTDIR"
echo "Created directory: $TESTDIR" | tee -a $LOGFILE
sleep 0.2  # Allow directory creation to be logged

# Create 5 files with content (should trigger both CREATE and OPEN_FILE_OP)
for i in {1..5}; do
    filename="$TESTDIR/file${i}.txt"
    echo "Creating file${i}.txt..." | tee -a $LOGFILE
    
    # Create file (CREATE operation)
    touch "$filename"
    sleep 0.1
    
    # Write content (OPEN_FILE_OP operation)
    # echo "This is content for file number $i created at $(date)" > "$filename"
    sleep 0.1
    
    echo "  → Created and wrote content to file${i}.txt" | tee -a $LOGFILE
done

show_analysis "After Creating Directory and Files"

# Rename all 5 files
echo "" | tee -a $LOGFILE
echo "Step 3: Renaming all 5 files..." | tee -a $LOGFILE

for i in {1..5}; do
    old_name="$TESTDIR/file${i}.txt"
    new_name="$TESTDIR/renamed_file${i}.txt"
    echo "Renaming file${i}.txt → renamed_file${i}.txt" | tee -a $LOGFILE
    mv "$old_name" "$new_name"
    sleep 0.1
done

show_analysis "After Renaming All Files"

# Test reading files (should trigger more OPEN_FILE_OP)
echo "" | tee -a $LOGFILE
echo "Step 4: Reading file contents (testing OPEN_FILE_OP)..." | tee -a $LOGFILE

for i in {1..5}; do
    filename="$TESTDIR/renamed_file${i}.txt"
    echo "Reading renamed_file${i}.txt..." | tee -a $LOGFILE
    content=$(cat "$filename")
    echo "  → Content length: ${#content} characters" | tee -a $LOGFILE
    sleep 0.1
done

show_analysis "After Reading Files"

# Rename the directory
echo "" | tee -a $LOGFILE
echo "Step 5: Renaming the directory..." | tee -a $LOGFILE

old_dir="$TESTDIR"
new_dir="${TESTDIR}_renamed"
echo "Renaming directory: $(basename $old_dir) → $(basename $new_dir)" | tee -a $LOGFILE
mv "$old_dir" "$new_dir"
sleep 0.2

# Update TESTDIR variable for cleanup
TESTDIR="$new_dir"

show_analysis "After Renaming Directory"

# Final comprehensive results
echo "" | tee -a $LOGFILE
echo "======================================================" | tee -a $LOGFILE
echo "=== COMPREHENSIVE FINAL RESULTS ===" | tee -a $LOGFILE
echo "======================================================" | tee -a $LOGFILE

# Get final counts
local total_timing=$(sudo dmesg | grep "MDT_TIMING" | wc -l)
local create_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation CREATE" | wc -l)
local open_file_ops=$(sudo dmesg | grep "MDT_TIMING.*OPEN_FILE_OP" | wc -l)
local open_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation OPEN" | wc -l)
local rename_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation RENAME" | wc -l)
local setattr_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation SETATTR" | wc -l)

echo "WHAT WE DID:" | tee -a $LOGFILE
echo "  Created: 1 directory" | tee -a $LOGFILE
echo "  Created: 5 files" | tee -a $LOGFILE
echo "  Wrote content to: 5 files" | tee -a $LOGFILE
echo "  Read content from: 5 files" | tee -a $LOGFILE
echo "  Renamed: 5 files" | tee -a $LOGFILE
echo "  Renamed: 1 directory" | tee -a $LOGFILE
echo "" | tee -a $LOGFILE

echo "EXPECTED OPERATIONS:" | tee -a $LOGFILE
echo "  CREATE: ~1 (directory creation)" | tee -a $LOGFILE
echo "  OPEN_FILE_OP: ~15 (5 create + 5 write + 5 read)" | tee -a $LOGFILE
echo "  RENAME: ~6 (5 files + 1 directory)" | tee -a $LOGFILE
echo "  SETATTR: ~5+ (from content writing)" | tee -a $LOGFILE
echo "" | tee -a $LOGFILE

echo "ACTUAL RESULTS:" | tee -a $LOGFILE
echo "  Total MDT_TIMING entries: $total_timing" | tee -a $LOGFILE
echo "  CREATE operations: $create_ops" | tee -a $LOGFILE
echo "  OPEN_FILE_OP operations: $open_file_ops" | tee -a $LOGFILE
echo "  Total OPEN operations: $open_ops" | tee -a $LOGFILE
echo "  RENAME operations: $rename_ops" | tee -a $LOGFILE
echo "  SETATTR operations: $setattr_ops" | tee -a $LOGFILE
echo "" | tee -a $LOGFILE

echo "VERIFICATION:" | tee -a $LOGFILE
if [ $total_timing -gt 0 ]; then
    echo "✓ SUCCESS: MDT timing logging is working!" | tee -a $LOGFILE
    
    if [ $open_file_ops -gt 0 ]; then
        echo "✓ EXCELLENT: OPEN_FILE_OP operations detected: $open_file_ops" | tee -a $LOGFILE
        if [ $open_file_ops -ge 10 ]; then
            echo "  → Great! Multiple file operations captured" | tee -a $LOGFILE
        fi
    else
        echo "⚠ WARNING: No OPEN_FILE_OP operations detected!" | tee -a $LOGFILE
    fi
    
    if [ $rename_ops -gt 0 ]; then
        echo "✓ SUCCESS: RENAME operations detected: $rename_ops" | tee -a $LOGFILE
        if [ $rename_ops -eq 6 ]; then
            echo "  → Perfect! Expected number of renames" | tee -a $LOGFILE
        fi
    else
        echo "⚠ WARNING: No RENAME operations detected!" | tee -a $LOGFILE
    fi
    
    if [ $create_ops -gt 0 ]; then
        echo "✓ SUCCESS: CREATE operations detected: $create_ops" | tee -a $LOGFILE
    fi
    
else
    echo "✗ FAILED: No MDT_TIMING entries found!" | tee -a $LOGFILE
fi

echo "" | tee -a $LOGFILE
echo "======================================================" | tee -a $LOGFILE
echo "=== DETAILED OPERATION BREAKDOWN ===" | tee -a $LOGFILE
echo "======================================================" | tee -a $LOGFILE

echo "ALL OPEN_FILE_OP OPERATIONS:" | tee -a $LOGFILE
sudo dmesg | grep "MDT_TIMING.*OPEN_FILE_OP" | tee -a $LOGFILE

echo "" | tee -a $LOGFILE
echo "ALL RENAME OPERATIONS:" | tee -a $LOGFILE
sudo dmesg | grep "MDT_TIMING.*Operation RENAME" | tee -a $LOGFILE

echo "" | tee -a $LOGFILE
echo "ALL CREATE OPERATIONS:" | tee -a $LOGFILE
sudo dmesg | grep "MDT_TIMING.*Operation CREATE" | tee -a $LOGFILE

echo "" | tee -a $LOGFILE
echo "FINAL DIRECTORY CONTENTS:" | tee -a $LOGFILE
ls -la "$TESTDIR" | tee -a $LOGFILE

# Cleanup
echo "" | tee -a $LOGFILE
echo "Cleaning up..." | tee -a $LOGFILE
rm -rf "$TESTDIR"

echo "" | tee -a $LOGFILE
echo "======================================================" | tee -a $LOGFILE
echo "Test completed at $(date)" | tee -a $LOGFILE
echo "Results saved to: $LOGFILE"
echo "======================================================" | tee -a $LOGFILE

echo ""
echo "=== MONITORING COMMANDS ==="
echo "OPEN_FILE_OP operations: sudo dmesg | grep 'MDT_TIMING.*OPEN_FILE_OP'"
echo "RENAME operations: sudo dmesg | grep 'MDT_TIMING.*RENAME'"
echo "All operations: sudo dmesg | grep 'MDT_TIMING'"
