#!/bin/bash

TESTDIR="/mnt/lustre/mdt_timing_test_$(date +%s)"
LOGFILE="/tmp/mdt_timing_test_$(date +%s).log"

# Clear kernel log
sudo dmesg -c > /dev/null

echo "Starting MDT timing tests at $(date)" | tee -a $LOGFILE
echo "Test directory: $TESTDIR" | tee -a $LOGFILE
echo "-------------" | tee -a $LOGFILE

# Create test directory
mkdir -p $TESTDIR
echo "Created test directory"

# Test 1: Basic file creation (CREATE)
echo "Test 1: Basic file creation"
for i in {1..10}; do
  touch $TESTDIR/file_$i
done
sudo dmesg | grep "MDT_TIMING.*CREATE" | tee -a $LOGFILE

# Test 2: Directory creation (CREATE)
echo "Test 2: Directory creation"
for i in {1..5}; do
  mkdir -p $TESTDIR/dir_$i/subdir_$i
done
sudo dmesg | grep "MDT_TIMING.*CREATE" | tee -a $LOGFILE

# Test 3: Attribute changes (SETATTR)
echo "Test 3: Attribute changes"
chmod 777 $TESTDIR/file_1
chmod 755 $TESTDIR/file_2
chown nobody $TESTDIR/file_3
sudo dmesg | grep "MDT_TIMING.*SETATTR" | tee -a $LOGFILE

# Test 4: Rename operations (RENAME)
echo "Test 4: Rename operations"
mv $TESTDIR/file_4 $TESTDIR/file_4_renamed
mv $TESTDIR/dir_1 $TESTDIR/dir_1_renamed
sudo dmesg | grep "MDT_TIMING.*RENAME" | tee -a $LOGFILE

# Test 5: Hard links (LINK)
echo "Test 5: Hard links"
ln $TESTDIR/file_5 $TESTDIR/file_5_link
ln $TESTDIR/file_6 $TESTDIR/file_6_link
sudo dmesg | grep "MDT_TIMING.*LINK" | tee -a $LOGFILE

# Test 6: Extended attributes (SETXATTR)
echo "Test 6: Extended attributes"
setfattr -n user.test -v "test_value" $TESTDIR/file_7
setfattr -n user.test2 -v "test_value2" $TESTDIR/file_8
sudo dmesg | grep "MDT_TIMING.*SETXATTR" | tee -a $LOGFILE

# Test 7: File deletion (UNLINK)
echo "Test 7: File deletion"
rm $TESTDIR/file_9
rm $TESTDIR/file_10
sudo dmesg | grep "MDT_TIMING.*UNLINK" | tee -a $LOGFILE

echo "-------------" | tee -a $LOGFILE
echo "Tests complete! Full timing data:" | tee -a $LOGFILE
sudo dmesg | grep MDT_TIMING | tee -a $LOGFILE

echo "Results saved to $LOGFILE"
