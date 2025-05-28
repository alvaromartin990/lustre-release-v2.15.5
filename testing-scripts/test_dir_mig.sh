#!/bin/bash

# Test script for MDT operations: single file creation, MIGRATE, and RESYNC
# Tests with multi-MDT setup (MDT0000 and MDT0001)

TESTDIR="/mnt/lustre/mdt_ops_test_$(date +%s)"
LOGFILE="/tmp/mdt_ops_test_$(date +%s).log"

echo "=== MDT Operations Test ===" | tee -a $LOGFILE
echo "Started at $(date)" | tee -a $LOGFILE
echo "Test directory: $TESTDIR" | tee -a $LOGFILE
echo "Testing: Single file creation, MIGRATE, and RESYNC operations" | tee -a $LOGFILE
echo "-------------" | tee -a $LOGFILE

# Function to show operation counts
show_operation_counts() {
    local test_name="$1"
    local create_count=$(sudo dmesg | grep "MDT_TIMING.*Operation CREATE" | wc -l)
    local open_count=$(sudo dmesg | grep "MDT_TIMING.*Operation OPEN" | wc -l)
    local migrate_count=$(sudo dmesg | grep "MDT_TIMING.*Operation MIGRATE" | wc -l)
    local resync_count=$(sudo dmesg | grep "MDT_TIMING.*Operation RESYNC" | wc -l)
    local setattr_count=$(sudo dmesg | grep "MDT_TIMING.*Operation SETATTR" | wc -l)
    
    echo "$test_name:" | tee -a $LOGFILE
    echo "  CREATE operations: $create_count" | tee -a $LOGFILE
    echo "  OPEN operations: $open_count" | tee -a $LOGFILE
    echo "  MIGRATE operations: $migrate_count" | tee -a $LOGFILE
    echo "  RESYNC operations: $resync_count" | tee -a $LOGFILE
    echo "  SETATTR operations: $setattr_count" | tee -a $LOGFILE
    echo "---" | tee -a $LOGFILE
}

# Function to get MDT index for a file
get_mdt_index() {
    local file="$1"
    lfs getstripe -m "$file" 2>/dev/null
}

# Clear kernel log
sudo dmesg -c > /dev/null
echo "Cleared kernel log buffer" | tee -a $LOGFILE

# Show initial filesystem status
echo "=== Filesystem Status ===" | tee -a $LOGFILE
lfs df -h | tee -a $LOGFILE
echo "-------------" | tee -a $LOGFILE

echo "=== TEST 4: Directory Migration ===" | tee -a $LOGFILE
echo "Testing directory migration between MDTs..." | tee -a $LOGFILE

# Create a directory with files
MIGRATE_DIR="$TESTDIR/migrate_dir"
mkdir -p "$MIGRATE_DIR"
echo "Created directory: $MIGRATE_DIR" | tee -a $LOGFILE

# Create files in the directory
for i in {1..3}; do
    echo "File $i content" > "$MIGRATE_DIR/file_$i.txt"
done
echo "Created 3 files in directory" | tee -a $LOGFILE

# Get current MDT of directory
dir_mdt=$(lfs getstripe -m "$MIGRATE_DIR" 2>/dev/null || echo "0")
echo "Directory currently on MDT$dir_mdt" | tee -a $LOGFILE

# Calculate target MDT
if [ "$dir_mdt" -eq "0" ]; then
    target_mdt=1
else
    target_mdt=0
fi

echo "Migrating directory from MDT$dir_mdt to MDT$target_mdt..." | tee -a $LOGFILE
lfs migrate -m $target_mdt "$MIGRATE_DIR" 2>&1 | tee -a $LOGFILE

# Verify directory migration
new_dir_mdt=$(lfs getstripe -m "$MIGRATE_DIR" 2>/dev/null || echo "unknown")
echo "Directory now on MDT$new_dir_mdt" | tee -a $LOGFILE

show_operation_counts "After directory migration"

echo "=== TIMING ANALYSIS ===" | tee -a $LOGFILE
echo "All MDT_TIMING entries:" | tee -a $LOGFILE
sudo dmesg | grep "MDT_TIMING" | tail -30 | tee -a $LOGFILE

echo "-------------" | tee -a $LOGFILE
echo "=== FINAL SUMMARY ===" | tee -a $LOGFILE

# Count final operations
final_create=$(sudo dmesg | grep "MDT_TIMING.*Operation CREATE" | wc -l)
final_open=$(sudo dmesg | grep "MDT_TIMING.*Operation OPEN" | wc -l)
final_migrate=$(sudo dmesg | grep "MDT_TIMING.*Operation MIGRATE" | wc -l)
final_resync=$(sudo dmesg | grep "MDT_TIMING.*Operation RESYNC" | wc -l)
final_setattr=$(sudo dmesg | grep "MDT_TIMING.*Operation SETATTR" | wc -l)

echo "FINAL OPERATION COUNTS:" | tee -a $LOGFILE
echo "  CREATE operations: $final_create" | tee -a $LOGFILE
echo "  OPEN operations: $final_open" | tee -a $LOGFILE
echo "  MIGRATE operations: $final_migrate" | tee -a $LOGFILE
echo "  RESYNC operations: $final_resync" | tee -a $LOGFILE
echo "  SETATTR operations: $final_setattr" | tee -a $LOGFILE

echo "-------------" | tee -a $LOGFILE
echo "PERFORMANCE ANALYSIS:" | tee -a $LOGFILE

# Extract timing for each operation type
if [ $final_migrate -gt 0 ]; then
    avg_migrate_time=$(sudo dmesg | grep "MDT_TIMING.*Operation MIGRATE" | sed 's/.*took \([0-9]*\) microseconds.*/\1/' | awk '{sum+=$1} END {if(NR>0) print sum/NR; else print 0}')
    echo "  Average MIGRATE operation time: ${avg_migrate_time} microseconds" | tee -a $LOGFILE
fi

if [ $final_resync -gt 0 ]; then
    avg_resync_time=$(sudo dmesg | grep "MDT_TIMING.*Operation RESYNC" | sed 's/.*took \([0-9]*\) microseconds.*/\1/' | awk '{sum+=$1} END {if(NR>0) print sum/NR; else print 0}')
    echo "  Average RESYNC operation time: ${avg_resync_time} microseconds" | tee -a $LOGFILE
fi

echo "-------------" | tee -a $LOGFILE
echo "Directory contents:" | tee -a $LOGFILE
ls -la "$TESTDIR"/ | head -20 | tee -a $LOGFILE

echo "-------------" | tee -a $LOGFILE
echo "Test completed!" | tee -a $LOGFILE
echo "Results saved to: $LOGFILE"

# Optional: Clean up (comment out if you want to keep the files)
# echo "Cleaning up test directory..."
# rm -rf "$TESTDIR"

