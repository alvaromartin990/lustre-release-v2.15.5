#!/bin/bash

set -e

LUSTRE_MOUNT="/mnt/lustre"
TEST_DIR="$LUSTRE_MOUNT/dram_test_fld"
NUM_OPERATIONS=1000
OUTPUT_FILE="fld_cache_results.csv"

function cleanup() {
    rm -rf "$TEST_DIR" 2>/dev/null || true
}

function print_header() {
    echo "timestamp,operation,iteration,latency_ns,cache_status,errors" > "$OUTPUT_FILE"
}

function test_fld_cache_pressure() {
    echo "Testing FLD cache pressure with frequent metadata operations..."
    
    mkdir -p "$TEST_DIR"
    cd "$TEST_DIR"
    
    local errors=0
    
    # Create files across different directories to stress FLD cache
    for i in $(seq 1 $NUM_OPERATIONS); do
        local dir_num=$((i % 50 + 1))
        local subdir="fld_test_dir_$dir_num"
        
        mkdir -p "$subdir"
        
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        # Create file in subdirectory - should trigger FLD lookups
        if ! touch "$subdir/fld_file_$i" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,fld_file_create,$i,$latency,unknown,$errors" >> "../$OUTPUT_FILE"
        
        # Immediately access the file to test FLD cache hit/miss
        start=$(date +%s%N)
        timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! ls -l "$subdir/fld_file_$i" > /dev/null 2>&1; then
            errors=$((errors + 1))
        fi
        
        end=$(date +%s%N)
        latency=$((end - start))
        
        echo "$timestamp,fld_cache_access,$i,$latency,potential_hit,$errors" >> "../$OUTPUT_FILE"
        
        if [ $((i % 100)) -eq 0 ]; then
            echo "Completed $i FLD operations..."
        fi
    done
    
    echo "FLD cache pressure test completed"
}

function test_cross_directory_operations() {
    echo "Testing cross-directory operations to stress FLD cache..."
    
    local errors=0
    
    # Create multiple directory trees
    for tree in $(seq 1 10); do
        mkdir -p "tree_$tree/level1/level2"
    done
    
    # Perform operations that jump between trees
    for i in $(seq 1 500); do
        local tree1=$((i % 10 + 1))
        local tree2=$(((i + 5) % 10 + 1))
        
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        # Create file in first tree
        if ! touch "tree_$tree1/level1/level2/cross_file_${i}_a" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,cross_dir_create_a,$i,$latency,cache_miss,$errors" >> "../$OUTPUT_FILE"
        
        start=$(date +%s%N)
        timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        # Immediately create file in different tree (likely cache miss)
        if ! touch "tree_$tree2/level1/level2/cross_file_${i}_b" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        end=$(date +%s%N)
        latency=$((end - start))
        
        echo "$timestamp,cross_dir_create_b,$i,$latency,cache_miss,$errors" >> "../$OUTPUT_FILE"
        
        # Go back to first tree (test cache behavior)
        start=$(date +%s%N)
        timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! ls "tree_$tree1/level1/level2/cross_file_${i}_a" > /dev/null 2>&1; then
            errors=$((errors + 1))
        fi
        
        end=$(date +%s%N)
        latency=$((end - start))
        
        echo "$timestamp,cross_dir_access,$i,$latency,cache_hit_test,$errors" >> "../$OUTPUT_FILE"
    done
    
    echo "Cross-directory operations completed"
}

function test_fld_cache_warming() {
    echo "Testing FLD cache warming patterns..."
    
    local warm_dir="fld_warm_test"
    mkdir -p "$warm_dir"
    cd "$warm_dir"
    
    local errors=0
    
    # Phase 1: Cold cache operations
    echo "Phase 1: Cold cache operations"
    for i in $(seq 1 100); do
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        mkdir -p "cold_dir_$i"
        if ! touch "cold_dir_$i/cold_file_$i" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,cold_cache_op,$i,$latency,cold,$errors" >> "../../$OUTPUT_FILE"
    done
    
    # Phase 2: Warm cache operations (repeat access)
    echo "Phase 2: Warm cache operations"
    for i in $(seq 1 100); do
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        if ! ls "cold_dir_$i/cold_file_$i" > /dev/null 2>&1; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,warm_cache_op,$i,$latency,warm,$errors" >> "../../$OUTPUT_FILE"
    done
    
    # Phase 3: Cache eviction test
    echo "Phase 3: Cache eviction pressure"
    for i in $(seq 1 200); do
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        mkdir -p "evict_dir_$i"
        if ! touch "evict_dir_$i/evict_file_$i" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,cache_eviction,$i,$latency,evicting,$errors" >> "../../$OUTPUT_FILE"
    done
    
    cd ..
    echo "Cache warming test completed"
}

function test_fld_sequence_locality() {
    echo "Testing FLD sequence locality patterns..."
    
    local seq_dir="fld_sequence_test"
    mkdir -p "$seq_dir"
    cd "$seq_dir"
    
    local errors=0
    
    # Test sequential access patterns (good locality)
    for i in $(seq 1 200); do
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        # Sequential file creation in same directory
        if ! touch "seq_file_$i" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,sequential_access,$i,$latency,good_locality,$errors" >> "../../$OUTPUT_FILE"
    done
    
    # Test random access patterns (poor locality)
    for i in $(seq 1 200); do
        local random_dir=$((RANDOM % 50 + 1))
        
        mkdir -p "random_dir_$random_dir"
        
        local start=$(date +%s%N)
        local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
        
        # Random directory access
        if ! touch "random_dir_$random_dir/random_file_$i" 2>/dev/null; then
            errors=$((errors + 1))
        fi
        
        local end=$(date +%s%N)
        local latency=$((end - start))
        
        echo "$timestamp,random_access,$i,$latency,poor_locality,$errors" >> "../../$OUTPUT_FILE"
    done
    
    cd ..
    echo "Sequence locality test completed"
}

function test_concurrent_fld_stress() {
    echo "Testing concurrent FLD stress..."
    
    local conc_dir="fld_concurrent_test"
    mkdir -p "$conc_dir"
    
    # Launch multiple background processes
    for worker in $(seq 1 5); do
        (
            cd "$conc_dir"
            mkdir -p "worker_$worker"
            
            for i in $(seq 1 100); do
                local start=$(date +%s%N)
                local timestamp=$(date '+%Y-%m-%d %H:%M:%S.%N')
                
                if ! touch "worker_$worker/concurrent_file_${worker}_$i" 2>/dev/null; then
                    local errors=1
                else
                    local errors=0
                fi
                
                local end=$(date +%s%N)
                local latency=$((end - start))
                
                echo "$timestamp,concurrent_fld,$worker,$latency,concurrent,$errors" >> "../../$OUTPUT_FILE"
                
                # Small delay to avoid overwhelming
                sleep 0.01
            done
        ) &
    done
    
    # Wait for all workers
    wait
    
    echo "Concurrent FLD stress completed"
}

function main() {
    if [ ! -d "$LUSTRE_MOUNT" ]; then
        echo "Error: Lustre mount point $LUSTRE_MOUNT not found"
        exit 1
    fi
    
    echo "Starting FLD cache lookup test at $(date)"
    echo "Lustre mount: $LUSTRE_MOUNT"
    echo "Test directory: $TEST_DIR"
    
    cleanup
    print_header
    
    test_fld_cache_pressure
    test_cross_directory_operations
    test_fld_cache_warming
    test_fld_sequence_locality
    test_concurrent_fld_stress
    
    cleanup
    
    echo ""
    echo "=== FLD Cache Test Results ==="
    echo "Results saved to: $OUTPUT_FILE"
    echo ""
}

main "$@"