#!/bin/bash

# Enhanced test script for MDT node identification
# Tests whether we can identify which specific MDT node performs each operation

TESTDIR="/mnt/lustre/mdt_node_test_$(date +%s)"
LOGFILE="/tmp/mdt_node_test_$(date +%s).log"
TEST_FILES=50  # Number of test files to create

echo "=== Enhanced MDT Node Identification Test ===" | tee -a $LOGFILE
echo "Started at $(date)" | tee -a $LOGFILE
echo "Test directory: $TESTDIR" | tee -a $LOGFILE
echo "Testing enhanced logging to identify which MDT node performs operations" | tee -a $LOGFILE
echo "Creating $TEST_FILES test files to potentially hit different MDT nodes" | tee -a $LOGFILE
echo "============================================================================" | tee -a $LOGFILE

# Function to show current MDT node statistics
show_mdt_stats() {
    local test_name="$1"
    echo "" | tee -a $LOGFILE
    echo "=== $test_name ===" | tee -a $LOGFILE
    
    # Count total operations
    local total_ops=$(sudo dmesg | grep "MDT_TIMING.*\[MDT:" | wc -l)
    echo "Total MDT operations captured: $total_ops" | tee -a $LOGFILE
    
    if [ $total_ops -eq 0 ]; then
        echo "WARNING: No MDT_TIMING entries found!" | tee -a $LOGFILE
        return
    fi
    
    # Show unique MDT nodes that performed operations
    echo "MDT Nodes that performed operations:" | tee -a $LOGFILE
    sudo dmesg | grep "MDT_TIMING.*\[MDT:" | grep -o "\[MDT:[^]]*\]" | sort | uniq -c | tee -a $LOGFILE
    
    # Show operations by type
    echo "" | tee -a $LOGFILE
    echo "Operations by type:" | tee -a $LOGFILE
    sudo dmesg | grep "MDT_TIMING.*Operation" | sed 's/.*Operation \([A-Z_]*\).*/\1/' | sort | uniq -c | tee -a $LOGFILE
    
    echo "---" | tee -a $LOGFILE
}

# Function to analyze operations per MDT node
analyze_per_mdt() {
    echo "" | tee -a $LOGFILE
    echo "=== DETAILED ANALYSIS PER MDT NODE ===" | tee -a $LOGFILE
    
    # Get unique MDT identifiers
    local mdt_nodes=$(sudo dmesg | grep "MDT_TIMING.*\[MDT:" | grep -o "\[MDT:[^]]*\]" | sort | uniq)
    
    if [ -z "$mdt_nodes" ]; then
        echo "No MDT nodes found in logs!" | tee -a $LOGFILE
        return
    fi
    
    echo "Found MDT nodes: $mdt_nodes" | tee -a $LOGFILE
    echo "" | tee -a $LOGFILE
    
    # Analyze each MDT node separately
    while IFS= read -r mdt_node; do
        if [ -n "$mdt_node" ]; then
            echo "=== Analysis for $mdt_node ===" | tee -a $LOGFILE
            
            # Count operations for this MDT
            local ops_count=$(sudo dmesg | grep "MDT_TIMING.*$mdt_node" | wc -l)
            echo "Total operations: $ops_count" | tee -a $LOGFILE
            
            # Show operation breakdown for this MDT
            echo "Operation breakdown:" | tee -a $LOGFILE
            sudo dmesg | grep "MDT_TIMING.*$mdt_node" | grep -o "Operation [A-Z_]*" | sort | uniq -c | tee -a $LOGFILE
            
            # Show timing statistics for this MDT
            echo "Timing statistics (microseconds):" | tee -a $LOGFILE
            local times=$(sudo dmesg | grep "MDT_TIMING.*$mdt_node" | grep -o "took [0-9]* microseconds" | grep -o "[0-9]*")
            if [ -n "$times" ]; then
                echo "$times" | awk '{
                    sum += $1; 
                    count++; 
                    if(min == "" || $1 < min) min = $1; 
                    if(max == "" || $1 > max) max = $1;
                } 
                END {
                    if(count > 0) {
                        printf "  Min: %d μs, Max: %d μs, Avg: %.1f μs, Count: %d\n", min, max, sum/count, count
                    }
                }' | tee -a $LOGFILE
            fi
            
            echo "" | tee -a $LOGFILE
        fi
    done <<< "$mdt_nodes"
}

# Function to create files that might hit different MDTs
create_test_files() {
    local operation_name="$1"
    local num_files="$2"
    
    echo "Creating $num_files files for $operation_name test..." | tee -a $LOGFILE
    
    for i in $(seq 1 $num_files); do
        local filename="${TESTDIR}/${operation_name}_file_${i}"
        
        # Create file (this should trigger CREATE/OPEN operations)
        touch "$filename"
        
        # Add some content (might trigger additional operations)
        echo "Test content for file $i created at $(date)" > "$filename"
        
        # Set extended attributes (should trigger SETXATTR operations)
        setfattr -n user.test_attr -v "test_value_${i}" "$filename" 2>/dev/null || true
        
        # Get file attributes (should trigger GETATTR operations)
        stat "$filename" >/dev/null 2>&1 || true
        getfattr -n user.test_attr "$filename" >/dev/null 2>&1 || true
        
        # Small delay to separate operations clearly in logs
        sleep 0.02
        
        # Show progress every 10 files
        if [ $((i % 10)) -eq 0 ]; then
            echo "  Created $i/$num_files files..." | tee -a $LOGFILE
        fi
    done
}

# Function to test various operations
test_operations() {
    echo "Testing various operations to see MDT node distribution..." | tee -a $LOGFILE
    
    # Test 1: Create files in main directory
    create_test_files "main" 1
    show_mdt_stats "After creating files in main directory"
    
    # Test 2: Create subdirectories (might hit different MDTs)
    # echo "Creating subdirectories..." | tee -a $LOGFILE
    # for i in {1..5}; do
    #     local subdir="${TESTDIR}/subdir_${i}"
    #     mkdir -p "$subdir"
        
    #     # Create files in subdirectory
    #     create_test_files "sub${i}" 8
        
    #     # Test directory operations
    #     ls -la "$subdir" >/dev/null 2>&1 || true
    #     stat "$subdir" >/dev/null 2>&1 || true
    # done
    # show_mdt_stats "After creating subdirectories and files"
    
    # Test 3: File modifications (SETATTR operations)
    # echo "Testing file modifications..." | tee -a $LOGFILE
    # for file in "${TESTDIR}"/main_file_{1..10}; do
    #     if [ -f "$file" ]; then
    #         chmod 755 "$file" 2>/dev/null || true
    #         chown $(whoami) "$file" 2>/dev/null || true
    #         touch "$file"  # Update timestamps
    #     fi
    #     sleep 0.01
    # done
    # show_mdt_stats "After file modifications"
    
    # # Test 4: File removals (UNLINK operations)
    # echo "Testing file removals..." | tee -a $LOGFILE
    # for file in "${TESTDIR}"/main_file_{11..15}; do
    #     if [ -f "$file" ]; then
    #         rm -f "$file" 2>/dev/null || true
    #     fi
    #     sleep 0.01
    # done
    # show_mdt_stats "After file removals"
}

# Clear kernel log and start fresh
echo "Clearing kernel log buffer..." | tee -a $LOGFILE
sudo dmesg -c > /dev/null

echo "Creating test directory: $TESTDIR" | tee -a $LOGFILE
mkdir -p "$TESTDIR"

# Wait a moment for any initial operations to complete
sleep 1

echo "Starting MDT node identification tests..." | tee -a $LOGFILE
echo "============================================================================" | tee -a $LOGFILE

# Run the tests
test_operations

echo "" | tee -a $LOGFILE
echo "============================================================================" | tee -a $LOGFILE
echo "=== FINAL ANALYSIS ===" | tee -a $LOGFILE

# Show all MDT_TIMING entries
echo "" | tee -a $LOGFILE
echo "All captured MDT_TIMING entries:" | tee -a $LOGFILE
sudo dmesg | grep "MDT_TIMING" | tee -a $LOGFILE

# Detailed analysis per MDT node
analyze_per_mdt

echo "" | tee -a $LOGFILE
echo "=== VERIFICATION RESULTS ===" | tee -a $LOGFILE

# Verify that we can identify MDT nodes
total_entries=$(sudo dmesg | grep "MDT_TIMING.*\[MDT:" | wc -l)
unique_mdts=$(sudo dmesg | grep "MDT_TIMING.*\[MDT:" | grep -o "\[MDT:[^]]*\]" | sort | uniq | wc -l)

echo "Total MDT operations logged: $total_entries" | tee -a $LOGFILE
echo "Number of unique MDT nodes identified: $unique_mdts" | tee -a $LOGFILE

if [ $total_entries -gt 0 ]; then
    echo "✓ SUCCESS: MDT timing logging is working!" | tee -a $LOGFILE
    
    if [ $unique_mdts -gt 1 ]; then
        echo "✓ EXCELLENT: Multiple MDT nodes detected - node identification working!" | tee -a $LOGFILE
    elif [ $unique_mdts -eq 1 ]; then
        echo "ℹ INFO: Only one MDT node detected. This could mean:" | tee -a $LOGFILE
        echo "  - You have a single-MDT setup" | tee -a $LOGFILE
        echo "  - All operations went to the same MDT" | tee -a $LOGFILE
        echo "  - Operations were not distributed across MDTs" | tee -a $LOGFILE
    else
        echo "⚠ WARNING: No MDT node information found in logs" | tee -a $LOGFILE
    fi
else
    echo "✗ FAILED: No MDT_TIMING entries found!" | tee -a $LOGFILE
    echo "Check if the code modifications were applied correctly." | tee -a $LOGFILE
fi

echo "" | tee -a $LOGFILE
echo "=== SUMMARY OF OPERATIONS BY MDT ===" | tee -a $LOGFILE
if [ $total_entries -gt 0 ]; then
    sudo dmesg | grep "MDT_TIMING.*\[MDT:" | grep -o "\[MDT:[^]]*\] Operation [A-Z_]*" | sort | uniq -c | tee -a $LOGFILE
fi

echo "" | tee -a $LOGFILE
echo "============================================================================" | tee -a $LOGFILE
echo "Test completed at $(date)" | tee -a $LOGFILE
echo "Results saved to: $LOGFILE"
echo "" 
echo "To check for MDT distribution in your Lustre setup, run:"
echo "  lfs getdirstripe $TESTDIR"
echo "  lctl get_param mdc.*.active"
echo ""
echo "To see live MDT operations:"
echo "  sudo dmesg -w | grep MDT_TIMING"

# Optional: Clean up (uncomment if you want to remove test files)
# echo "Cleaning up test directory..."
# rm -rf "$TESTDIR" 2>/dev/null || true
