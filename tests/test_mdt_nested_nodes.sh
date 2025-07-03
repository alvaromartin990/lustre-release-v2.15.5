#!/bin/bash

# Directory Tree Test Script for MDT Timing Operations
# Creates a directory tree, places files at leaf level, removes them, and removes the tree
# Tests CREATE, OPEN_FILE_OP, UNLINK, and RMDIR operations

TESTDIR="/mnt/lustre/tree_test_$(date +%s)"
LOGFILE="/tmp/tree_test_$(date +%s).log"

echo "========================================================================" | tee -a $LOGFILE
echo "=== Directory Tree Test for MDT Timing Operations ===" | tee -a $LOGFILE
echo "========================================================================" | tee -a $LOGFILE
echo "Started at $(date)" | tee -a $LOGFILE
echo "Test root directory: $TESTDIR" | tee -a $LOGFILE
echo "" | tee -a $LOGFILE

# Function to get user input with validation
get_user_input() {
    while true; do
        echo "=== Configuration ===" | tee -a $LOGFILE
        echo ""
        
        # Get tree depth
        while true; do
            read -p "Enter the depth of the directory tree (1-10): " TREE_DEPTH
            if [[ "$TREE_DEPTH" =~ ^[0-9]+$ ]] && [ "$TREE_DEPTH" -ge 1 ] && [ "$TREE_DEPTH" -le 10 ]; then
                break
            else
                echo "Please enter a valid number between 1 and 10."
            fi
        done
        
        # Get number of files per leaf directory
        while true; do
            read -p "Enter the number of files to create at leaf level (1-50): " FILES_PER_LEAF
            if [[ "$FILES_PER_LEAF" =~ ^[0-9]+$ ]] && [ "$FILES_PER_LEAF" -ge 1 ] && [ "$FILES_PER_LEAF" -le 50 ]; then
                break
            else
                echo "Please enter a valid number between 1 and 50."
            fi
        done
        
        # Get branching factor
        while true; do
            read -p "Enter the branching factor (directories per level, 1-5): " BRANCH_FACTOR
            if [[ "$BRANCH_FACTOR" =~ ^[0-9]+$ ]] && [ "$BRANCH_FACTOR" -ge 1 ] && [ "$BRANCH_FACTOR" -le 5 ]; then
                break
            else
                echo "Please enter a valid number between 1 and 5."
            fi
        done
        
        # Calculate totals
        local total_dirs=$(( (BRANCH_FACTOR ** (TREE_DEPTH + 1) - 1) / (BRANCH_FACTOR - 1) - 1 ))
        if [ "$BRANCH_FACTOR" -eq 1 ]; then
            total_dirs=$TREE_DEPTH
        fi
        local leaf_dirs=$(( BRANCH_FACTOR ** TREE_DEPTH ))
        local total_files=$(( leaf_dirs * FILES_PER_LEAF ))
        
        echo ""
        echo "=== Configuration Summary ===" | tee -a $LOGFILE
        echo "Tree depth: $TREE_DEPTH levels" | tee -a $LOGFILE
        echo "Branching factor: $BRANCH_FACTOR directories per level" | tee -a $LOGFILE
        echo "Files per leaf directory: $FILES_PER_LEAF" | tee -a $LOGFILE
        echo "Total directories to create: $total_dirs" | tee -a $LOGFILE
        echo "Total leaf directories: $leaf_dirs" | tee -a $LOGFILE
        echo "Total files to create: $total_files" | tee -a $LOGFILE
        echo "" | tee -a $LOGFILE
        
        read -p "Proceed with this configuration? (y/n): " confirm
        if [[ "$confirm" =~ ^[Yy]$ ]]; then
            break
        fi
        echo ""
    done
}

# Function to show detailed kernel log analysis
show_analysis() {
    local step_name="$1"
    echo "" | tee -a $LOGFILE
    echo "=== $step_name ===" | tee -a $LOGFILE
    
    # Count all MDT_TIMING entries
    local total_timing=$(sudo dmesg | grep "MDT_TIMING" | wc -l)
    echo "Total MDT_TIMING entries: $total_timing" | tee -a $LOGFILE
    
    # Count specific operation types
    local create_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation CREATE" | wc -l)
    local open_file_ops=$(sudo dmesg | grep "MDT_TIMING.*OPEN_FILE_OP" | wc -l)
    local open_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation OPEN" | wc -l)
    local unlink_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation UNLINK" | wc -l)
    local rmdir_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation RMDIR" | wc -l)
    local setattr_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation SETATTR" | wc -l)
    local rename_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation RENAME" | wc -l)
    
    echo "Operation breakdown:" | tee -a $LOGFILE
    echo "  CREATE operations: $create_ops" | tee -a $LOGFILE
    echo "  OPEN_FILE_OP operations: $open_file_ops" | tee -a $LOGFILE
    echo "  Total OPEN operations: $open_ops" | tee -a $LOGFILE
    echo "  UNLINK operations: $unlink_ops" | tee -a $LOGFILE
    echo "  RMDIR operations: $rmdir_ops" | tee -a $LOGFILE
    echo "  SETATTR operations: $setattr_ops" | tee -a $LOGFILE
    echo "  RENAME operations: $rename_ops" | tee -a $LOGFILE
    
    # Show timing statistics if available
    if [ $total_timing -gt 0 ]; then
        echo "" | tee -a $LOGFILE
        echo "Recent timing entries (last 10):" | tee -a $LOGFILE
        sudo dmesg | grep "MDT_TIMING" | tail -10 | while read line; do
            echo "  $line" | tee -a $LOGFILE
        done
    fi
    
    echo "---" | tee -a $LOGFILE
}

# Function to create directory tree recursively
create_directory_tree() {
    local base_path="$1"
    local current_depth="$2"
    local max_depth="$3"
    local branch_factor="$4"
    
    if [ "$current_depth" -gt "$max_depth" ]; then
        return
    fi
    
    for i in $(seq 1 $branch_factor); do
        local dir_name="${base_path}/level_${current_depth}_branch_${i}"
        echo "Creating directory: $dir_name" | tee -a $LOGFILE
        mkdir -p "$dir_name"
        
        # Small delay to see individual operations
        sleep 0.05
        
        # Recursively create subdirectories
        if [ "$current_depth" -lt "$max_depth" ]; then
            create_directory_tree "$dir_name" $((current_depth + 1)) "$max_depth" "$branch_factor"
        fi
    done
}

# Function to create files at leaf directories
create_files_at_leaves() {
    local base_path="$1"
    local depth="$2"
    local branch_factor="$3"
    local files_per_leaf="$4"
    local file_count=0
    
    # Find all leaf directories
    echo "Finding leaf directories at depth $depth..." | tee -a $LOGFILE
    
    # Build the path pattern for leaf directories
    local leaf_pattern="$base_path"
    for level in $(seq 1 $depth); do
        leaf_pattern="$leaf_pattern/level_${level}_branch_*"
    done
    
    # Create files in each leaf directory
    for leaf_dir in $leaf_pattern; do
        if [ -d "$leaf_dir" ]; then
            echo "Creating $files_per_leaf files in: $leaf_dir" | tee -a $LOGFILE
            
            for file_num in $(seq 1 $files_per_leaf); do
                local filename="${leaf_dir}/file_${file_num}.txt"
                echo "Creating file: $filename" | tee -a $LOGFILE
                
                # Create file and add content
                touch "$filename"
                echo "Test file $file_num in $(basename $leaf_dir) created at $(date)" > "$filename"
                
                file_count=$((file_count + 1))
                
                # Small delay to see individual operations
                sleep 0.02
                
                # Show progress every 10 files
                if [ $((file_count % 10)) -eq 0 ]; then
                    echo "  Progress: $file_count files created..." | tee -a $LOGFILE
                fi
            done
        fi
    done
    
    echo "Total files created: $file_count" | tee -a $LOGFILE
}

# Function to remove files from leaf directories
remove_files_from_leaves() {
    local base_path="$1"
    local depth="$2"
    local removed_count=0
    
    echo "Removing files from leaf directories..." | tee -a $LOGFILE
    
    # Build the path pattern for leaf directories
    local leaf_pattern="$base_path"
    for level in $(seq 1 $depth); do
        leaf_pattern="$leaf_pattern/level_${level}_branch_*"
    done
    
    # Remove files from each leaf directory
    for leaf_dir in $leaf_pattern; do
        if [ -d "$leaf_dir" ]; then
            echo "Removing files from: $leaf_dir" | tee -a $LOGFILE
            
            for filename in "$leaf_dir"/*.txt; do
                if [ -f "$filename" ]; then
                    echo "Removing file: $filename" | tee -a $LOGFILE
                    rm "$filename"
                    removed_count=$((removed_count + 1))
                    
                    # Small delay to see individual operations
                    sleep 0.02
                    
                    # Show progress every 10 files
                    if [ $((removed_count % 10)) -eq 0 ]; then
                        echo "  Progress: $removed_count files removed..." | tee -a $LOGFILE
                    fi
                fi
            done
        fi
    done
    
    echo "Total files removed: $removed_count" | tee -a $LOGFILE
}

# Function to remove directory tree from deepest to shallowest
remove_directory_tree() {
    local base_path="$1"
    local max_depth="$2"
    local removed_count=0
    
    echo "Removing directory tree from depth $max_depth to 1..." | tee -a $LOGFILE
    
    # Remove directories level by level, from deepest to shallowest
    for level in $(seq $max_depth -1 1); do
        echo "Removing directories at level $level..." | tee -a $LOGFILE
        
        # Build pattern for directories at this level
        local level_pattern="$base_path"
        for l in $(seq 1 $level); do
            level_pattern="$level_pattern/level_${l}_branch_*"
        done
        
        # Remove directories at this level
        for dir_path in $level_pattern; do
            if [ -d "$dir_path" ]; then
                echo "Removing directory: $dir_path" | tee -a $LOGFILE
                rmdir "$dir_path" 2>/dev/null || {
                    echo "  Warning: Could not remove $dir_path (may not be empty)" | tee -a $LOGFILE
                    rm -rf "$dir_path" 2>/dev/null || true
                }
                removed_count=$((removed_count + 1))
                
                # Small delay to see individual operations
                sleep 0.05
            fi
        done
    done
    
    echo "Total directories removed: $removed_count" | tee -a $LOGFILE
}

# Function to show final summary
show_final_summary() {
    echo "" | tee -a $LOGFILE
    echo "========================================================================" | tee -a $LOGFILE
    echo "=== FINAL SUMMARY AND VERIFICATION ===" | tee -a $LOGFILE
    echo "========================================================================" | tee -a $LOGFILE
    
    # Count final results
    local total_timing=$(sudo dmesg | grep "MDT_TIMING" | wc -l)
    local create_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation CREATE" | wc -l)
    local open_file_ops=$(sudo dmesg | grep "MDT_TIMING.*OPEN_FILE_OP" | wc -l)
    local open_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation OPEN" | wc -l)
    local unlink_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation UNLINK" | wc -l)
    local rmdir_ops=$(sudo dmesg | grep "MDT_TIMING.*Operation RMDIR" | wc -l)
    
    echo "OPERATION SUMMARY:" | tee -a $LOGFILE
    echo "  Configuration:" | tee -a $LOGFILE
    echo "    Tree depth: $TREE_DEPTH levels" | tee -a $LOGFILE
    echo "    Branching factor: $BRANCH_FACTOR" | tee -a $LOGFILE
    echo "    Files per leaf: $FILES_PER_LEAF" | tee -a $LOGFILE
    echo "" | tee -a $LOGFILE
    echo "  MDT Operations Captured:" | tee -a $LOGFILE
    echo "    Total MDT_TIMING entries: $total_timing" | tee -a $LOGFILE
    echo "    CREATE operations: $create_ops (directories)" | tee -a $LOGFILE
    echo "    OPEN_FILE_OP operations: $open_file_ops (file creation/writing)" | tee -a $LOGFILE
    echo "    Total OPEN operations: $open_ops (all file opens)" | tee -a $LOGFILE
    echo "    UNLINK operations: $unlink_ops (file removal)" | tee -a $LOGFILE
    echo "    RMDIR operations: $rmdir_ops (directory removal)" | tee -a $LOGFILE
    
    echo "" | tee -a $LOGFILE
    echo "VERIFICATION RESULTS:" | tee -a $LOGFILE
    
    if [ $total_timing -gt 0 ]; then
        echo "✓ SUCCESS: MDT timing logging is working!" | tee -a $LOGFILE
        
        if [ $create_ops -gt 0 ]; then
            echo "✓ CREATE operations detected: $create_ops" | tee -a $LOGFILE
        else
            echo "⚠ WARNING: No CREATE operations detected!" | tee -a $LOGFILE
        fi
        
        if [ $open_file_ops -gt 0 ]; then
            echo "✓ OPEN_FILE_OP operations detected: $open_file_ops" | tee -a $LOGFILE
        else
            echo "⚠ WARNING: No OPEN_FILE_OP operations detected!" | tee -a $LOGFILE
        fi
        
        if [ $unlink_ops -gt 0 ]; then
            echo "✓ UNLINK operations detected: $unlink_ops" | tee -a $LOGFILE
        else
            echo "⚠ WARNING: No UNLINK operations detected!" | tee -a $LOGFILE
        fi
        
        if [ $rmdir_ops -gt 0 ]; then
            echo "✓ RMDIR operations detected: $rmdir_ops" | tee -a $LOGFILE
        else
            echo "⚠ WARNING: No RMDIR operations detected!" | tee -a $LOGFILE
        fi
        
    else
        echo "✗ FAILED: No MDT_TIMING entries found!" | tee -a $LOGFILE
        echo "  Check if the kernel modifications were applied and loaded correctly." | tee -a $LOGFILE
    fi
}

# Main execution starts here
echo "This script will test MDT timing operations by creating and removing a directory tree."
echo ""

# Get user configuration
get_user_input

echo "" | tee -a $LOGFILE
echo "Starting test execution..." | tee -a $LOGFILE

# Clear kernel log
echo "Step 1: Clearing kernel log buffer..." | tee -a $LOGFILE
sudo dmesg -c > /dev/null
echo "Kernel log cleared." | tee -a $LOGFILE

# Create the root test directory
echo "" | tee -a $LOGFILE
echo "Step 2: Creating root test directory..." | tee -a $LOGFILE
mkdir -p "$TESTDIR"
echo "Created root directory: $TESTDIR" | tee -a $LOGFILE

# Create directory tree
echo "" | tee -a $LOGFILE
echo "Step 3: Creating directory tree (depth: $TREE_DEPTH, branching: $BRANCH_FACTOR)..." | tee -a $LOGFILE
create_directory_tree "$TESTDIR" 1 "$TREE_DEPTH" "$BRANCH_FACTOR"
echo "Directory tree creation completed." | tee -a $LOGFILE

show_analysis "After Directory Tree Creation"

# Create files at leaf directories
echo "" | tee -a $LOGFILE
echo "Step 4: Creating files at leaf directories..." | tee -a $LOGFILE
create_files_at_leaves "$TESTDIR" "$TREE_DEPTH" "$BRANCH_FACTOR" "$FILES_PER_LEAF"
echo "File creation completed." | tee -a $LOGFILE

show_analysis "After File Creation"

# Small delay before starting removal
echo "" | tee -a $LOGFILE
echo "Pausing before starting removal process..." | tee -a $LOGFILE
sleep 2

# Remove files
echo "" | tee -a $LOGFILE
echo "Step 5: Removing files from leaf directories..." | tee -a $LOGFILE
remove_files_from_leaves "$TESTDIR" "$TREE_DEPTH"
echo "File removal completed." | tee -a $LOGFILE

show_analysis "After File Removal"

# Remove directory tree
echo "" | tee -a $LOGFILE
echo "Step 6: Removing directory tree..." | tee -a $LOGFILE
remove_directory_tree "$TESTDIR" "$TREE_DEPTH"
echo "Directory tree removal completed." | tee -a $LOGFILE

# Remove root directory
echo "" | tee -a $LOGFILE
echo "Step 7: Removing root test directory..." | tee -a $LOGFILE
rmdir "$TESTDIR" 2>/dev/null || rm -rf "$TESTDIR" 2>/dev/null || true
echo "Root directory removed." | tee -a $LOGFILE

show_analysis "After Complete Removal"

# Show final summary
show_final_summary

echo "" | tee -a $LOGFILE
echo "========================================================================" | tee -a $LOGFILE
echo "Test completed at $(date)" | tee -a $LOGFILE
echo "Results saved to: $LOGFILE"
echo "========================================================================" | tee -a $LOGFILE

echo ""
echo "=== HELPFUL MONITORING COMMANDS ==="
echo "To monitor live MDT operations:"
echo "  sudo dmesg -w | grep 'MDT_TIMING'"
echo ""
echo "To see specific operation types:"
echo "  sudo dmesg | grep 'MDT_TIMING.*CREATE'     # Directory creation"
echo "  sudo dmesg | grep 'MDT_TIMING.*OPEN_FILE_OP' # File operations"
echo "  sudo dmesg | grep 'MDT_TIMING.*UNLINK'     # File removal"
echo "  sudo dmesg | grep 'MDT_TIMING.*RMDIR'      # Directory removal"
echo ""
echo "To see all captured timing data:"
echo "  sudo dmesg | grep 'MDT_TIMING' | tail -20"
