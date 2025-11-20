#!/bin/bash

set -e

LUSTRE_MOUNT="/mnt/lustre"
# Fallback to tmp for testing if Lustre not available
if [ ! -d "$LUSTRE_MOUNT" ] || [ ! -w "$LUSTRE_MOUNT" ]; then
    LUSTRE_MOUNT="/tmp"
    echo "Warning: Using $LUSTRE_MOUNT instead of Lustre for testing"
fi
TEST_DIR="$LUSTRE_MOUNT/dram_test_repeated"
NUM_ITERATIONS=1000
OUTPUT_FILE="repeated_metadata_results.csv"

function cleanup() {
    rm -rf "$TEST_DIR" 2>/dev/null || true
}

function print_header() {
    echo "timestamp,operation,iteration,latency_ns,operation_type,errors" > "$OUTPUT_FILE"
}

function test_repeated_create_delete() {
    echo "Testing repeated create/delete cycles..."
    
    mkdir -p "$TEST_DIR"
    cd "$TEST_DIR"
    
    local errors=0
    
    for i in $(seq 1 $NUM_ITERATIONS); do
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        # Create file
        if ! touch "repeat_file_$i" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local create_latency=$((end - start))
        
        echo "$timestamp,repeated_create,$i,$create_latency,create_delete_cycle,$errors" >> "../$OUTPUT_FILE"
        
        # Small delay to ensure different timestamp
        sleep 0.001
        
        start=$(date +%s%N)
        timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        # Delete file
        if ! rm "repeat_file_$i" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        end=$(date +%s%N)
        local delete_latency=$((end - start))
        
        echo "$timestamp,repeated_delete,$i,$delete_latency,create_delete_cycle,$errors" >> "../$OUTPUT_FILE"
        
        if [ $((i % 100)) -eq 0 ]; then
            echo "Completed $i create/delete cycles..."
        fi
    done
    
    echo "Create/delete cycles completed"
}

function test_repeated_stat_calls() {
    echo "Testing repeated stat operations on same files..."
    
    local stat_dir="stat_test"
    mkdir -p "$stat_dir"
    cd "$stat_dir"
    
    # Create test files
    for i in $(seq 1 50); do
        echo "test content $i" > "stat_test_file_$i"
    done
    
    local errors=0
    
    # Repeatedly stat the same files
    for i in $(seq 1 $NUM_ITERATIONS); do
        local file_num=$(((i % 50) + 1))
        
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! stat "stat_test_file_$file_num" > /dev/null 2>&1; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,repeated_stat,$i,$latency,stat_operations,$errors" >> "../../$OUTPUT_FILE"
    done
    
    cd ..
    echo "Repeated stat operations completed"
}

function test_repeated_chmod_operations() {
    echo "Testing repeated chmod operations..."
    
    local chmod_dir="chmod_test"
    mkdir -p "$chmod_dir"
    cd "$chmod_dir"
    
    # Create test files
    for i in $(seq 1 20); do
        touch "chmod_test_file_$i"
    done
    
    local errors=0
    
    for i in $(seq 1 400); do
        local file_num=$(((i % 20) + 1))
        local mode=$((644 + (i % 100)))
        
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! chmod "$mode" "chmod_test_file_$file_num" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,repeated_chmod,$i,$latency,chmod_operations,$errors" >> "../../$OUTPUT_FILE"
    done
    
    cd ..
    echo "Repeated chmod operations completed"
}

function test_repeated_xattr_operations() {
    echo "Testing repeated extended attribute operations..."
    
    local xattr_dir="xattr_test"
    mkdir -p "$xattr_dir"
    cd "$xattr_dir"
    
    # Create test files
    for i in $(seq 1 30); do
        touch "xattr_test_file_$i"
    done
    
    local errors=0
    
    for i in $(seq 1 600); do
        local file_num=$(((i % 30) + 1))
        
        # Set extended attribute
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! setfattr -n "user.test_attr_$i" -v "value_$i" "xattr_test_file_$file_num" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,repeated_setfattr,$i,$latency,xattr_operations,$errors" >> "../../$OUTPUT_FILE"
        
        # Get extended attribute
        start=$(date +%s%N)
        timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! getfattr -n "user.test_attr_$i" "xattr_test_file_$file_num" > /dev/null 2>&1; then
            errors=$((errors + 1))
        fi
        
        end=$(date +%s%N)
        latency=$((end - start))
        
        echo "$timestamp,repeated_getfattr,$i,$latency,xattr_operations,$errors" >> "../../$OUTPUT_FILE"
        
        # Remove extended attribute every 10th operation
        if [ $((i % 10)) -eq 0 ]; then
            start=$(date +%s%N)
            timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
            
            if ! setfattr -x "user.test_attr_$i" "xattr_test_file_$file_num" 2>/dev/null; then
                errors=$((errors + 1))
            fi
            
            end=$(date +%s%N)
            latency=$((end - start))
            
            echo "$timestamp,repeated_removefattr,$i,$latency,xattr_operations,$errors" >> "../../$OUTPUT_FILE"
        fi
    done
    
    cd ..
    echo "Repeated xattr operations completed"
}

function test_repeated_directory_operations() {
    echo "Testing repeated directory operations..."
    
    local dir_ops_dir="dir_ops_test"
    mkdir -p "$dir_ops_dir"
    cd "$dir_ops_dir"
    
    local errors=0
    
    for i in $(seq 1 500); do
        # Create directory
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! mkdir "temp_dir_$i" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,repeated_mkdir,$i,$latency,directory_ops,$errors" >> "../../$OUTPUT_FILE"
        
        # List directory
        start=$(date +%s%N)
        timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! ls -la "temp_dir_$i" > /dev/null 2>&1; then
            errors=$((errors + 1))
        fi
        
        end=$(date +%s%N)
        latency=$((end - start))
        
        echo "$timestamp,repeated_ls,$i,$latency,directory_ops,$errors" >> "../../$OUTPUT_FILE"
        
        # Remove directory
        start=$(date +%s%N)
        timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! rmdir "temp_dir_$i" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        end=$(date +%s%N)
        latency=$((end - start))
        
        echo "$timestamp,repeated_rmdir,$i,$latency,directory_ops,$errors" >> "../../$OUTPUT_FILE"
    done
    
    cd ..
    echo "Repeated directory operations completed"
}

function test_mixed_metadata_workload() {
    echo "Testing mixed metadata workload patterns..."
    
    local mixed_dir="mixed_workload_test"
    mkdir -p "$mixed_dir"
    cd "$mixed_dir"
    
    local errors=0
    
    for i in $(seq 1 $NUM_ITERATIONS); do
        local op_type=$((i % 6))
        
        case $op_type in
            0)
                # File creation
                local start=$(date +%s%N)
                local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
                
                if ! touch "mixed_file_$i" 2>/dev/null; then
                    errors=$((errors + 1))
                fi
                
                local end=$(date +%s%N)
                local latency=$((end - start))
                
                echo "$timestamp,mixed_create,$i,$latency,mixed_workload,$errors" >> "../../$OUTPUT_FILE"
                ;;
            1)
                # Stat operation
                local target_file="mixed_file_$(((i / 6) * 6))"
                if [ -f "$target_file" ]; then
                    local start=$(date +%s%N)
                    local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
                    
                    if ! stat "$target_file" > /dev/null 2>&1; then
                        errors=$((errors + 1))
                    fi
                    
                    local end=$(date +%s%N)
                    local latency=$((end - start))
                    
                    echo "$timestamp,mixed_stat,$i,$latency,mixed_workload,$errors" >> "../../$OUTPUT_FILE"
                fi
                ;;
            2)
                # Directory creation
                local start=$(date +%s%N)
                local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
                
                if ! mkdir "mixed_dir_$i" 2>/dev/null; then
                    errors=$((errors + 1))
                fi
                
                local end=$(date +%s%N)
                local latency=$((end - start))
                
                echo "$timestamp,mixed_mkdir,$i,$latency,mixed_workload,$errors" >> "../../$OUTPUT_FILE"
                ;;
            3)
                # Chmod operation
                local target_file="mixed_file_$(((i / 6) * 6))"
                if [ -f "$target_file" ]; then
                    local start=$(date +%s%N)
                    local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
                    
                    local mode=$((644 + (i % 100)))
                    if ! chmod "$mode" "$target_file" 2>/dev/null; then
                        errors=$((errors + 1))
                    fi
                    
                    local end=$(date +%s%N)
                    local latency=$((end - start))
                    
                    echo "$timestamp,mixed_chmod,$i,$latency,mixed_workload,$errors" >> "../../$OUTPUT_FILE"
                fi
                ;;
            4)
                # Extended attribute
                local target_file="mixed_file_$(((i / 6) * 6))"
                if [ -f "$target_file" ]; then
                    local start=$(date +%s%N)
                    local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
                    
                    if ! setfattr -n "user.mixed_attr" -v "mixed_value_$i" "$target_file" 2>/dev/null; then
                        errors=$((errors + 1))
                    fi
                    
                    local end=$(date +%s%N)
                    local latency=$((end - start))
                    
                    echo "$timestamp,mixed_xattr,$i,$latency,mixed_workload,$errors" >> "../../$OUTPUT_FILE"
                fi
                ;;
            5)
                # List directory
                local start=$(date +%s%N)
                local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
                
                if ! ls -1 . > /dev/null 2>&1; then
                    errors=$((errors + 1))
                fi
                
                local end=$(date +%s%N)
                local latency=$((end - start))
                
                echo "$timestamp,mixed_ls,$i,$latency,mixed_workload,$errors" >> "../../$OUTPUT_FILE"
                ;;
        esac
        
        if [ $((i % 200)) -eq 0 ]; then
            echo "Completed $i mixed operations..."
        fi
    done
    
    cd ..
    echo "Mixed metadata workload completed"
}

function main() {
    if [ ! -d "$LUSTRE_MOUNT" ]; then
        echo "Error: Lustre mount point $LUSTRE_MOUNT not found"
        exit 1
    fi
    
    echo "Starting repeated metadata operations test at $(date)"
    echo "Lustre mount: $LUSTRE_MOUNT"
    echo "Test directory: $TEST_DIR"
    echo "Iterations: $NUM_ITERATIONS"
    
    cleanup
    print_header
    
    test_repeated_create_delete
    test_repeated_stat_calls
    test_repeated_chmod_operations
    test_repeated_xattr_operations
    test_repeated_directory_operations
    test_mixed_metadata_workload
    
    cleanup
    
    echo ""
    echo "=== Repeated Metadata Operations Test Results ==="
    echo "Results saved to: $OUTPUT_FILE"
    echo ""
}

main "$@"