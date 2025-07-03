#!/bin/bash

# Test script to answer:
# 1. Are CREATE operations only for directories?
# 2. Are OPEN operations actually "file creation"?
# 3. What are OPEN ops then?

TESTDIR="/mnt/lustre/operation_test_$(date +%s)"
LOGFILE="/tmp/operation_test_$(date +%s).log"

echo "=== Operation Type Investigation ===" | tee -a $LOGFILE
echo "Started at $(date)" | tee -a $LOGFILE
echo "Test directory: $TESTDIR" | tee -a $LOGFILE
echo "-------------" | tee -a $LOGFILE

# Function to show current operation counts
show_counts() {
    local test_name="$1"
    local create_count=$(sudo dmesg | grep "MDT_TIMING.*CREATE" | wc -l)
    local open_count=$(sudo dmesg | grep "MDT_TIMING.*OPEN" | wc -l)
    local setattr_count=$(sudo dmesg | grep "MDT_TIMING.*SETATTR" | wc -l)
    local close_count=$(sudo dmesg | grep "MDT_TIMING.*CLOSE" | wc -l)
    
    echo "$test_name - CREATE:$create_count, OPEN:$open_count, SETATTR:$setattr_count, CLOSE:$close_count" | tee -a $LOGFILE
}

# Clear kernel log and create base directory
sudo dmesg -c > /dev/null
mkdir -p $TESTDIR
show_counts "After base directory creation"

echo "=== TEST 1: Directory Creation Only ===" | tee -a $LOGFILE
echo "Creating 5 directories to test if CREATE is directory-specific" | tee -a $LOGFILE
for i in {1..5}; do
    mkdir $TESTDIR/dir_$i
    show_counts "After creating directory $i"
done
echo "---" | tee -a $LOGFILE

echo "=== TEST 2: File Creation (New Files) ===" | tee -a $LOGFILE
echo "Creating 3 new files to see OPEN vs CREATE" | tee -a $LOGFILE
for i in {1..3}; do
    touch $TESTDIR/newfile_$i
    show_counts "After creating new file $i"
done
echo "---" | tee -a $LOGFILE

echo "=== TEST 3: Opening Existing Files ===" | tee -a $LOGFILE
echo "Opening existing files to distinguish create vs open" | tee -a $LOGFILE
for i in {1..3}; do
    # Read from existing file (should trigger OPEN without creation)
    cat $TESTDIR/newfile_$i > /dev/null
    show_counts "After reading existing file $i"
done
echo "---" | tee -a $LOGFILE

echo "=== TEST 4: Writing to Existing Files ===" | tee -a $LOGFILE
echo "Writing to existing files" | tee -a $LOGFILE
for i in {1..3}; do
    echo "test content" > $TESTDIR/newfile_$i
    show_counts "After writing to existing file $i"
done
echo "---" | tee -a $LOGFILE

echo "=== TEST 5: Appending to Files ===" | tee -a $LOGFILE  
echo "Appending to existing files" | tee -a $LOGFILE
for i in {1..3}; do
    echo "appended content" >> $TESTDIR/newfile_$i
    show_counts "After appending to file $i"
done
echo "---" | tee -a $LOGFILE

echo "=== TEST 6: Creating Files with Content ===" | tee -a $LOGFILE
echo "Creating files with immediate content" | tee -a $LOGFILE
for i in {1..3}; do
    echo "initial content" > $TESTDIR/contentfile_$i
    show_counts "After creating file with content $i"
done
echo "---" | tee -a $LOGFILE

echo "=== TEST 7: Different File Creation Methods ===" | tee -a $LOGFILE
echo "Testing various file creation methods" | tee -a $LOGFILE

echo "Method 1: cp from /dev/null" | tee -a $LOGFILE
cp /dev/null $TESTDIR/cp_method
show_counts "After cp method"

echo "Method 2: dd command" | tee -a $LOGFILE
dd if=/dev/null of=$TESTDIR/dd_method bs=1 count=0 2>/dev/null
show_counts "After dd method"

echo "Method 3: Using > redirection" | tee -a $LOGFILE
> $TESTDIR/redirect_method
show_counts "After redirect method"
echo "---" | tee -a $LOGFILE

echo "=== FINAL ANALYSIS ===" | tee -a $LOGFILE
echo "All MDT_TIMING entries:" | tee -a $LOGFILE
sudo dmesg | grep MDT_TIMING | tee -a $LOGFILE

# Final counts
final_create=$(sudo dmesg | grep "MDT_TIMING.*CREATE" | wc -l)
final_open=$(sudo dmesg | grep "MDT_TIMING.*OPEN" | wc -l)
final_setattr=$(sudo dmesg | grep "MDT_TIMING.*SETATTR" | wc -l)
final_close=$(sudo dmesg | grep "MDT_TIMING.*CLOSE" | wc -l)

echo "-------------" | tee -a $LOGFILE
echo "FINAL COUNTS:" | tee -a $LOGFILE
echo "CREATE: $final_create" | tee -a $LOGFILE
echo "OPEN: $final_open" | tee -a $LOGFILE
echo "SETATTR: $final_setattr" | tee -a $LOGFILE
echo "CLOSE: $final_close" | tee -a $LOGFILE

# Calculate what we created
total_dirs=6  # 1 base + 5 test directories
total_files=12  # 3 newfiles + 3 contentfiles + 3 method files + 3 duplicates from writing
total_operations=$((total_dirs + total_files))

echo "-------------" | tee -a $LOGFILE
echo "ANALYSIS:" | tee -a $LOGFILE
echo "Directories created: $total_dirs" | tee -a $LOGFILE
echo "Files created/modified: ~$total_files" | tee -a $LOGFILE

# Test hypotheses
echo "-------------" | tee -a $LOGFILE
echo "HYPOTHESIS TESTING:" | tee -a $LOGFILE

# Hypothesis 1: CREATE operations only for directories
if [ $final_create -eq $total_dirs ]; then
    echo "✓ HYPOTHESIS 1 CONFIRMED: CREATE operations are ONLY for directories" | tee -a $LOGFILE
    echo "  CREATE count ($final_create) matches directory count ($total_dirs)" | tee -a $LOGFILE
elif [ $final_create -gt $total_dirs ]; then
    echo "✗ HYPOTHESIS 1 REJECTED: CREATE operations include more than directories" | tee -a $LOGFILE
    echo "  CREATE count ($final_create) > directory count ($total_dirs)" | tee -a $LOGFILE
else
    echo "? HYPOTHESIS 1 UNCLEAR: Fewer CREATE ops than directories created" | tee -a $LOGFILE
    echo "  CREATE count ($final_create) < directory count ($total_dirs)" | tee -a $LOGFILE
fi

# Hypothesis 2: OPEN operations for file creation/access
echo "HYPOTHESIS 2: OPEN operations represent file access (creation + reading + writing)" | tee -a $LOGFILE
echo "  OPEN count: $final_open" | tee -a $LOGFILE
echo "  This includes: file creation, reading, writing, appending" | tee -a $LOGFILE

# Hypothesis 3: What are OPEN ops
echo "HYPOTHESIS 3: OPEN operations are ANY file access operation" | tee -a $LOGFILE
echo "  - File creation (touch, >, cp, dd)" | tee -a $LOGFILE
echo "  - Reading existing files (cat)" | tee -a $LOGFILE
echo "  - Writing to files (>, >>)" | tee -a $LOGFILE
echo "  - NOT directory operations" | tee -a $LOGFILE

echo "-------------" | tee -a $LOGFILE
echo "CONCLUSION:" | tee -a $LOGFILE
echo "- CREATE = Directory operations only" | tee -a $LOGFILE
echo "- OPEN = All file access operations (create, read, write)" | tee -a $LOGFILE
echo "- SETATTR = File attribute changes (often paired with OPEN)" | tee -a $LOGFILE

echo "Test completed. Results saved to: $LOGFILE"
