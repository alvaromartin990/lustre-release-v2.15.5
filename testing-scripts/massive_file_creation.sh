#!/bin/bash

TESTDIR="/mnt/lustre/mdt_timing_bulk_test_$(date +%s)"
LOGFILE="/tmp/mdt_timing_bulk_test_$(date +%s).log"
DMESG_SNAPSHOT="/tmp/dmesg_snapshot_$(date +%s).log"

# Number of files to create for bulk testing
NUM_FILES=100

# Clear kernel log and take initial snapshot
sudo dmesg -c > /dev/null
echo "Cleared kernel log buffer"

echo "Starting MDT bulk timing tests at $(date)" | tee -a $LOGFILE
echo "Test directory: $TESTDIR" | tee -a $LOGFILE
echo "Number of files to create: $NUM_FILES" | tee -a $LOGFILE
echo "-------------" | tee -a $LOGFILE

# Create test directory
mkdir -p $TESTDIR
echo "Created test directory: $TESTDIR"

# Function to capture and analyze dmesg without clearing it
capture_mdt_timing() {
    local test_name="$1"
    local operation="$2"
    echo "Capturing MDT timing for: $test_name" | tee -a $LOGFILE
    
    # Capture current dmesg and filter for MDT_TIMING
    sudo dmesg | grep "MDT_TIMING.*$operation" | tail -20 | tee -a $LOGFILE
    
    # Count the number of timing entries for this operation
    local count=$(sudo dmesg | grep "MDT_TIMING.*$operation" | wc -l)
    echo "Found $count MDT_TIMING entries for $operation" | tee -a $LOGFILE
}

# Test 1: Bulk file creation (CREATE) - This is the main focus
echo "=== Test 1: Bulk file creation ($NUM_FILES files) ===" | tee -a $LOGFILE
echo "Starting bulk file creation at $(date)" | tee -a $LOGFILE

# Measure wall-clock time for the operation
start_time=$(date +%s.%N)

# Create files in batches to monitor timing more granularly
batch_size=20
for ((i=1; i<=NUM_FILES; i++)); do
    touch $TESTDIR/file_$i
    
    # Every batch_size files, capture timing data
    if (( i % batch_size == 0 )); then
        echo "Created $i files so far..." | tee -a $LOGFILE
        capture_mdt_timing "Batch at $i files" "CREATE"
        echo "---" | tee -a $LOGFILE
    fi
done

end_time=$(date +%s.%N)
duration=$(echo "$end_time - $start_time" | bc)
echo "Bulk file creation completed at $(date)" | tee -a $LOGFILE
echo "Wall-clock time for $NUM_FILES file creation: ${duration} seconds" | tee -a $LOGFILE

# Final capture of all CREATE operations
echo "=== Final CREATE timing summary ===" | tee -a $LOGFILE
capture_mdt_timing "All CREATE operations" "CREATE"

# Test 2: Create a single subdirectory for organization
echo "=== Test 2: Single directory creation ===" | tee -a $LOGFILE
mkdir -p $TESTDIR/subdir
capture_mdt_timing "Directory creation" "CREATE"

# Test 3: Attribute changes on a subset of files (to avoid overwhelming the log)
echo "=== Test 3: Attribute changes (subset of files) ===" | tee -a $LOGFILE
for i in {1..10}; do
    chmod 777 $TESTDIR/file_$i
done
capture_mdt_timing "Attribute changes" "SETATTR"

# Test 4: Rename operations on a subset
echo "=== Test 4: Rename operations (subset of files) ===" | tee -a $LOGFILE
for i in {1..5}; do
    mv $TESTDIR/file_$i $TESTDIR/file_${i}_renamed
done
capture_mdt_timing "Rename operations" "RENAME"

# Test 5: Hard links on a subset
echo "=== Test 5: Hard links (subset of files) ===" | tee -a $LOGFILE
for i in {11..15}; do
    ln $TESTDIR/file_$i $TESTDIR/file_${i}_link
done
capture_mdt_timing "Hard links" "LINK"

# Test 6: Extended attributes on a subset
echo "=== Test 6: Extended attributes (subset of files) ===" | tee -a $LOGFILE
for i in {16..20}; do
    setfattr -n user.test -v "test_value_$i" $TESTDIR/file_$i 2>/dev/null || echo "setfattr failed for file_$i"
done
capture_mdt_timing "Extended attributes" "SETXATTR"

# Test 7: File deletion on a subset
echo "=== Test 7: File deletion (subset of files) ===" | tee -a $LOGFILE
for i in {21..30}; do
    rm $TESTDIR/file_$i
done
capture_mdt_timing "File deletion" "UNLINK"

# Test 8: Check if kernel log buffer is full or if entries are being lost
echo "=== Test 8: Kernel log buffer analysis ===" | tee -a $LOGFILE
total_mdt_entries=$(sudo dmesg | grep MDT_TIMING | wc -l)
echo "Total MDT_TIMING entries in kernel log: $total_mdt_entries" | tee -a $LOGFILE

# Check dmesg buffer size and current usage
dmesg_buffer_info=$(sudo dmesg -s)
echo "Kernel log buffer status:" | tee -a $LOGFILE
echo "Buffer size: $(cat /proc/sys/kernel/printk_ratelimit)" | tee -a $LOGFILE

# Test 9: Rapid file creation to test log saturation
echo "=== Test 9: Rapid file creation (testing log saturation) ===" | tee -a $LOGFILE
rapid_start_time=$(date +%s.%N)

# Create 50 more files rapidly without intermediate checks
for i in {201..250}; do
    touch $TESTDIR/rapid_file_$i
done

rapid_end_time=$(date +%s.%N)
rapid_duration=$(echo "$rapid_end_time - $rapid_start_time" | bc)
echo "Rapid creation of 50 files took: ${rapid_duration} seconds" | tee -a $LOGFILE

# Check how many timing entries we captured for rapid creation
rapid_entries=$(sudo dmesg | grep "MDT_TIMING.*CREATE" | tail -50 | wc -l)
echo "Captured $rapid_entries timing entries for rapid creation (expected: 50)" | tee -a $LOGFILE

if [ $rapid_entries -lt 50 ]; then
    echo "WARNING: Missing timing entries! This suggests kernel log buffer overflow or rate limiting." | tee -a $LOGFILE
else
    echo "All timing entries captured successfully." | tee -a $LOGFILE
fi

echo "=== FINAL SUMMARY ===" | tee -a $LOGFILE
echo "Tests complete at $(date)" | tee -a $LOGFILE
echo "Total files created: $((NUM_FILES + 50))" | tee -a $LOGFILE
echo "All timing data captured in dmesg:" | tee -a $LOGFILE

# Save complete dmesg output for analysis
sudo dmesg > $DMESG_SNAPSHOT
echo "Complete kernel log saved to: $DMESG_SNAPSHOT" | tee -a $LOGFILE

# Summary statistics
create_count=$(sudo dmesg | grep "MDT_TIMING.*CREATE" | wc -l)
setattr_count=$(sudo dmesg | grep "MDT_TIMING.*SETATTR" | wc -l)
rename_count=$(sudo dmesg | grep "MDT_TIMING.*RENAME" | wc -l)
link_count=$(sudo dmesg | grep "MDT_TIMING.*LINK" | wc -l)
unlink_count=$(sudo dmesg | grep "MDT_TIMING.*UNLINK" | wc -l)

echo "Final timing entry counts:" | tee -a $LOGFILE
echo "  CREATE: $create_count" | tee -a $LOGFILE
echo "  SETATTR: $setattr_count" | tee -a $LOGFILE
echo "  RENAME: $rename_count" | tee -a $LOGFILE
echo "  LINK: $link_count" | tee -a $LOGFILE
echo "  UNLINK: $unlink_count" | tee -a $LOGFILE

echo "Results saved to: $LOGFILE"
echo "Complete dmesg saved to: $DMESG_SNAPSHOT"
echo "Use the Python plotting script with: python3 plot_mdt_timing.py $DMESG_SNAPSHOT"

# Optional: Clean up test directory (comment out if you want to keep it)
# echo "Cleaning up test directory..."
# rm -rf $TESTDIR
