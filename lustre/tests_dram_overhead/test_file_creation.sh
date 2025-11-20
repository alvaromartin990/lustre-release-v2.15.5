#!/bin/bash

set -e

LUSTRE_MOUNT="/mnt/lustre"
TEST_DIR="$LUSTRE_MOUNT/dram_test_files"
NUM_FILES=1000
OUTPUT_FILE="file_creation_results.csv"

function cleanup() {
    rm -rf "$TEST_DIR" 2>/dev/null || true
}

function print_header() {
    echo "timestamp,operation,file_num,latency_ns,total_files,errors" > "$OUTPUT_FILE"
}

function test_file_creation() {
    echo "Testing file creation latency with $NUM_FILES files..."
    
    mkdir -p "$TEST_DIR"
    cd "$TEST_DIR"
    
    local errors=0
    local start_total=$(date +%s%N)
    
    for i in $(seq 1 $NUM_FILES); do
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! touch "test_file_$i" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,file_create,$i,$latency,$NUM_FILES,$errors" >> "../$OUTPUT_FILE"
        
        if [ $((i % 100)) -eq 0 ]; then
            echo "Created $i files..."
        fi
    done
    
    local end_total=$(date +%s%N)
    local total_time=$((end_total - start_total))
    local avg_latency=$((total_time / NUM_FILES))
    
    echo ""
    echo "=== File Creation Test Results ==="
    echo "Total files: $NUM_FILES"
    echo "Total time: $((total_time / 1000000))ms"
    echo "Average latency: $((avg_latency / 1000))μs"
    echo "Errors: $errors"
    echo "Throughput: $((NUM_FILES * 1000000000 / total_time)) files/sec"
    echo "Results saved to: $OUTPUT_FILE"
    echo ""
}

function main() {
    if [ ! -d "$LUSTRE_MOUNT" ]; then
        echo "Error: Lustre mount point $LUSTRE_MOUNT not found"
        exit 1
    fi
    
    echo "Starting file creation overhead test at $(date)"
    echo "Lustre mount: $LUSTRE_MOUNT"
    echo "Test directory: $TEST_DIR"
    
    cleanup
    print_header
    test_file_creation
    cleanup
}

main "$@"