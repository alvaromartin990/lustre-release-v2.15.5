#!/bin/bash

TESTDIR="/mnt/lustre/mdt_timing_test_$(date +%s)"
LOGFILE="/tmp/mdt_timing_test_$(date +%s).log"

# Maximum depth for directory structure
MAX_DEPTH=8
# Branching factor (subdirs per directory)
BRANCH_FACTOR=2
# Number of files to create at leaf directories
LEAF_FILES=5

# List to collect created files for operations
FILE_LIST=()
DIR_LIST=()

# Function to create recursive directory structure
create_directory_tree() {
    local current_path="$1"
    local current_depth="$2"
    
    # Add this directory to our list
    DIR_LIST+=("$current_path")
    
    # If we've reached max depth, create files
    if [ "$current_depth" -eq "$MAX_DEPTH" ]; then
        for i in $(seq 1 $LEAF_FILES); do
            local file_path="${current_path}/leaf_file_${i}"
            touch "$file_path"
            FILE_LIST+=("$file_path")
        done
        return
    fi
    
    # Otherwise create subdirectories and recurse
    for i in $(seq 1 $BRANCH_FACTOR); do
        local new_dir="${current_path}/dir_${i}"
        mkdir -p "$new_dir"
        create_directory_tree "$new_dir" $((current_depth + 1))
    done
}

# Clear kernel log
sudo dmesg -c > /dev/null

echo "Starting MDT timing tests at $(date)" | tee -a $LOGFILE
echo "Test directory: $TESTDIR" | tee -a $LOGFILE
echo "Directory depth: $MAX_DEPTH, branching factor: $BRANCH_FACTOR" | tee -a $LOGFILE
echo "Files per leaf directory: $LEAF_FILES" | tee -a $LOGFILE
echo "-------------" | tee -a $LOGFILE

# Create test directory
mkdir -p $TESTDIR
echo "Created test directory"

# Test 1: Create deep directory structure
echo "Test 1: Creating deep directory structure"
create_directory_tree "$TESTDIR" 0
echo "Created $(echo ${#DIR_LIST[@]}) directories and $(echo ${#FILE_LIST[@]}) files"
sudo dmesg | grep "MDT_TIMING.*CREATE" | tee -a $LOGFILE

# Get random samples for testing
# Select random files for operations
get_random_files() {
    local count=$1
    for i in $(seq 1 $count); do
        echo "${FILE_LIST[$RANDOM % ${#FILE_LIST[@]}]}"
    done
}

# Select random directories for operations
get_random_dirs() {
    local count=$1
    for i in $(seq 1 $count); do
        echo "${DIR_LIST[$RANDOM % ${#DIR_LIST[@]}]}"
    done
}

# Test 2: Attribute changes (SETATTR)
echo "Test 2: Attribute changes"
for file in $(get_random_files 10); do
    chmod 777 "$file"
done
for dir in $(get_random_dirs 5); do
    chmod 755 "$dir"
done
for file in $(get_random_files 5); do
    chown nobody "$file"
done
sudo dmesg | grep "MDT_TIMING.*SETATTR" | tee -a $LOGFILE

# Test 3: Rename operations (RENAME)
echo "Test 3: Rename operations"
for file in $(get_random_files 10); do
    mv "$file" "${file}_renamed"
    FILE_LIST+=("${file}_renamed")
done
for dir in $(get_random_dirs 5); do
    # Only rename directories that aren't the test root
    if [[ "$dir" != "$TESTDIR" ]]; then
        mv "$dir" "${dir}_renamed"
        DIR_LIST+=("${dir}_renamed")
    fi
done
sudo dmesg | grep "MDT_TIMING.*RENAME" | tee -a $LOGFILE

# Test 4: Hard links (LINK)
echo "Test 4: Hard links"
for file in $(get_random_files 10); do
    if [ -f "$file" ]; then  # Ensure the file exists
        ln "$file" "${file}_link"
        FILE_LIST+=("${file}_link")
    fi
done
sudo dmesg | grep "MDT_TIMING.*LINK" | tee -a $LOGFILE

# Test 5: Extended attributes (SETXATTR)
echo "Test 5: Extended attributes"
for file in $(get_random_files 10); do
    if [ -f "$file" ]; then  # Ensure the file exists
        setfattr -n user.test -v "test_value" "$file"
    fi
done
for dir in $(get_random_dirs 5); do
    if [ -d "$dir" ]; then  # Ensure the directory exists
        setfattr -n user.test2 -v "test_value2" "$dir"
    fi
done
sudo dmesg | grep "MDT_TIMING.*SETXATTR" | tee -a $LOGFILE

# Test 6: File lookup operations
echo "Test 6: File lookup/stat operations"
for file in $(get_random_files 20); do
    if [ -f "$file" ]; then
        stat "$file" > /dev/null
    fi
done
sudo dmesg | grep "MDT_TIMING.*GETATTR" | tee -a $LOGFILE

# Test 7: File deletion (UNLINK)
echo "Test 7: File deletion"
for file in $(get_random_files 20); do
    if [ -f "$file" ]; then
        rm "$file"
    fi
done
sudo dmesg | grep "MDT_TIMING.*UNLINK" | tee -a $LOGFILE

echo "-------------" | tee -a $LOGFILE
echo "Tests complete! Full timing data:" | tee -a $LOGFILE
sudo dmesg | grep MDT_TIMING | tee -a $LOGFILE

echo "Results saved to $LOGFILE"
