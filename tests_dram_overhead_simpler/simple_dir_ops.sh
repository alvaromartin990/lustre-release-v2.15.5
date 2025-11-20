#!/bin/bash

# Simple directory operations test for DRAM overhead measurement
# Tests: mkdir, stat on directories with timing
# Runs in seconds, not minutes

set -e

# Configuration
LUSTRE_MOUNT="/mnt/lustre"
# Fallback to /tmp if Lustre not available
if [ ! -d "$LUSTRE_MOUNT" ] || [ ! -w "$LUSTRE_MOUNT" ]; then
    LUSTRE_MOUNT="/tmp"
    echo "Warning: Using $LUSTRE_MOUNT instead of Lustre for testing"
fi

TEST_DIR="$LUSTRE_MOUNT/simple_dir_test_$$"
NUM_DIRS=30  # Small number for fast execution
OUTPUT_FILE="simple_dir_ops_results.csv"

function cleanup() {
    rm -rf "$TEST_DIR" 2>/dev/null || true
}

function print_csv_header() {
    echo "timestamp,operation,dir_num,latency_ns,status" > "$OUTPUT_FILE"
}

function get_timestamp() {
    date '+%Y-%m-%d %H:%M:%S.%N'
}

function test_directory_operations() {
    echo "Testing simple directory operations on $NUM_DIRS directories..."
    
    mkdir -p "$TEST_DIR"
    cd "$TEST_DIR"
    
    local total_start=$(date +%s%N)
    
    # Test directory creation (mkdir)
    echo "Phase 1: Creating directories..."
    for i in $(seq 1 $NUM_DIRS); do
        local start=$(date +%s%N)
        local timestamp=$(get_timestamp)
        
        if mkdir "dir_$i" 2>/dev/null; then
            local status="success"
        else
            local status="error"
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,mkdir,$i,$latency,$status" >> "../$OUTPUT_FILE"
    done
    
    # Test directory stat operations
    echo "Phase 2: Stat operations on directories..."
    for i in $(seq 1 $NUM_DIRS); do
        local start=$(date +%s%N)
        local timestamp=$(get_timestamp)
        
        if stat "dir_$i" >/dev/null 2>&1; then
            local status="success"
        else
            local status="error"
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,stat_dir,$i,$latency,$status" >> "../$OUTPUT_FILE"
    done
    
    # Test nested directory operations (create subdirs)
    echo "Phase 3: Creating nested directories..."
    for i in $(seq 1 $NUM_DIRS); do
        local start=$(date +%s%N)
        local timestamp=$(get_timestamp)
        
        if mkdir "dir_$i/subdir" 2>/dev/null; then
            local status="success"
        else
            local status="error"
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,mkdir_nested,$i,$latency,$status" >> "../$OUTPUT_FILE"
    done
    
    # Test listing directory contents
    echo "Phase 4: Listing directory contents..."
    for i in $(seq 1 $NUM_DIRS); do
        local start=$(date +%s%N)
        local timestamp=$(get_timestamp)
        
        if ls "dir_$i" >/dev/null 2>&1; then
            local status="success"
        else
            local status="error"
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,ls_dir,$i,$latency,$status" >> "../$OUTPUT_FILE"
    done
    
    local total_end=$(date +%s%N)
    local total_time=$((total_end - total_start))
    
    echo ""
    echo "=== Simple Directory Operations Test Results ==="
    echo "Total directories processed: $NUM_DIRS"
    echo "Total time: $((total_time / 1000000))ms"
    echo "Operations: mkdir, stat_dir, mkdir_nested, ls_dir"
    echo "Results saved to: $OUTPUT_FILE"
    echo ""
}

function print_summary() {
    if [ ! -f "$OUTPUT_FILE" ]; then
        echo "No results file found!"
        return 1
    fi
    
    echo "Summary of operations:"
    echo "Operation,Count,Avg_Latency_us,Max_Latency_us,Min_Latency_us"
    
    for op in mkdir stat_dir mkdir_nested ls_dir; do
        local count=$(grep ",$op," "$OUTPUT_FILE" | wc -l)
        if [ "$count" -gt 0 ]; then
            local avg=$(grep ",$op," "$OUTPUT_FILE" | awk -F, '{sum+=$4; count++} END {print int(sum/count/1000)}')
            local max=$(grep ",$op," "$OUTPUT_FILE" | awk -F, 'BEGIN{max=0} {if($4>max)max=$4} END {print int(max/1000)}')
            local min=$(grep ",$op," "$OUTPUT_FILE" | awk -F, 'BEGIN{min=999999999} {if($4<min)min=$4} END {print int(min/1000)}')
            echo "$op,$count,$avg,$max,$min"
        fi
    done
}

function main() {
    echo "Starting simple directory operations test at $(date)"
    echo "Test directory: $TEST_DIR"
    echo "Number of directories: $NUM_DIRS"
    echo ""
    
    cleanup
    print_csv_header
    test_directory_operations
    print_summary
    cleanup
    
    echo "Test completed successfully!"
}

# Trap to ensure cleanup on exit
trap cleanup EXIT

main "$@"