#!/bin/bash

set -e

LUSTRE_MOUNT="/mnt/lustre"
# Fallback to tmp for testing if Lustre not available
if [ ! -d "$LUSTRE_MOUNT" ] || [ ! -w "$LUSTRE_MOUNT" ]; then
    LUSTRE_MOUNT="/tmp"
    echo "Warning: Using $LUSTRE_MOUNT instead of Lustre for testing"
fi
TEST_DIR="$LUSTRE_MOUNT/dram_test_fid_stress"
NUM_FILES=2000
NUM_CONCURRENT=10
OUTPUT_FILE="fid_stress_results.csv"

function cleanup() {
    rm -rf "$TEST_DIR" 2>/dev/null || true
    killall -9 create_files_batch 2>/dev/null || true
}

function print_header() {
    echo "timestamp,operation,worker_id,file_num,latency_ns,total_files,errors" > "$OUTPUT_FILE"
}

function create_files_batch() {
    local worker_id=$1
    local start_file=$2
    local end_file=$3
    local batch_dir="$TEST_DIR/worker_$worker_id"
    
    mkdir -p "$batch_dir"
    cd "$batch_dir"
    
    local errors=0
    
    for i in $(seq $start_file $end_file); do
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        # Create file - this will trigger FID allocation in Lustre
        if ! touch "fid_test_file_${worker_id}_${i}" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,fid_create,$worker_id,$i,$latency,$NUM_FILES,$errors" >> "../../$OUTPUT_FILE"
        
        # Immediately get file attributes to trigger FID lookups
        start=$(date +%s%N)
        timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! stat "fid_test_file_${worker_id}_${i}" > /dev/null 2>&1; then
            errors=$((errors + 1))
        fi
        
        end=$(date +%s%N)
        latency=$((end - start))
        
        echo "$timestamp,fid_lookup,$worker_id,$i,$latency,$NUM_FILES,$errors" >> "../../$OUTPUT_FILE"
        
        # Create and delete rapidly to stress FID recycling
        if [ $((i % 10)) -eq 0 ]; then
            start=$(date +%s%N)
            timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
            
            if ! rm "fid_test_file_${worker_id}_${i}" 2>/dev/null; then
                errors=$((errors + 1))
            fi
            
            end=$(date +%s%N)
            latency=$((end - start))
            
            echo "$timestamp,fid_delete,$worker_id,$i,$latency,$NUM_FILES,$errors" >> "../../$OUTPUT_FILE"
            
            # Recreate immediately
            start=$(date +%s%N)
            timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
            
            if ! touch "fid_test_file_${worker_id}_${i}_recreated" 2>/dev/null; then
                errors=$((errors + 1))
            fi
            
            end=$(date +%s%N)
            latency=$((end - start))
            
            echo "$timestamp,fid_recreate,$worker_id,$i,$latency,$NUM_FILES,$errors" >> "../../$OUTPUT_FILE"
        fi
    done
}

function test_concurrent_fid_creation() {
    echo "Testing concurrent FID creation with $NUM_CONCURRENT workers..."
    
    local files_per_worker=$((NUM_FILES / NUM_CONCURRENT))
    local start_total=$(date +%s%N)
    
    # Launch concurrent workers
    for worker in $(seq 1 $NUM_CONCURRENT); do
        local start_file=$(((worker - 1) * files_per_worker + 1))
        local end_file=$((worker * files_per_worker))
        
        echo "Starting worker $worker (files $start_file to $end_file)..."
        create_files_batch $worker $start_file $end_file &
    done
    
    # Wait for all workers to complete
    echo "Waiting for all workers to complete..."
    wait
    
    local end_total=$(date +%s%N)
    local total_time=$((end_total - start_total))
    
    echo ""
    echo "Concurrent FID creation completed in $((total_time / 1000000))ms"
}

function test_fid_sequence_stress() {
    echo "Testing FID sequence allocation patterns..."
    
    local seq_dir="$TEST_DIR/sequence_test"
    mkdir -p "$seq_dir"
    cd "$seq_dir"
    
    local errors=0
    
    # Rapid file creation to stress sequence allocation
    for i in $(seq 1 500); do
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        # Create multiple files rapidly to trigger sequence allocation
        if ! (touch "seq_${i}_a" && touch "seq_${i}_b" && touch "seq_${i}_c") 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,fid_sequence,$i,0,$latency,500,$errors" >> "../../$OUTPUT_FILE"
    done
    
    echo "FID sequence stress test completed"
}

function test_hardlink_operations() {
    echo "Testing hardlink operations (shared FIDs)..."
    
    local link_dir="$TEST_DIR/hardlink_test"
    mkdir -p "$link_dir"
    cd "$link_dir"
    
    local errors=0
    
    # Create base files
    for i in $(seq 1 100); do
        touch "base_file_$i"
    done
    
    # Create hardlinks and measure FID handling
    for i in $(seq 1 100); do
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! ln "base_file_$i" "hardlink_$i" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,hardlink_create,$i,0,$latency,100,$errors" >> "../../$OUTPUT_FILE"
        
        # Stat both files to test FID consistency
        start=$(date +%s%N)
        timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! (stat "base_file_$i" && stat "hardlink_$i") > /dev/null 2>&1; then
            errors=$((errors + 1))
        fi
        
        end=$(date +%s%N)
        latency=$((end - start))
        
        echo "$timestamp,hardlink_stat,$i,0,$latency,100,$errors" >> "../../$OUTPUT_FILE"
    done
    
    echo "Hardlink operations completed"
}

function test_symlink_operations() {
    echo "Testing symlink operations..."
    
    local sym_dir="$TEST_DIR/symlink_test"
    mkdir -p "$sym_dir"
    cd "$sym_dir"
    
    local errors=0
    
    # Create symlinks and measure metadata overhead
    for i in $(seq 1 200); do
        touch "target_file_$i"
        
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! ln -s "target_file_$i" "symlink_$i" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,symlink_create,$i,0,$latency,200,$errors" >> "../../$OUTPUT_FILE"
        
        # Test symlink resolution
        start=$(date +%s%N)
        timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! readlink "symlink_$i" > /dev/null 2>&1; then
            errors=$((errors + 1))
        fi
        
        end=$(date +%s%N)
        latency=$((end - start))
        
        echo "$timestamp,symlink_readlink,$i,0,$latency,200,$errors" >> "../../$OUTPUT_FILE"
    done
    
    echo "Symlink operations completed"
}

function main() {
    if [ ! -d "$LUSTRE_MOUNT" ]; then
        echo "Error: Lustre mount point $LUSTRE_MOUNT not found"
        exit 1
    fi
    
    echo "Starting FID creation stress test at $(date)"
    echo "Lustre mount: $LUSTRE_MOUNT"
    echo "Test directory: $TEST_DIR"
    echo "Concurrent workers: $NUM_CONCURRENT"
    echo "Total files: $NUM_FILES"
    
    cleanup
    mkdir -p "$TEST_DIR"
    print_header
    
    test_concurrent_fid_creation
    test_fid_sequence_stress
    test_hardlink_operations
    test_symlink_operations
    
    cleanup
    
    echo ""
    echo "=== FID Stress Test Results ==="
    echo "Results saved to: $OUTPUT_FILE"
    echo ""
}

main "$@"