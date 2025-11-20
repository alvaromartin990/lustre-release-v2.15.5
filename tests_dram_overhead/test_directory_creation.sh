#!/bin/bash

set -e

LUSTRE_MOUNT="/mnt/lustre"
TEST_DIR="$LUSTRE_MOUNT/dram_test_dirs"
NUM_DIRS=500
OUTPUT_FILE="directory_creation_results.csv"

function cleanup() {
    rm -rf "$TEST_DIR" 2>/dev/null || true
}

function print_header() {
    echo "timestamp,operation,dir_num,latency_ns,total_dirs,errors" > "$OUTPUT_FILE"
}

function test_directory_creation() {
    echo "Testing directory creation latency with $NUM_DIRS directories..."
    
    mkdir -p "$TEST_DIR"
    cd "$TEST_DIR"
    
    local errors=0
    local start_total=$(date +%s%N)
    
    for i in $(seq 1 $NUM_DIRS); do
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! mkdir "test_dir_$i" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,dir_create,$i,$latency,$NUM_DIRS,$errors" >> "../$OUTPUT_FILE"
        
        if [ $((i % 50)) -eq 0 ]; then
            echo "Created $i directories..."
        fi
    done
    
    echo "Testing nested directory creation..."
    for i in $(seq 1 100); do
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! mkdir -p "nested/level$i/deep" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,nested_dir_create,$i,$latency,100,$errors" >> "../$OUTPUT_FILE"
        
        rm -rf "nested" 2>/dev/null || true
    done
    
    local end_total=$(date +%s%N)
    local total_time=$((end_total - start_total))
    local avg_latency=$((total_time / (NUM_DIRS + 100)))
    
    echo ""
    echo "=== Directory Creation Test Results ==="
    echo "Total directories: $((NUM_DIRS + 100))"
    echo "Total time: $((total_time / 1000000))ms"
    echo "Average latency: $((avg_latency / 1000))μs"
    echo "Errors: $errors"
    echo "Throughput: $(((NUM_DIRS + 100) * 1000000000 / total_time)) dirs/sec"
    echo "Results saved to: $OUTPUT_FILE"
    echo ""
}

function main() {
    if [ ! -d "$LUSTRE_MOUNT" ]; then
        echo "Error: Lustre mount point $LUSTRE_MOUNT not found"
        exit 1
    fi
    
    echo "Starting directory creation overhead test at $(date)"
    echo "Lustre mount: $LUSTRE_MOUNT"
    echo "Test directory: $TEST_DIR"
    
    cleanup
    print_header
    test_directory_creation
    cleanup
}

main "$@"