#!/bin/bash

set -e

LUSTRE_MOUNT="/mnt/lustre"
# Fallback to tmp for testing if Lustre not available
if [ ! -d "$LUSTRE_MOUNT" ] || [ ! -w "$LUSTRE_MOUNT" ]; then
    LUSTRE_MOUNT="/tmp"
    echo "Warning: Using $LUSTRE_MOUNT instead of Lustre for testing"
fi
TEST_DIR="$LUSTRE_MOUNT/dram_test_lookups"
NUM_FILES=500
NUM_LOOKUPS=2000
OUTPUT_FILE="lookups_results.csv"

function cleanup() {
    rm -rf "$TEST_DIR" 2>/dev/null || true
}

function print_header() {
    echo "timestamp,operation,iteration,latency_ns,total_ops,errors" > "$OUTPUT_FILE"
}

function setup_test_files() {
    echo "Setting up test files for lookup tests..."
    mkdir -p "$TEST_DIR"
    cd "$TEST_DIR"
    
    # Create files with varying name patterns
    for i in $(seq 1 $NUM_FILES); do
        touch "file_$i"
        touch "long_filename_pattern_$i.txt"
        mkdir -p "subdir_$i"
        touch "subdir_$i/nested_file_$i"
    done
    
    # Create some deep directory structures
    for i in $(seq 1 50); do
        mkdir -p "deep/level1/level2/level3/level4_$i"
        touch "deep/level1/level2/level3/level4_$i/deepfile_$i"
    done
}

function test_simple_lookups() {
    echo "Testing simple file lookups..."
    
    local errors=0
    local start_total=$(date +%s%N)
    
    for i in $(seq 1 $NUM_LOOKUPS); do
        local file_num=$(((i % NUM_FILES) + 1))
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! test -f "file_$file_num" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,simple_lookup,$i,$latency,$NUM_LOOKUPS,$errors" >> "../$OUTPUT_FILE"
        
        if [ $((i % 200)) -eq 0 ]; then
            echo "Completed $i simple lookups..."
        fi
    done
    
    local end_total=$(date +%s%N)
    local total_time=$((end_total - start_total))
    
    echo "Simple lookups: $((total_time / 1000000))ms total, $((total_time / NUM_LOOKUPS / 1000))μs avg"
}

function test_path_lookups() {
    echo "Testing path resolution lookups..."
    
    local errors=0
    
    for i in $(seq 1 500); do
        local file_num=$(((i % NUM_FILES) + 1))
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! test -f "subdir_$file_num/nested_file_$file_num" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,nested_lookup,$i,$latency,500,$errors" >> "../$OUTPUT_FILE"
    done
    
    echo "Nested path lookups completed"
}

function test_deep_lookups() {
    echo "Testing deep path lookups..."
    
    local errors=0
    
    for i in $(seq 1 200); do
        local file_num=$(((i % 50) + 1))
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! test -f "deep/level1/level2/level3/level4_$file_num/deepfile_$file_num" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,deep_lookup,$i,$latency,200,$errors" >> "../$OUTPUT_FILE"
    done
    
    echo "Deep path lookups completed"
}

function test_negative_lookups() {
    echo "Testing negative lookups (non-existent files)..."
    
    local errors=0
    
    for i in $(seq 1 300); do
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        # Intentionally look for non-existent files
        if test -f "nonexistent_file_$i" 2>/dev/null; then
            errors=$((errors + 1))  # This would be unexpected
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,negative_lookup,$i,$latency,300,$errors" >> "../$OUTPUT_FILE"
    done
    
    echo "Negative lookups completed"
}

function test_wildcard_operations() {
    echo "Testing wildcard pattern matching..."
    
    local errors=0
    
    for i in $(seq 1 100); do
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! ls file_* > /dev/null 2>&1; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,wildcard_lookup,$i,$latency,100,$errors" >> "../$OUTPUT_FILE"
        
        start=$(date +%s%N)
        timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! find . -name "*file_*" -type f > /dev/null 2>&1; then
            errors=$((errors + 1))
        fi
        
        end=$(date +%s%N)
        latency=$((end - start))
        
        echo "$timestamp,find_lookup,$i,$latency,100,$errors" >> "../$OUTPUT_FILE"
    done
    
    echo "Wildcard operations completed"
}

function test_readdir_operations() {
    echo "Testing readdir operations..."
    
    local errors=0
    
    for i in $(seq 1 100); do
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! ls -1 . | head -10 > /dev/null 2>&1; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,readdir,$i,$latency,100,$errors" >> "../$OUTPUT_FILE"
    done
    
    echo "Readdir operations completed"
}

function main() {
    if [ ! -d "$LUSTRE_MOUNT" ]; then
        echo "Error: Lustre mount point $LUSTRE_MOUNT not found"
        exit 1
    fi
    
    echo "Starting lookup performance overhead test at $(date)"
    echo "Lustre mount: $LUSTRE_MOUNT"
    echo "Test directory: $TEST_DIR"
    
    cleanup
    print_header
    setup_test_files
    test_simple_lookups
    test_path_lookups
    test_deep_lookups
    test_negative_lookups
    test_wildcard_operations
    test_readdir_operations
    cleanup
    
    echo ""
    echo "=== Lookup Performance Test Results ==="
    echo "Results saved to: $OUTPUT_FILE"
    echo ""
}

main "$@"