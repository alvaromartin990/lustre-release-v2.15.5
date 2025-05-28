#!/bin/bash

# Simple file creation test - translating C sprintf pattern to bash
# C pattern: sprintf(curr_item, "%s/file.%s"LLU"", path, o.mk_name, itemNum);

# Configuration
path="/mnt/lustre/simple_test_$(date +%s)"
mk_name="test"  # Equivalent to o.mk_name in C code
LOGFILE="/tmp/simple_file_create_$(date +%s).log"

echo "=== Simple File Creation Test ===" | tee -a $LOGFILE
echo "Started at $(date)" | tee -a $LOGFILE
echo "Test directory: $path" | tee -a $LOGFILE
echo "File naming pattern: \${path}/file.\${mk_name}\${itemNum}" | tee -a $LOGFILE
echo "-------------" | tee -a $LOGFILE

# Clear kernel log and create test directory
sudo dmesg -c > /dev/null
mkdir -p $path
echo "Cleared kernel log and created test directory" | tee -a $LOGFILE

# Record start time
start_time=$(date +%s.%N)
echo "Starting file creation at $(date)" | tee -a $LOGFILE

# Create 10 files using the C sprintf pattern translated to bash
for itemNum in {1..10}; do
    # Translate C sprintf pattern: sprintf(curr_item, "%s/file.%s%llu", path, o.mk_name, itemNum);
    curr_item="${path}/file.${mk_name}${itemNum}"
    
    echo "Creating file: $curr_item" | tee -a $LOGFILE
    touch "$curr_item"
    
    # Check immediately after each file creation
    current_creates=$(sudo dmesg | grep "MDT_TIMING.*CREATE" | wc -l)
    current_opens=$(sudo dmesg | grep "MDT_TIMING.*OPEN" | wc -l)
    echo "  After file $itemNum: CREATE=$current_creates, OPEN=$current_opens" | tee -a $LOGFILE
done

# Record end time
end_time=$(date +%s.%N)
duration=$(echo "$end_time - $start_time" | bc)
echo "File creation completed at $(date)" | tee -a $LOGFILE
echo "Total time: ${duration} seconds" | tee -a $LOGFILE

echo "-------------" | tee -a $LOGFILE
echo "=== Final Results ===" | tee -a $LOGFILE

# Count all operation types
create_count=$(sudo dmesg | grep "MDT_TIMING.*CREATE" | wc -l)
open_count=$(sudo dmesg | grep "MDT_TIMING.*OPEN" | wc -l)
close_count=$(sudo dmesg | grep "MDT_TIMING.*CLOSE" | wc -l)
setattr_count=$(sudo dmesg | grep "MDT_TIMING.*SETATTR" | wc -l)

echo "Operation counts:" | tee -a $LOGFILE
echo "  CREATE: $create_count (expected: 10 files + 1 directory = 11)" | tee -a $LOGFILE
echo "  OPEN: $open_count" | tee -a $LOGFILE
echo "  CLOSE: $close_count" | tee -a $LOGFILE
echo "  SETATTR: $setattr_count" | tee -a $LOGFILE

# Show all timing entries
echo "-------------" | tee -a $LOGFILE
echo "All MDT_TIMING entries:" | tee -a $LOGFILE
sudo dmesg | grep MDT_TIMING | tee -a $LOGFILE

# Analysis
echo "-------------" | tee -a $LOGFILE
echo "=== Analysis ===" | tee -a $LOGFILE

if [ $create_count -eq 1 ]; then
    echo "RESULT: Only 1 CREATE operation logged (likely the directory creation)" | tee -a $LOGFILE
    echo "CONCLUSION: File creation does NOT trigger CREATE operation logging" | tee -a $LOGFILE
elif [ $create_count -eq 11 ]; then
    echo "RESULT: All CREATE operations logged correctly (10 files + 1 directory)" | tee -a $LOGFILE
    echo "CONCLUSION: CREATE operation logging is working perfectly" | tee -a $LOGFILE
else
    echo "RESULT: $create_count CREATE operations logged (unexpected count)" | tee -a $LOGFILE
    echo "CONCLUSION: Partial CREATE operation logging" | tee -a $LOGFILE
fi

if [ $open_count -eq 10 ]; then
    echo "OBSERVATION: OPEN operations match file creation count exactly" | tee -a $LOGFILE
    echo "HYPOTHESIS: File creation uses OPEN operation, not CREATE operation" | tee -a $LOGFILE
elif [ $open_count -gt 10 ]; then
    echo "OBSERVATION: More OPEN operations than files created" | tee -a $LOGFILE
    echo "HYPOTHESIS: Additional OPEN operations from other sources" | tee -a $LOGFILE
else
    echo "OBSERVATION: Fewer OPEN operations than files created" | tee -a $LOGFILE
fi

# List created files to verify they exist
echo "-------------" | tee -a $LOGFILE
echo "Created files:" | tee -a $LOGFILE
ls -la "$path"/ | tee -a $LOGFILE

echo "-------------" | tee -a $LOGFILE
echo "Test completed. Results saved to: $LOGFILE"

# Optional: Clean up (comment out if you want to keep the files)
# echo "Cleaning up..."
# rm -rf "$path"
