#!/bin/bash

# MDT Operations Test - Nested Directory and File Operations
# Tests directory creation, file creation, file deletion, and directory deletion

TESTDIR="/mnt/lustre/nested_mdt_test_$(date +%s)"
LOGFILE="/tmp/nested_mdt_test_$(date +%s).log"
KERNEL_LOG="/tmp/mdt_kernel_log_$(date +%s).log"

echo "=========================================" | tee -a $LOGFILE
echo "=== MDT NESTED OPERATIONS TEST ===" | tee -a $LOGFILE
echo "=========================================" | tee -a $LOGFILE
echo "Started at $(date)" | tee -a $LOGFILE
echo "Test directory: $TESTDIR" | tee -a $LOGFILE
echo "Log file: $LOGFILE" | tee -a $LOGFILE
echo "Kernel log capture: $KERNEL_LOG" | tee -a $LOGFILE
echo "" | tee -a $LOGFILE

# Function to capture and analyze current kernel log state
analyze_current_operations() {
    local step_name="$1"
    local expected_ops="$2"
    
    echo "=== ANALYSIS: $step_name ===" | tee -a $LOGFILE
    
    # Capture current kernel log entries
    sudo dmesg | grep "MDT_TIMING" > "$KERNEL_LOG.tmp"
    
    if [ -s "$KERNEL_LOG.tmp" ]; then
        # Count total operations
        local total_ops=$(wc -l < "$KERNEL_LOG.tmp")
        echo "Total MDT operations captured: $total_ops" | tee -a $LOGFILE
        
        # Analyze by operation type
        echo "" | tee -a $LOGFILE
        echo "Operations by type:" | tee -a $LOGFILE
        grep -o "Operation [A-Z_]*" "$KERNEL_LOG.tmp" | sort | uniq -c | sort -nr | tee -a $LOGFILE
        
        # Analyze by MDT
        echo "" | tee -a $LOGFILE
        echo "Operations by MDT:" | tee -a $LOGFILE
        if grep -q "\[MDT:" "$KERNEL_LOG.tmp"; then
            grep -o "\[MDT:[^]]*\]" "$KERNEL_LOG.tmp" | sort | uniq -c | sort -nr | tee -a $LOGFILE
        else
            echo "No MDT information found in logs (using old format)" | tee -a $LOGFILE
        fi
        
        # Analyze by Node
        echo "" | tee -a $LOGFILE
        echo "Operations by Node:" | tee -a $LOGFILE
        if grep -q "Node:" "$KERNEL_LOG.tmp"; then
            grep -o "Node:[0-9]*" "$KERNEL_LOG.tmp" | sort | uniq -c | sort -nr | tee -a $LOGFILE
        else
            echo "No Node information found in logs" | tee -a $LOGFILE
        fi
        
        # Show timing statistics
        echo "" | tee -a $LOGFILE
        echo "Timing statistics (microseconds):" | tee -a $LOGFILE
        times=$(grep -o "took [0-9]* microseconds" "$KERNEL_LOG.tmp" | grep -o "[0-9]*")
        if [ -n "$times" ]; then
            echo "$times" | awk '
            {
                sum += $1
                sumsq += ($1)^2
                if (NR == 1) {
                    min = max = $1
                } else {
                    if ($1 < min) min = $1
                    if ($1 > max) max = $1
                }
            }
            END {
                if (NR > 0) {
                    avg = sum / NR
                    if (NR > 1) {
                        stddev = sqrt((sumsq - sum^2/NR) / (NR-1))
                    } else {
                        stddev = 0
                    }
                    printf "  Min: %d μs\n", min
                    printf "  Max: %d μs\n", max
                    printf "  Avg: %.1f μs\n", avg
                    printf "  StdDev: %.1f μs\n", stddev
                    printf "  Count: %d operations\n", NR
                }
            }' | tee -a $LOGFILE
        fi
        
        # Save current state to main kernel log
        cat "$KERNEL_LOG.tmp" >> "$KERNEL_LOG"
        
    else
        echo "No MDT_TIMING entries found!" | tee -a $LOGFILE
    fi
    
    echo "" | tee -a $LOGFILE
    echo "Expected: $expected_ops" | tee -a $LOGFILE
    echo "---" | tee -a $LOGFILE
    echo "" | tee -a $LOGFILE
}

# Function to wait and ensure operations are logged
wait_for_operations() {
    echo "Waiting for operations to be logged..." | tee -a $LOGFILE
    sleep 1
}

# Function to create nested directories recursively
create_nested_dirs() {
    local current_path="$1"
    local current_depth="$2"
    local max_depth="$3"
    
    if [ $current_depth -eq $max_depth ]; then
        # We're at leaf level, store the path
        echo "$current_path" >> "$LEAF_DIRS_FILE"
        return
    fi
    
    # Create subdirectories at this level
    for i in {1..2}; do
        local subdir="${current_path}/level${current_depth}_dir${i}"
        mkdir -p "$subdir"
        ((DIR_COUNT++))
        create_nested_dirs "$subdir" $((current_depth + 1)) "$max_depth"
    done
}

# Get user input
echo "Enter the depth of nested directories: "
read -r DEPTH
while ! [[ "$DEPTH" =~ ^[0-9]+$ ]] || [ "$DEPTH" -lt 1 ]; do
    echo "Please enter a positive integer for depth: "
    read -r DEPTH
done

echo "Enter the number of files to create at each leaf directory: "
read -r NUM_FILES
while ! [[ "$NUM_FILES" =~ ^[0-9]+$ ]] || [ "$NUM_FILES" -lt 1 ]; do
    echo "Please enter a positive integer for number of files: "
    read -r NUM_FILES
done

echo "" | tee -a $LOGFILE
echo "Configuration:" | tee -a $LOGFILE
echo "  Directory depth: $DEPTH" | tee -a $LOGFILE
echo "  Files per leaf directory: $NUM_FILES" | tee -a $LOGFILE
echo "" | tee -a $LOGFILE

# Setup
echo "Setting up test environment..." | tee -a $LOGFILE
echo "Clearing kernel log buffer..." | tee -a $LOGFILE
sudo dmesg -c > /dev/null

# Temporary file to store leaf directories
LEAF_DIRS_FILE="/tmp/leaf_dirs_$$.txt"
> "$LEAF_DIRS_FILE"

# Directory counter
DIR_COUNT=0

# Step 1: Create nested directories
echo "STEP 1: CREATING NESTED DIRECTORIES" | tee -a $LOGFILE
echo "===================================" | tee -a $LOGFILE
echo "Creating directory structure with depth=$DEPTH..." | tee -a $LOGFILE

mkdir -p "$TESTDIR"
((DIR_COUNT++))
create_nested_dirs "$TESTDIR" 1 "$DEPTH"

# Calculate expected number of directories
EXPECTED_DIRS=$((2**DEPTH - 1))
echo "Created $DIR_COUNT directories (expected: $EXPECTED_DIRS)" | tee -a $LOGFILE

wait_for_operations
analyze_current_operations "Directory Creation" "$DIR_COUNT CREATE operations for directories"

# Step 2: Create files in leaf directories
echo "STEP 2: CREATING FILES IN LEAF DIRECTORIES" | tee -a $LOGFILE
echo "==========================================" | tee -a $LOGFILE

FILE_COUNT=0
LEAF_COUNT=0
while IFS= read -r leaf_dir; do
    ((LEAF_COUNT++))
    echo "Creating $NUM_FILES files in leaf directory: $leaf_dir" | tee -a $LOGFILE
    for i in $(seq 1 $NUM_FILES); do
        filename="${leaf_dir}/file_${i}.txt"
        echo "Test content for file $i" > "$filename"
        ((FILE_COUNT++))
    done
done < "$LEAF_DIRS_FILE"

echo "Created $FILE_COUNT files in $LEAF_COUNT leaf directories" | tee -a $LOGFILE

wait_for_operations
analyze_current_operations "File Creation" "$FILE_COUNT OPEN operations for files"

# Step 3: Delete files
echo "STEP 3: DELETING FILES" | tee -a $LOGFILE
echo "======================" | tee -a $LOGFILE

DEL_FILE_COUNT=0
while IFS= read -r leaf_dir; do
    echo "Deleting files from: $leaf_dir" | tee -a $LOGFILE
    for i in $(seq 1 $NUM_FILES); do
        filename="${leaf_dir}/file_${i}.txt"
        rm -f "$filename"
        ((DEL_FILE_COUNT++))
    done
done < "$LEAF_DIRS_FILE"

echo "Deleted $DEL_FILE_COUNT files" | tee -a $LOGFILE

wait_for_operations
analyze_current_operations "File Deletion" "$DEL_FILE_COUNT UNLINK operations for files"

# Step 4: Delete directories (from deepest to root)
echo "STEP 4: DELETING DIRECTORIES" | tee -a $LOGFILE
echo "============================" | tee -a $LOGFILE
echo "Removing directory tree..." | tee -a $LOGFILE

# Use find to remove directories from deepest to shallowest
find "$TESTDIR" -type d -depth | while read -r dir; do
    echo "Removing directory: $dir" | tee -a $LOGFILE
    rmdir "$dir" 2>/dev/null || echo "  (Directory not empty or already removed)" | tee -a $LOGFILE
done

# Alternative: force remove everything if rmdir fails
if [ -d "$TESTDIR" ]; then
    echo "Force removing remaining directories..." | tee -a $LOGFILE
    rm -rf "$TESTDIR"
fi

wait_for_operations
analyze_current_operations "Directory Deletion" "UNLINK operations for directories"

# Final Analysis
echo "=========================================" | tee -a $LOGFILE
echo "=== FINAL COMPREHENSIVE ANALYSIS ===" | tee -a $LOGFILE
echo "=========================================" | tee -a $LOGFILE

# Capture final state
sudo dmesg | grep "MDT_TIMING" > "$KERNEL_LOG.final"

if [ -s "$KERNEL_LOG.final" ]; then
    total_operations=$(wc -l < "$KERNEL_LOG.final")
    echo "TOTAL OPERATIONS PERFORMED: $total_operations" | tee -a $LOGFILE
    echo "" | tee -a $LOGFILE
    
    echo "=== OPERATION TYPE DISTRIBUTION ===" | tee -a $LOGFILE
    grep -o "Operation [A-Z_]*" "$KERNEL_LOG.final" | sort | uniq -c | sort -nr | tee -a $LOGFILE
    echo "" | tee -a $LOGFILE
    
    echo "=== MDT DISTRIBUTION ===" | tee -a $LOGFILE
    if grep -q "\[MDT:" "$KERNEL_LOG.final"; then
        grep -o "\[MDT:[^]]*\]" "$KERNEL_LOG.final" | sort | uniq -c | sort -nr | tee -a $LOGFILE
        
        echo "" | tee -a $LOGFILE
        echo "=== OPERATIONS PER MDT ===" | tee -a $LOGFILE
        while IFS= read -r mdt_name; do
            count=$(grep -c "$mdt_name" "$KERNEL_LOG.final")
            echo "MDT $mdt_name: $count operations" | tee -a $LOGFILE
            
            echo "  Operation breakdown:" | tee -a $LOGFILE
            grep "$mdt_name" "$KERNEL_LOG.final" | grep -o "Operation [A-Z_]*" | sort | uniq -c | sort -nr | sed 's/^/    /' | tee -a $LOGFILE
            echo "" | tee -a $LOGFILE
        done < <(grep -o "\[MDT:[^]]*\]" "$KERNEL_LOG.final" | sort -u)
    else
        echo "No MDT-specific information available (old log format)" | tee -a $LOGFILE
    fi
    
    echo "" | tee -a $LOGFILE
    echo "=== PERFORMANCE SUMMARY ===" | tee -a $LOGFILE
    times=$(grep -o "took [0-9]* microseconds" "$KERNEL_LOG.final" | grep -o "[0-9]*")
    if [ -n "$times" ]; then
        echo "$times" | awk '
        {
            sum += $1
            sumsq += ($1)^2
            if (NR == 1) {
                min = max = $1
            } else {
                if ($1 < min) min = $1
                if ($1 > max) max = $1
            }
        }
        END {
            if (NR > 0) {
                avg = sum / NR
                if (NR > 1) {
                    stddev = sqrt((sumsq - sum^2/NR) / (NR-1))
                } else {
                    stddev = 0
                }
                printf "Overall Statistics:\n"
                printf "  Total operations: %d\n", NR
                printf "  Min time: %d μs\n", min
                printf "  Max time: %d μs\n", max
                printf "  Average time: %.1f μs\n", avg
                printf "  Standard deviation: %.1f μs\n", stddev
                printf "  Total time: %.1f ms\n", sum/1000
                printf "\nExpected operations breakdown:\n"
                printf "  Directory creations: %d\n", '$DIR_COUNT'
                printf "  File creations: %d\n", '$FILE_COUNT'
                printf "  File deletions: %d\n", '$DEL_FILE_COUNT'
                printf "  Directory deletions: %d\n", '$DIR_COUNT'
                printf "  Total expected: %d\n", '$DIR_COUNT' + '$FILE_COUNT' + '$DEL_FILE_COUNT' + '$DIR_COUNT'
            }
        }' | tee -a $LOGFILE
    fi
    
else
    echo "No MDT operations were captured!" | tee -a $LOGFILE
    echo "Check if:" | tee -a $LOGFILE
    echo "  - MDT timing logging is enabled in the kernel" | tee -a $LOGFILE
    echo "  - The modifications were applied correctly" | tee -a $LOGFILE
    echo "  - The Lustre filesystem is working properly" | tee -a $LOGFILE
fi

# Cleanup temporary files
rm -f "$KERNEL_LOG.tmp" "$LEAF_DIRS_FILE" 2>/dev/null

echo "" | tee -a $LOGFILE
echo "=== TEST COMPLETION ===" | tee -a $LOGFILE
echo "Test completed at $(date)" | tee -a $LOGFILE
echo "" | tee -a $LOGFILE
echo "Generated files:" | tee -a $LOGFILE
echo "  - Test log: $LOGFILE" | tee -a $LOGFILE
echo "  - Kernel log: $KERNEL_LOG.final" | tee -a $LOGFILE

echo "" | tee -a $LOGFILE
echo "To analyze with the plotting script:" | tee -a $LOGFILE
echo "  python plot_mdt_timing.py $KERNEL_LOG.final" | tee -a $LOGFILE

echo ""
echo "========================================="
echo "Test completed! Check the results in:"
echo "  Log file: $LOGFILE"
echo "  Kernel operations: $KERNEL_LOG.final"
echo ""
echo "To run analysis:"
echo "  python plot_mdt_timing.py $KERNEL_LOG.final"
echo ""
echo "To monitor live operations:"
echo "  sudo dmesg -w | grep MDT_TIMING"
