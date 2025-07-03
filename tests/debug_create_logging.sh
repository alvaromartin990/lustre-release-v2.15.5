#!/bin/bash

TESTDIR="/mnt/lustre/debug_create_$(date +%s)"
LOGFILE="/tmp/debug_create_$(date +%s).log"

echo "=== CREATE Operation Debug Analysis ===" | tee -a $LOGFILE
echo "Started at $(date)" | tee -a $LOGFILE

# Clear kernel log
sudo dmesg -c > /dev/null

mkdir -p $TESTDIR
echo "Created test directory: $TESTDIR" | tee -a $LOGFILE

# Test different file creation methods to see which triggers CREATE vs OPEN
echo "=== Testing Different File Creation Methods ===" | tee -a $LOGFILE

echo "1. Using touch command:" | tee -a $LOGFILE
touch $TESTDIR/touch_test
sudo dmesg | grep MDT_TIMING | tail -5 | tee -a $LOGFILE
echo "---" | tee -a $LOGFILE

echo "2. Using echo redirection:" | tee -a $LOGFILE
echo "test" > $TESTDIR/echo_test
sudo dmesg | grep MDT_TIMING | tail -5 | tee -a $LOGFILE
echo "---" | tee -a $LOGFILE

echo "3. Using cp from /dev/null:" | tee -a $LOGFILE
cp /dev/null $TESTDIR/cp_test
sudo dmesg | grep MDT_TIMING | tail -5 | tee -a $LOGFILE
echo "---" | tee -a $LOGFILE

echo "4. Using dd:" | tee -a $LOGFILE
dd if=/dev/null of=$TESTDIR/dd_test bs=1 count=0 2>/dev/null
sudo dmesg | grep MDT_TIMING | tail -5 | tee -a $LOGFILE
echo "---" | tee -a $LOGFILE

echo "5. Creating with specific open() flags using Python:" | tee -a $LOGFILE
python3 -c "
import os
# O_CREAT | O_WRONLY | O_TRUNC
fd = os.open('$TESTDIR/python_test', os.O_CREAT | os.O_WRONLY | os.O_TRUNC, 0o644)
os.close(fd)
"
sudo dmesg | grep MDT_TIMING | tail -5 | tee -a $LOGFILE
echo "---" | tee -a $LOGFILE

echo "6. Directory creation (known to work):" | tee -a $LOGFILE
mkdir $TESTDIR/debug_subdir
sudo dmesg | grep MDT_TIMING | tail -5 | tee -a $LOGFILE
echo "---" | tee -a $LOGFILE

# Test if CREATE timing is working for directories but not files
echo "=== Testing Multiple Directory Creation ===" | tee -a $LOGFILE
for i in {1..5}; do
    mkdir $TESTDIR/dir_$i
done
create_dirs=$(sudo dmesg | grep "MDT_TIMING.*CREATE" | wc -l)
echo "CREATE entries after directory creation: $create_dirs" | tee -a $LOGFILE

# Test with different timing intervals
echo "=== Testing CREATE with Delays ===" | tee -a $LOGFILE
for i in {1..5}; do
    echo "Creating file with delay: file_delay_$i" | tee -a $LOGFILE
    touch $TESTDIR/file_delay_$i
    sleep 0.1  # Small delay to see if timing changes
    sudo dmesg | grep MDT_TIMING | tail -2 | tee -a $LOGFILE
    echo "---" | tee -a $LOGFILE
done

# Check all operation types in final summary
echo "=== Final Operation Summary ===" | tee -a $LOGFILE
echo "All MDT_TIMING entries:" | tee -a $LOGFILE
sudo dmesg | grep MDT_TIMING | tee -a $LOGFILE

# Count each operation type
create_count=$(sudo dmesg | grep "MDT_TIMING.*CREATE" | wc -l)
open_count=$(sudo dmesg | grep "MDT_TIMING.*OPEN" | wc -l)
close_count=$(sudo dmesg | grep "MDT_TIMING.*CLOSE" | wc -l)
getattr_count=$(sudo dmesg | grep "MDT_TIMING.*GETATTR" | wc -l)
setattr_count=$(sudo dmesg | grep "MDT_TIMING.*SETATTR" | wc -l)

echo "=== Operation Count Summary ===" | tee -a $LOGFILE
echo "CREATE: $create_count" | tee -a $LOGFILE
echo "OPEN: $open_count" | tee -a $LOGFILE
echo "CLOSE: $close_count" | tee -a $LOGFILE
echo "GETATTR: $getattr_count" | tee -a $LOGFILE
echo "SETATTR: $setattr_count" | tee -a $LOGFILE

# Hypothesis testing
echo "=== Analysis ===" | tee -a $LOGFILE
total_files_created=10  # Approximate count of files we created
total_dirs_created=7   # Approximate count of directories we created

echo "Files created: ~$total_files_created" | tee -a $LOGFILE
echo "Directories created: ~$total_dirs_created" | tee -a $LOGFILE
echo "CREATE operations logged: $create_count" | tee -a $LOGFILE
echo "OPEN operations logged: $open_count" | tee -a $LOGFILE

if [ $create_count -lt $((total_files_created + total_dirs_created)) ]; then
    echo "CONCLUSION: CREATE operations are not being logged properly for files!" | tee -a $LOGFILE
    if [ $create_count -gt 0 ] && [ $create_count -ge $total_dirs_created ]; then
        echo "HYPOTHESIS: CREATE logging might work for directories but not files" | tee -a $LOGFILE
    fi
else
    echo "CONCLUSION: CREATE operations are being logged correctly" | tee -a $LOGFILE
fi

if [ $open_count -ge $total_files_created ]; then
    echo "OBSERVATION: OPEN operations are being logged for file creation" | tee -a $LOGFILE
    echo "HYPOTHESIS: File creation goes through OPEN path, not CREATE path" | tee -a $LOGFILE
fi

echo "=== Recommendations ===" | tee -a $LOGFILE
echo "1. Check if CREATE timing hook is only in directory creation code path" | tee -a $LOGFILE
echo "2. Verify if file creation uses OPEN instead of CREATE in the kernel" | tee -a $LOGFILE
echo "3. Look for separate file vs directory creation code paths in Lustre" | tee -a $LOGFILE
echo "4. Check if CREATE timing is accidentally filtered or rate-limited" | tee -a $LOGFILE

echo "Debug complete. Results saved to: $LOGFILE"
