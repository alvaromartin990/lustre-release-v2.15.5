#!/bin/bash

set -e

LUSTRE_MOUNT="/mnt/lustre"
TEST_DIR="$LUSTRE_MOUNT/dram_test_attrs"
NUM_FILES=200
NUM_ATTR_OPS=1000
OUTPUT_FILE="attributes_results.csv"

function cleanup() {
    rm -rf "$TEST_DIR" 2>/dev/null || true
}

function print_header() {
    echo "timestamp,operation,iteration,latency_ns,total_ops,errors" > "$OUTPUT_FILE"
}

function setup_test_files() {
    echo "Setting up test files..."
    mkdir -p "$TEST_DIR"
    cd "$TEST_DIR"
    
    for i in $(seq 1 $NUM_FILES); do
        touch "testfile_$i"
        echo "test content for file $i" > "testfile_$i"
    done
}

function test_stat_operations() {
    echo "Testing stat() operations..."
    
    local errors=0
    local start_total=$(date +%s%N)
    
    for i in $(seq 1 $NUM_ATTR_OPS); do
        local file_num=$(((i % NUM_FILES) + 1))
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! stat "testfile_$file_num" > /dev/null 2>&1; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,stat,$i,$latency,$NUM_ATTR_OPS,$errors" >> "../$OUTPUT_FILE"
    done
    
    local end_total=$(date +%s%N)
    local total_time=$((end_total - start_total))
    
    echo "Stat operations: $((total_time / 1000000))ms total, $((total_time / NUM_ATTR_OPS / 1000))μs avg"
}

function test_xattr_operations() {
    echo "Testing extended attributes..."
    
    local errors=0
    
    for i in $(seq 1 200); do
        local file_num=$(((i % NUM_FILES) + 1))
        
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! setfattr -n user.test_attr -v "test_value_$i" "testfile_$file_num" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,setfattr,$i,$latency,200,$errors" >> "../$OUTPUT_FILE"
        
        start=$(date +%s%N)
        timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! getfattr -n user.test_attr "testfile_$file_num" > /dev/null 2>&1; then
            errors=$((errors + 1))
        fi
        
        end=$(date +%s%N)
        latency=$((end - start))
        
        echo "$timestamp,getfattr,$i,$latency,200,$errors" >> "../$OUTPUT_FILE"
    done
    
    echo "Extended attribute operations completed"
}

function test_chmod_chown() {
    echo "Testing chmod/chown operations..."
    
    local errors=0
    
    for i in $(seq 1 100); do
        local file_num=$(((i % NUM_FILES) + 1))
        
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        local mode=$((644 + (i % 100)))
        if ! chmod "$mode" "testfile_$file_num" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,chmod,$i,$latency,100,$errors" >> "../$OUTPUT_FILE"
        
        start=$(date +%s%N)
        timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! touch -t "$(date +%Y%m%d%H%M.%S)" "testfile_$file_num" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        end=$(date +%s%N)
        latency=$((end - start))
        
        echo "$timestamp,touch_time,$i,$latency,100,$errors" >> "../$OUTPUT_FILE"
    done
    
    echo "Permission/timestamp operations completed"
}

function test_ls_operations() {
    echo "Testing directory listing operations..."
    
    local errors=0
    
    for i in $(seq 1 50); do
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! ls -la . > /dev/null 2>&1; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,ls_la,$i,$latency,50,$errors" >> "../$OUTPUT_FILE"
        
        start=$(date +%s%N)
        timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! ls -1 . | wc -l > /dev/null 2>&1; then
            errors=$((errors + 1))
        fi
        
        end=$(date +%s%N)
        latency=$((end - start))
        
        echo "$timestamp,ls_count,$i,$latency,50,$errors" >> "../$OUTPUT_FILE"
    done
    
    echo "Directory listing operations completed"
}

function main() {
    if [ ! -d "$LUSTRE_MOUNT" ]; then
        echo "Error: Lustre mount point $LUSTRE_MOUNT not found"
        exit 1
    fi
    
    echo "Starting attribute operations overhead test at $(date)"
    echo "Lustre mount: $LUSTRE_MOUNT"
    echo "Test directory: $TEST_DIR"
    
    cleanup
    print_header
    setup_test_files
    test_stat_operations
    test_xattr_operations
    test_chmod_chown
    test_ls_operations
    cleanup
    
    echo ""
    echo "=== Attribute Operations Test Results ==="
    echo "Results saved to: $OUTPUT_FILE"
    echo ""
}

main "$@"