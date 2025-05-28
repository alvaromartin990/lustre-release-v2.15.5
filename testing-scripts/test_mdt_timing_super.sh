#!/bin/bash

TESTDIR="/mnt/lustre/mdt_heavy_test_$(date +%s)"
LOGFILE="/tmp/mdt_heavy_test_$(date +%s).log"
TIMINGFILE="/tmp/mdt_heavy_timing_$(date +%s).log"
FILE_SIZE="10M"  # Size for each "heavy" file
NUM_FILES=20

# Function to capture timing data
capture_timing() {
    local test_name=$1
    echo "Capturing timing data for: $test_name"
    sudo dmesg | grep MDT_TIMING >> $TIMINGFILE
    echo "Test: $test_name" >> $LOGFILE
    sudo dmesg | grep MDT_TIMING >> $LOGFILE
    sudo dmesg -c > /dev/null  # Clear buffer for next test
}

# Start with clean log
sudo dmesg -c > /dev/null
echo "Starting heavy file MDT timing tests at $(date)" | tee -a $LOGFILE
echo "Test directory: $TESTDIR" | tee -a $LOGFILE
echo "File size: $FILE_SIZE" | tee -a $LOGFILE
echo "Number of files: $NUM_FILES" | tee -a $LOGFILE
echo "-------------" | tee -a $LOGFILE

# Create test directory
mkdir -p $TESTDIR
echo "Created test directory"

# Test 1: Create heavy files with content
echo "Test 1: Creating $NUM_FILES heavy files with content..."
for i in $(seq 1 $NUM_FILES); do
    echo "Creating file $i of $NUM_FILES"
    dd if=/dev/urandom of=$TESTDIR/heavy_file_$i bs=1M count=${FILE_SIZE%M} status=none
done
capture_timing "Create Heavy Files"

# Test 2: Set extended attributes on files
echo "Test 2: Setting extended attributes..."
for i in $(seq 1 $NUM_FILES); do
    echo "Setting xattr on file $i of $NUM_FILES"
    setfattr -n user.test_attr1 -v "This is a test attribute with a fairly long value to make it more complex" $TESTDIR/heavy_file_$i
    setfattr -n user.test_attr2 -v "Another test attribute with different content $(date)" $TESTDIR/heavy_file_$i
    setfattr -n user.test_attr3 -v "A third attribute with unique content $RANDOM" $TESTDIR/heavy_file_$i
done
capture_timing "Set Extended Attributes"

# Test 3: Change permissions/attributes
echo "Test 3: Changing file attributes..."
for i in $(seq 1 $NUM_FILES); do
    echo "Changing attributes on file $i of $NUM_FILES"
    chmod 755 $TESTDIR/heavy_file_$i
    touch -m -d "2023-01-0$((i % 9 + 1)) 12:00:00" $TESTDIR/heavy_file_$i
    chown nobody $TESTDIR/heavy_file_$i
done
capture_timing "Change Attributes"

# Test 4: Rename files
echo "Test 4: Renaming files..."
for i in $(seq 1 $NUM_FILES); do
    echo "Renaming file $i of $NUM_FILES"
    mv $TESTDIR/heavy_file_$i $TESTDIR/heavy_file_${i}_renamed
done
capture_timing "Rename Files"

# Test 5: Create hard links
echo "Test 5: Creating hard links..."
for i in $(seq 1 $NUM_FILES); do
    echo "Creating hard link for file $i of $NUM_FILES"
    ln $TESTDIR/heavy_file_${i}_renamed $TESTDIR/heavy_file_${i}_hardlink
done
capture_timing "Create Hard Links"

# Test 6: Open and read files
echo "Test 6: Opening and reading files..."
for i in $(seq 1 $NUM_FILES); do
    echo "Reading file $i of $NUM_FILES"
    cat $TESTDIR/heavy_file_${i}_renamed > /dev/null
done
capture_timing "Open and Read Files"

# Test 7: Truncate files (modify size)
echo "Test 7: Truncating files..."
for i in $(seq 1 $NUM_FILES); do
    echo "Truncating file $i of $NUM_FILES"
    truncate -s 5M $TESTDIR/heavy_file_${i}_renamed
done
capture_timing "Truncate Files"

# Test 8: Delete files
echo "Test 8: Deleting files..."
for i in $(seq 1 $((NUM_FILES/2))); do
    echo "Deleting file $i of $NUM_FILES/2"
    rm $TESTDIR/heavy_file_${i}_renamed
done
capture_timing "Delete Files"

# Test 9: Delete hard links
echo "Test 9: Deleting hard links..."
for i in $(seq 1 $NUM_FILES); do
    echo "Deleting hard link $i of $NUM_FILES"
    if [ -f "$TESTDIR/heavy_file_${i}_hardlink" ]; then
        rm $TESTDIR/heavy_file_${i}_hardlink
    fi
done
capture_timing "Delete Hard Links"

echo "-------------" | tee -a $LOGFILE
echo "Tests complete!" | tee -a $LOGFILE
echo "Full timing data saved to $TIMINGFILE" | tee -a $LOGFILE
echo "Log saved to $LOGFILE" | tee -a $LOGFILE
