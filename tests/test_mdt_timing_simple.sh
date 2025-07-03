#!/bin/bash

TESTDIR="/mnt/lustre/mdt_sequence_test_$(date +%s)"
LOGFILE="/tmp/mdt_sequence_test_$(date +%s).log"
FILE_SIZE="10M"  # Size for each "heavy" file

# Function to capture and display timing data
capture_timing() {
    local test_name=$1
    echo "==== $test_name ====" >> $LOGFILE
    sudo dmesg | grep MDT_TIMING >> $LOGFILE
    echo "Captured timing data for: $test_name"
    sudo dmesg -c > /dev/null  # Clear buffer for next test
}

# Start with clean log
sudo dmesg -c > /dev/null
echo "Starting sequential MDT timing test at $(date)" | tee -a $LOGFILE
echo "Test directory: $TESTDIR" | tee -a $LOGFILE
echo "-------------" | tee -a $LOGFILE

# Test 1: Create top-level directory
echo "Test 1: Creating top-level directory"
mkdir -p $TESTDIR
capture_timing "Create Top Directory"

# Test 2: Create subdirectory
echo "Test 2: Creating subdirectory"
mkdir -p $TESTDIR/subdir
capture_timing "Create Subdirectory"

# Test 3: Create 3 heavy files with content
echo "Test 3: Creating 3 heavy files"
for i in {1..3}; do
    echo "Creating heavy file $i"
    dd if=/dev/urandom of=$TESTDIR/subdir/heavy_file_$i bs=1M count=${FILE_SIZE%M} status=none
done
capture_timing "Create Heavy Files"

# Test 4: Set extended attributes
echo "Test 4: Setting extended attributes"
for i in {1..3}; do
    setfattr -n user.test_attr -v "This is a test attribute value $i" $TESTDIR/subdir/heavy_file_$i
done
capture_timing "Set Extended Attributes"

# Test 5: Change permissions/ownership
echo "Test 5: Changing permissions and ownership"
chmod 755 $TESTDIR/subdir/heavy_file_1
chmod 644 $TESTDIR/subdir/heavy_file_2
chmod 600 $TESTDIR/subdir/heavy_file_3
chown nobody $TESTDIR/subdir/heavy_file_1
capture_timing "Change Permissions and Ownership"

# Test 6: Rename files
echo "Test 6: Renaming files"
mv $TESTDIR/subdir/heavy_file_1 $TESTDIR/subdir/heavy_file_1_renamed
mv $TESTDIR/subdir/heavy_file_2 $TESTDIR/subdir/heavy_file_2_renamed
capture_timing "Rename Files"

# Test 7: Create hard links
echo "Test 7: Creating hard links"
ln $TESTDIR/subdir/heavy_file_3 $TESTDIR/heavy_file_3_link
ln $TESTDIR/subdir/heavy_file_1_renamed $TESTDIR/heavy_file_1_link
capture_timing "Create Hard Links"

# Test 8: Read files
echo "Test 8: Reading files"
cat $TESTDIR/subdir/heavy_file_1_renamed > /dev/null
cat $TESTDIR/subdir/heavy_file_2_renamed > /dev/null
cat $TESTDIR/subdir/heavy_file_3 > /dev/null
capture_timing "Read Files"

# Test 9: Truncate files
echo "Test 9: Truncating files"
truncate -s 5M $TESTDIR/subdir/heavy_file_1_renamed
truncate -s 2M $TESTDIR/subdir/heavy_file_2_renamed
capture_timing "Truncate Files"

# Test 10: Move files between directories
echo "Test 10: Moving files between directories"
mv $TESTDIR/subdir/heavy_file_3 $TESTDIR/
capture_timing "Move Files Between Directories"

# Test 11: Delete files
echo "Test 11: Deleting files"
rm $TESTDIR/heavy_file_3
rm $TESTDIR/heavy_file_3_link
rm $TESTDIR/heavy_file_1_link
capture_timing "Delete Files"

# Test 12: Delete directory structure
echo "Test 12: Deleting directory structure"
rm -rf $TESTDIR/subdir
rm -rf $TESTDIR
capture_timing "Delete Directory Structure"

echo "-------------" | tee -a $LOGFILE
echo "Tests complete!" | tee -a $LOGFILE
echo "Log saved to $LOGFILE" | tee -a $LOGFILE

echo "You can analyze the results with: ./plot_mdt_timing.py $LOGFILE"
