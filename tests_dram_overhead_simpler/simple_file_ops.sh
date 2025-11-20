#!/bin/bash

# Simple file operations test for DRAM overhead measurement
# Tests: touch, stat, getfattr operations with timing
# Runs in seconds, not minutes

set -e

# Configuration
LUSTRE_MOUNT="/mnt/lustre"
# Fallback to /tmp if Lustre not available
if [ ! -d "$LUSTRE_MOUNT" ] || [ ! -w "$LUSTRE_MOUNT" ]; then
    LUSTRE_MOUNT="/tmp"
    echo "Warning: Using $LUSTRE_MOUNT instead of Lustre for testing"
fi

TEST_DIR="$LUSTRE_MOUNT/simple_test_$$"
NUM_FILES=50  # Small number for fast execution
OUTPUT_FILE="simple_file_ops_results.csv"

function cleanup() {
    rm -rf "$TEST_DIR" 2>/dev/null || true
}

function print_csv_header() {
    echo "timestamp,operation,file_num,latency_ns,status" > "$OUTPUT_FILE"
}

function get_timestamp() {
    date '+%Y-%m-%d %H:%M:%S.%N'
}

function test_file_operations() {
    echo "Testing simple file operations on $NUM_FILES files..."
    
    mkdir -p "$TEST_DIR"
    cd "$TEST_DIR"
    
    local total_start=$(date +%s%N)
    
    # Test file creation (touch)
    echo "Phase 1: Creating files..."
    for i in $(seq 1 $NUM_FILES); do
        local start=$(date +%s%N)
        local timestamp=$(get_timestamp)
        
        if touch "file_$i" 2>/dev/null; then
            local status="success"
        else
            local status="error"
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,touch,$i,$latency,$status" >> "../$OUTPUT_FILE"
    done
    
    # Test file stat operations
    echo "Phase 2: Stat operations..."
    for i in $(seq 1 $NUM_FILES); do
        local start=$(date +%s%N)
        local timestamp=$(get_timestamp)
        
        if stat "file_$i" >/dev/null 2>&1; then
            local status="success"
        else
            local status="error"
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,stat,$i,$latency,$status" >> "../$OUTPUT_FILE"
    done
    
    # Test getfattr operations (if available)
    echo "Phase 3: getfattr operations..."
    for i in $(seq 1 $NUM_FILES); do
        local start=$(date +%s%N)
        local timestamp=$(get_timestamp)
        
        if getfattr "file_$i" >/dev/null 2>&1; then
            local status="success"
        else
            local status="error"
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,getfattr,$i,$latency,$status" >> "../$OUTPUT_FILE"
    done
    
    local total_end=$(date +%s%N)
    local total_time=$((total_end - total_start))
    
    echo ""
    echo "=== Simple File Operations Test Results ==="
    echo "Total files processed: $NUM_FILES"
    echo "Total time: $((total_time / 1000000))ms"
    echo "Operations: touch, stat, getfattr"
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
    
    for op in touch stat getfattr; do
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
    echo "Starting simple file operations test at $(date)"
    echo "Test directory: $TEST_DIR"
    echo "Number of files: $NUM_FILES"
    echo ""
    
    cleanup
    print_csv_header
    test_file_operations
    print_summary
    cleanup
    
    echo "Test completed successfully!"
}

# Trap to ensure cleanup on exit
trap cleanup EXIT

main "$@"