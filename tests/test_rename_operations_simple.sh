#!/bin/bash

# Super Simple Rename Test for MDT Timing
# Creates 1 directory with 5 files, renames all files, then renames the directory

TESTDIR="/mnt/lustre/simple_rename_$(date +%s)"
LOGFILE="/tmp/simple_rename_$(date +%s).log"

echo "======================================================" | tee -a $LOGFILE
echo "=== Super Simple Rename Test for MDT Timing ===" | tee -a $LOGFILE
echo "======================================================" | tee -a $LOGFILE
echo "Started at $(date)" | tee -a $LOGFILE
echo "Test directory: $TESTDIR" | tee -a $LOGFILE
echo "" | tee -a $LOGFILE

# Function to show analysis
show_analysis() {
    local step_name="$1"
    echo "" | tee -a $LOGFILE
    echo "=== $step_name ===" | tee -a $LOGFILE
    
    local total_timing=$(sudo dmesg | grep "MDT_TIMING" | wc -l)
    local rename_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation RENAME" | wc -l)
    local create_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation CREATE" | wc -l)
    local open_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation OPEN" | wc -l)
    
    echo "Total MDT_TIMING entries: $total_timing" | tee -a $LOGFILE
    echo "CREATE operations: $create_ops" | tee -a $LOGFILE
    echo "OPEN operations: $open_ops" | tee -a $LOGFILE
    echo "RENAME operations: $rename_ops ←" | tee -a $LOGFILE
    echo "---" | tee -a $LOGFILE
}

# Clear kernel log
echo "Step 1: Clearing kernel log..." | tee -a $LOGFILE
sudo dmesg -c > /dev/null

# Create directory and files
echo "" | tee -a $LOGFILE
echo "Step 2: Creating test directory and 5 files..." | tee -a $LOGFILE

mkdir -p "$TESTDIR"
echo "Created directory: $TESTDIR" | tee -a $LOGFILE

# Create 5 files
for i in {1..5}; do
    filename="$TESTDIR/file${i}.txt"
    touch "$filename"
    # echo "Content for file $i" > "$filename"
    echo "Created: file${i}.txt" | tee -a $LOGFILE
    sleep 0.1
done

show_analysis "After Creating Directory and Files"

# Rename all 5 files
echo "" | tee -a $LOGFILE
echo "Step 3: Renaming all 5 files..." | tee -a $LOGFILE

for i in {1..5}; do
    old_name="$TESTDIR/file${i}.txt"
    new_name="$TESTDIR/renamed_file${i}.txt"
    mv "$old_name" "$new_name"
    echo "Renamed: file${i}.txt → renamed_file${i}.txt" | tee -a $LOGFILE
    sleep 0.1
done

show_analysis "After Renaming All Files"

# Rename the directory
echo "" | tee -a $LOGFILE
echo "Step 4: Renaming the directory..." | tee -a $LOGFILE

old_dir="$TESTDIR"
new_dir="${TESTDIR}_renamed"
mv "$old_dir" "$new_dir"
echo "Renamed directory: $(basename $old_dir) → $(basename $new_dir)" | tee -a $LOGFILE

# Update TESTDIR variable for cleanup
TESTDIR="$new_dir"

show_analysis "After Renaming Directory"

# Final results
echo "" | tee -a $LOGFILE
echo "======================================================" | tee -a $LOGFILE
echo "=== FINAL RESULTS ===" | tee -a $LOGFILE
echo "======================================================" | tee -a $LOGFILE

local total_timing=$(sudo dmesg | grep "MDT_TIMING" | wc -l)
local rename_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation RENAME" | wc -l)

echo "WHAT WE DID:" | tee -a $LOGFILE
echo "  Created: 1 directory" | tee -a $LOGFILE
echo "  Created: 5 files" | tee -a $LOGFILE
echo "  Renamed: 5 files" | tee -a $LOGFILE
echo "  Renamed: 1 directory" | tee -a $LOGFILE
echo "  Expected RENAME operations: 6 (5 files + 1 directory)" | tee -a $LOGFILE
echo "" | tee -a $LOGFILE

echo "RESULTS:" | tee -a $LOGFILE
echo "  Total MDT_TIMING entries: $total_timing" | tee -a $LOGFILE
echo "  RENAME operations captured: $rename_ops" | tee -a $LOGFILE

if [ $rename_ops -gt 0 ]; then
    echo "✓ SUCCESS: RENAME operations detected!" | tee -a $LOGFILE
    if [ $rename_ops -eq 6 ]; then
        echo "✓ PERFECT: Exactly 6 RENAME operations as expected!" | tee -a $LOGFILE
    else
        echo "ℹ INFO: $rename_ops RENAME operations (expected 6)" | tee -a $LOGFILE
    fi
else
    echo "⚠ WARNING: No RENAME operations detected!" | tee -a $LOGFILE
fi

echo "" | tee -a $LOGFILE
echo "ALL RENAME OPERATIONS:" | tee -a $LOGFILE
sudo dmesg | grep "MDT_TIMING.*Operation RENAME" | tee -a $LOGFILE

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
echo "To see RENAME operations: sudo dmesg | grep 'MDT_TIMING.*RENAME'"
