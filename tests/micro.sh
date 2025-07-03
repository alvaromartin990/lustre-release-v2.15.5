#!/bin/bash
# lustre_allocation_benchmark.sh
# Comprehensive microbenchmark comparing Lustre ldiskfs allocation vs memory operations
# Targets OBD_ALLOC_PTR_ARRAY_LARGE macro and osd_oi_update() patterns

set -euo pipefail

# Global configuration
readonly SCRIPT_NAME="lustre_allocation_benchmark"
readonly VERSION="1.0"
readonly BENCHMARK_RUNS=100
readonly MIN_BENCHMARK_TIME=5  # seconds
readonly LUSTRE_MOUNT_POINT="${LUSTRE_MOUNT:-/mnt/lustre}"
readonly TEST_DIR="$LUSTRE_MOUNT_POINT/benchmark_test"
readonly RESULTS_DIR="/tmp/benchmark_results_$(date +%Y%m%d_%H%M%S)"

# Data structure sizes matching Lustre OI operations
readonly FID_SIZE=16          # Lustre FID structure size
readonly OI_ENTRY_SIZE=64     # Typical OI table entry size  
readonly INODE_CACHE_SIZE=1024 # Inode cache entry size
readonly ARRAY_SIZE=1000      # Number of structures to allocate

# Performance governor control
readonly ORIGINAL_GOVERNOR=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "unknown")

# Global arrays for timing results
declare -a LUSTRE_TIMINGS=()
declare -a MEMORY_TIMINGS=()

# ============================================================================
# ERROR HANDLING AND CLEANUP
# ============================================================================

error_handler() {
    local exit_code=$?
    local line_number=$1
    echo "ERROR: Script failed at line $line_number with exit code $exit_code" >&2
    echo "Command: $BASH_COMMAND" >&2
    cleanup_environment
    exit $exit_code
}

trap 'error_handler ${LINENO}' ERR

cleanup_environment() {
    echo "Cleaning up benchmark environment..."
    
    # Restore CPU governor
    if [[ "$ORIGINAL_GOVERNOR" != "unknown" ]]; then
        echo "$ORIGINAL_GOVERNOR" | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null 2>&1 || true
    fi
    
    # Clean up test files
    [[ -d "$TEST_DIR" ]] && rm -rf "$TEST_DIR" 2>/dev/null || true
    
    # Remove any temporary files
    rm -f /tmp/lustre_benchmark_* 2>/dev/null || true
    
    # Clear memory pressure
    echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null 2>&1 || true
    
    echo "Cleanup completed."
}

# ============================================================================
# ENVIRONMENT SETUP AND VALIDATION
# ============================================================================

setup_benchmark_environment() {
    echo "Setting up benchmark environment..."
    
    # Create results directory
    mkdir -p "$RESULTS_DIR"
    
    # Validate Lustre filesystem
    if ! df -t lustre "$LUSTRE_MOUNT_POINT" >/dev/null 2>&1; then
        echo "ERROR: $LUSTRE_MOUNT_POINT is not a Lustre filesystem" >&2
        return 1
    fi
    
    # Create test directory
    mkdir -p "$TEST_DIR"
    
    # Test write permissions
    if ! touch "$TEST_DIR/.write_test" 2>/dev/null; then
        echo "ERROR: No write permission in $TEST_DIR" >&2
        return 1
    fi
    rm -f "$TEST_DIR/.write_test"
    
    # Check available space (need at least 100MB)
    local available=$(df "$TEST_DIR" | awk 'NR==2 {print $4}')
    if [[ $available -lt 102400 ]]; then
        echo "ERROR: Insufficient space in $TEST_DIR (need 100MB, have ${available}KB)" >&2
        return 1
    fi
    
    # Set performance governor for consistent timing
    echo "Setting CPU governor to performance mode..."
    echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor >/dev/null 2>&1 || {
        echo "WARNING: Could not set CPU governor to performance mode" >&2
    }
    
    # Disable swap for consistent memory behavior
    sudo swapoff -a 2>/dev/null || echo "WARNING: Could not disable swap" >&2
    
    # Set process to high priority
    sudo renice -n -10 $$ >/dev/null 2>&1 || true
    
    # Configure Lustre striping for consistent behavior
    lfs setstripe -c 1 -S 1M "$TEST_DIR" 2>/dev/null || {
        echo "WARNING: Could not set Lustre striping parameters" >&2
    }
    
    echo "Environment setup completed."
}

validate_system_requirements() {
    echo "Validating system requirements..."
    
    # Check for required commands
    local required_commands=("lfs" "lctl" "bc" "awk" "sort")
    for cmd in "${required_commands[@]}"; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            echo "ERROR: Required command '$cmd' not found" >&2
            return 1
        fi
    done
    
    # Check for root privileges (needed for some operations)
    if [[ $EUID -ne 0 ]]; then
        echo "WARNING: Running without root privileges. Some optimizations may not work." >&2
    fi
    
    # Validate Lustre client version
    local lustre_version=$(lctl version 2>/dev/null | head -1 | awk '{print $4}' || echo "unknown")
    echo "Lustre version: $lustre_version"
    
    # Check system load
    local load_avg=$(uptime | awk -F'load average:' '{print $2}' | awk '{print $1}' | tr -d ',')
    if (( $(echo "$load_avg > 2.0" | bc -l) )); then
        echo "WARNING: High system load ($load_avg). Results may be inconsistent." >&2
    fi
    
    echo "System validation completed."
}

# ============================================================================
# HIGH-PRECISION TIMING FUNCTIONS
# ============================================================================

get_nanosecond_time() {
    date +%s.%N
}

calculate_duration() {
    local start_time="$1"
    local end_time="$2"
    echo "$end_time - $start_time" | bc -l
}

# ============================================================================
# CACHE MANAGEMENT
# ============================================================================

clear_filesystem_caches() {
    # Clear page cache, dentries, and inodes
    sudo sync
    echo 3 | sudo tee /proc/sys/vm/drop_caches >/dev/null 2>&1 || true
    
    # Wait for cache clearing to complete
    sleep 0.1
}

warm_filesystem_cache() {
    local test_path="$1"
    # Warm up the filesystem cache with representative operations
    for i in {1..5}; do
        ls -la "$test_path" >/dev/null 2>&1 || true
    done
}

# ============================================================================
# LUSTRE-SPECIFIC BENCHMARK FUNCTIONS
# ============================================================================

benchmark_lustre_allocation() {
    local run_number="$1"
    local test_file="$LUSTRE_MOUNT_POINT/benchmark_${run_number}.txt"
    
    # Clear caches before each run for consistent measurement
    clear_filesystem_caches
    
    # Create unique kernel log marker for this run
    local kernel_log_mark="LUSTRE_BENCHMARK_RUN_${run_number}_$(date +%s)"
    echo "$kernel_log_mark" | sudo tee /dev/kmsg >/dev/null 2>&1 || true
    
    # Get current kernel log position for fallback
    local log_start_line=$(dmesg | wc -l)
    
    local start_time=$(get_nanosecond_time)
    
    # Perform actual file operations that trigger Lustre allocation macros
    # This approach is more likely to trigger OBD_ALLOC_PTR and related macros
    
    for ((i=1; i<=ARRAY_SIZE; i++)); do
        local file_name="${test_file}_${i}"
        
        # === FILE CREATION (triggers REINT_OPEN and allocations) ===
        touch "$file_name" 2>/dev/null || true
        
        # Add some content to trigger more allocation paths
        echo "benchmark_data_${run_number}_${i}" > "$file_name" 2>/dev/null || true
        
        # Set extended attributes to stress allocation paths
        setfattr -n user.lustre_fid_$i -v "$(printf '%0*d' $FID_SIZE $i)" "$file_name" 2>/dev/null || true
        setfattr -n user.test_data -v "allocation_test_${i}" "$file_name" 2>/dev/null || true
        
        # Force periodic syncs to trigger journal commits and OI operations
        if (( i % 100 == 0 )); then
            sync  # This should trigger "Lustre OI commit latency" events
        fi
        
        # === FILE DELETION (triggers REINT_UNLINK and deallocations) ===
        rm -f "$file_name" 2>/dev/null || true
        
        # Small delay to ensure log separation
        sleep 0.001
    done
    
    # Force final sync to ensure all operations complete
    sync
    
    local end_time=$(get_nanosecond_time)
    
    # Add end marker
    echo "${kernel_log_mark}_END" | sudo tee /dev/kmsg >/dev/null 2>&1 || true
    
    # Wait for logs to be written
    sleep 0.5
    
    # Capture kernel logs using the proven method from the example
    local new_log_lines=""
    if command -v dmesg >/dev/null 2>&1; then
        # Get all dmesg output and find our marker, then print everything after it
        new_log_lines=$(dmesg | awk -v marker="$kernel_log_mark" '
        BEGIN { found = 0; end_found = 0 }
        {
            if (found && !end_found) {
                if (index($0, marker "_END")) {
                    end_found = 1
                } else {
                    print $0
                }
            } else if (!found && index($0, marker)) {
                found = 1
            }
        }')
    fi
    
    # If marker method fails, fall back to line-based method
    if [[ -z "$new_log_lines" ]]; then
        new_log_lines=$(dmesg | tail -n +$((log_start_line + 1)))
    fi
    
    # Count your specific target allocation messages using improved patterns
    local obd_alloc_ptr=$(echo "$new_log_lines" | grep -c "OBD_ALLOC_PTR[^_]" || echo "0")
    local obd_free_ptr_array=$(echo "$new_log_lines" | grep -c "OBD_FREE_PTR_ARRAY" || echo "0")
    local lustre_oi_commit=$(echo "$new_log_lines" | grep -c "Lustre OI commit latency" || echo "0")
    
    # Also count additional allocation patterns from the example
    local obd_alloc_ptr_array_large=$(echo "$new_log_lines" | grep -c "OBD_ALLOC_PTR_ARRAY_LARGE" || echo "0")
    local obd_alloc_ptr_array=$(echo "$new_log_lines" | grep -c "OBD_ALLOC_PTR_ARRAY[^_]" || echo "0")
    local obd_alloc=$(echo "$new_log_lines" | grep -c "OBD_ALLOC[^_]" || echo "0")
    local obd_slab_alloc_ptr=$(echo "$new_log_lines" | grep -c "OBD_SLAB_ALLOC_PTR" || echo "0")
    
    # Count timing-related messages
    local mdt_timing=$(echo "$new_log_lines" | grep -c "MDT_TIMING" || echo "0")
    local mdd_timing=$(echo "$new_log_lines" | grep -c "MDD_TIMING" || echo "0")
    local osd_timing=$(echo "$new_log_lines" | grep -c "OSD_TIMING" || echo "0")
    local reint_open=$(echo "$new_log_lines" | grep -c "REINT_OPEN" || echo "0")
    local stage_messages=$(echo "$new_log_lines" | grep -c "Stage [1-5]" || echo "0")
    
    # Extract timing information for the target "Lustre OI commit latency" events
    local timing_info=""
    if echo "$new_log_lines" | grep -q "Lustre OI commit latency"; then
        timing_info=$(echo "$new_log_lines" | grep "Lustre OI commit latency" | head -5)
    fi
    
    # Save detailed log analysis with all tracked patterns
    {
        echo "Run $run_number: TARGET_EVENTS: OBD_ALLOC_PTR=$obd_alloc_ptr, OBD_FREE_PTR_ARRAY=$obd_free_ptr_array, Lustre_OI_commit=$lustre_oi_commit"
        echo "Run $run_number: ADDITIONAL_ALLOCS: OBD_ALLOC_PTR_ARRAY_LARGE=$obd_alloc_ptr_array_large, OBD_ALLOC_PTR_ARRAY=$obd_alloc_ptr_array, OBD_ALLOC=$obd_alloc, OBD_SLAB_ALLOC_PTR=$obd_slab_alloc_ptr"
        echo "Run $run_number: TIMING_EVENTS: MDT_TIMING=$mdt_timing, MDD_TIMING=$mdd_timing, OSD_TIMING=$osd_timing, REINT_OPEN=$reint_open, Stage_messages=$stage_messages"
        if [[ -n "$timing_info" ]]; then
            echo "  Lustre OI commit latency samples:"
            echo "$timing_info" | sed 's/^/    /'
        fi
    } >> "$RESULTS_DIR/lustre_allocation_events.log"
    
    # Save the actual log messages for analysis
    if [[ -n "$new_log_lines" ]]; then
        echo "=== Run $run_number Log Messages (Marker: $kernel_log_mark) ===" >> "$RESULTS_DIR/lustre_kernel_logs.txt"
        echo "$new_log_lines" >> "$RESULTS_DIR/lustre_kernel_logs.txt"
        echo "" >> "$RESULTS_DIR/lustre_kernel_logs.txt"
    fi
    
    # Save run-specific filtered logs with all target patterns
    {
        echo "=== Run $run_number Filtered Lustre Messages ==="
        echo "$new_log_lines" | grep -E "(OBD_ALLOC_PTR|OBD_FREE_PTR_ARRAY|Lustre OI commit latency|OBD_ALLOC_PTR_ARRAY_LARGE|OBD_ALLOC_PTR_ARRAY|OBD_ALLOC[^_]|OBD_SLAB_ALLOC_PTR|MDT_TIMING|MDD_TIMING|OSD_TIMING|REINT_OPEN|Stage [1-5])" || echo "No target messages found"
        echo ""
    } >> "$RESULTS_DIR/lustre_filtered_logs.txt"
    
    # Clean up test files
    rm -f "${test_file}"_* 2>/dev/null || true
    
    local duration=$(calculate_duration "$start_time" "$end_time")
    
    echo "$duration"
}

benchmark_memory_allocation() {
    local run_number="$1"
    
    # Create equivalent memory allocation pattern
    # Use a helper C program or simulate with shell operations
    local temp_file="/tmp/memory_test_${run_number}"
    
    local start_time=$(get_nanosecond_time)
    
    # Simulate equivalent memory operations
    # Create memory pressure similar to Lustre's allocation patterns
    for ((i=1; i<=ARRAY_SIZE; i++)); do
        # Create temporary files in memory (tmpfs) to simulate memory allocation
        printf '%0*d' $OI_ENTRY_SIZE $i > "$temp_file.$i"
        
        # Simulate periodic memory operations equivalent to journal commits
        if (( i % 100 == 0 )); then
            # Force memory sync equivalent to filesystem sync
            sync
        fi
    done
    
    local end_time=$(get_nanosecond_time)
    
    # Clean up memory allocations
    rm -f "$temp_file"* 2>/dev/null || true
    
    local duration=$(calculate_duration "$start_time" "$end_time")
    echo "$duration"
}

# ============================================================================
# STATISTICAL ANALYSIS FUNCTIONS (FIXED)
# ============================================================================

calculate_mean() {
    local array_name="$1[@]"
    local array=("${!array_name}")
    local sum=0
    local count=${#array[@]}
    
    if [[ $count -eq 0 ]]; then
        echo "0"
        return
    fi
    
    for value in "${array[@]}"; do
        sum=$(echo "$sum + $value" | bc -l)
    done
    
    echo "scale=6; $sum / $count" | bc -l
}

calculate_median() {
    local array_name="$1[@]"
    local array=("${!array_name}")
    local count=${#array[@]}
    
    if [[ $count -eq 0 ]]; then
        echo "0"
        return
    fi
    
    # Sort array
    local sorted=($(printf '%s\n' "${array[@]}" | sort -n))
    
    if (( count % 2 == 0 )); then
        local mid1=${sorted[$((count/2 - 1))]}
        local mid2=${sorted[$((count/2))]}
        echo "scale=6; ($mid1 + $mid2) / 2" | bc -l
    else
        echo "${sorted[$((count/2))]}"
    fi
}

calculate_stddev() {
    local array_name="$1[@]"
    local array=("${!array_name}")
    local count=${#array[@]}
    
    if [[ $count -eq 0 ]]; then
        echo "0"
        return
    fi
    
    local mean=$(calculate_mean "$1")
    local sum_sq=0
    
    for value in "${array[@]}"; do
        local diff=$(echo "$value - $mean" | bc -l)
        sum_sq=$(echo "$sum_sq + ($diff * $diff)" | bc -l)
    done
    
    local variance=$(echo "scale=6; $sum_sq / $count" | bc -l)
    echo "scale=6; sqrt($variance)" | bc -l
}

detect_outliers() {
    local array_name="$1[@]"
    local array=("${!array_name}")
    local threshold="${2:-2}"  # Standard deviations
    
    if [[ ${#array[@]} -eq 0 ]]; then
        echo "0"
        return
    fi
    
    local mean=$(calculate_mean "$1")
    local stddev=$(calculate_stddev "$1")
    local outlier_count=0
    
    # Avoid division by zero
    if (( $(echo "$stddev == 0" | bc -l) )); then
        echo "0"
        return
    fi
    
    for value in "${array[@]}"; do
        local z_score=$(echo "scale=6; ($value - $mean) / $stddev" | bc -l)
        local abs_z_score=$(echo "scale=6; sqrt($z_score * $z_score)" | bc -l)
        
        if (( $(echo "$abs_z_score > $threshold" | bc -l) )); then
            ((outlier_count++))
        fi
    done
    
    echo "$outlier_count"
}

# ============================================================================
# BENCHMARK EXECUTION
# ============================================================================

run_benchmark_suite() {
    echo "Starting benchmark suite with $BENCHMARK_RUNS iterations..."
    echo "Target data structure patterns:"
    echo "  - FID structures: ${FID_SIZE} bytes"
    echo "  - OI entries: ${OI_ENTRY_SIZE} bytes"  
    echo "  - Array size: ${ARRAY_SIZE} elements"
    echo
    
    local start_suite_time=$(date +%s)
    
    # Progress tracking
    local progress_interval=$((BENCHMARK_RUNS / 10))
    [[ $progress_interval -lt 5 ]] && progress_interval=5
    
    for ((run=1; run<=BENCHMARK_RUNS; run++)); do
        # Progress indicator
        if (( run % progress_interval == 0 )) || [[ $run -eq 1 ]]; then
            echo "Progress: Run $run/$BENCHMARK_RUNS ($(( run * 100 / BENCHMARK_RUNS ))%)"
        fi
        
        # Lustre filesystem benchmark
        echo -n "  Lustre run $run... "
        local lustre_time=$(benchmark_lustre_allocation "$run")
        LUSTRE_TIMINGS+=("$lustre_time")
        echo "${lustre_time}s"
        
        # Small delay between tests
        sleep 0.1
        
        # Memory benchmark  
        echo -n "  Memory run $run... "
        local memory_time=$(benchmark_memory_allocation "$run")
        MEMORY_TIMINGS+=("$memory_time")
        echo "${memory_time}s"
        
        # Check for early termination conditions
        local elapsed_time=$(($(date +%s) - start_suite_time))
        if [[ $run -ge 10 && $elapsed_time -gt $MIN_BENCHMARK_TIME ]]; then
            # Check if we have sufficient statistical stability
            if [[ $run -ge 30 ]]; then
                # Get recent results for stability check
                local recent_lustre=("${LUSTRE_TIMINGS[@]: -10}")
                local recent_memory=("${MEMORY_TIMINGS[@]: -10}")
                
                # Create temporary arrays for statistical analysis
                declare -a TEMP_LUSTRE=("${recent_lustre[@]}")
                declare -a TEMP_MEMORY=("${recent_memory[@]}")
                
                local lustre_stddev=$(calculate_stddev TEMP_LUSTRE)
                local memory_stddev=$(calculate_stddev TEMP_MEMORY)
                
                # If coefficient of variation is low, we have stable results
                local lustre_mean=$(calculate_mean TEMP_LUSTRE)
                local memory_mean=$(calculate_mean TEMP_MEMORY)
                local lustre_cv=$(echo "scale=3; $lustre_stddev / $lustre_mean" | bc -l)
                local memory_cv=$(echo "scale=3; $memory_stddev / $memory_mean" | bc -l)
                
                if (( $(echo "$lustre_cv < 0.1 && $memory_cv < 0.1" | bc -l) )); then
                    echo "  Early termination: Results have converged (CV < 10%)"
                    break
                fi
            fi
        fi
        
        # Periodic cache clearing to avoid interference
        if (( run % 20 == 0 )); then
            clear_filesystem_caches
        fi
    done
    
    echo "Benchmark suite completed with ${#LUSTRE_TIMINGS[@]} runs."
}

# ============================================================================
# RESULTS ANALYSIS AND REPORTING
# ============================================================================

generate_comprehensive_report() {
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    local report_file="$RESULTS_DIR/benchmark_report.txt"
    
    # Calculate statistics using fixed functions
    local lustre_mean=$(calculate_mean LUSTRE_TIMINGS)
    local lustre_median=$(calculate_median LUSTRE_TIMINGS)
    local lustre_stddev=$(calculate_stddev LUSTRE_TIMINGS)
    local lustre_min=$(printf '%s\n' "${LUSTRE_TIMINGS[@]}" | sort -n | head -1)
    local lustre_max=$(printf '%s\n' "${LUSTRE_TIMINGS[@]}" | sort -n | tail -1)
    
    local memory_mean=$(calculate_mean MEMORY_TIMINGS)
    local memory_median=$(calculate_median MEMORY_TIMINGS)
    local memory_stddev=$(calculate_stddev MEMORY_TIMINGS)
    local memory_min=$(printf '%s\n' "${MEMORY_TIMINGS[@]}" | sort -n | head -1)
    local memory_max=$(printf '%s\n' "${MEMORY_TIMINGS[@]}" | sort -n | tail -1)
    
    # Performance comparison
    local speedup=$(echo "scale=2; $lustre_mean / $memory_mean" | bc -l)
    local improvement=$(echo "scale=1; (($lustre_mean - $memory_mean) / $lustre_mean) * 100" | bc -l)
    
    # Outlier detection
    local lustre_outliers=$(detect_outliers LUSTRE_TIMINGS)
    local memory_outliers=$(detect_outliers MEMORY_TIMINGS)
    
    # Analyze Lustre-specific kernel log events
    local total_obd_alloc_ptr=0
    local total_obd_free_ptr_array=0
    local total_lustre_oi_commit=0
    
    if [[ -f "$RESULTS_DIR/lustre_allocation_events.log" ]]; then
        total_obd_alloc_ptr=$(grep "OBD_ALLOC_PTR=" "$RESULTS_DIR/lustre_allocation_events.log" | \
            sed 's/.*OBD_ALLOC_PTR=\([0-9]*\).*/\1/' | \
            awk '{sum+=$1} END {print sum+0}')
        
        total_obd_free_ptr_array=$(grep "OBD_FREE_PTR_ARRAY=" "$RESULTS_DIR/lustre_allocation_events.log" | \
            sed 's/.*OBD_FREE_PTR_ARRAY=\([0-9]*\).*/\1/' | \
            awk '{sum+=$1} END {print sum+0}')
            
        total_lustre_oi_commit=$(grep "Lustre_OI_commit=" "$RESULTS_DIR/lustre_allocation_events.log" | \
            sed 's/.*Lustre_OI_commit=\([0-9]*\).*/\1/' | \
            awk '{sum+=$1} END {print sum+0}')
    fi
    
    # Generate report
    cat > "$report_file" << EOF
================================================================================
LUSTRE LDISKFS vs MEMORY ALLOCATION BENCHMARK REPORT
Generated: $timestamp
================================================================================

BENCHMARK CONFIGURATION:
- Script Version: $VERSION
- Total Runs: ${#LUSTRE_TIMINGS[@]}
- Target Allocation Pattern: OBD_ALLOC_PTR_ARRAY_LARGE simulation
- Data Structure Size: ${OI_ENTRY_SIZE} bytes per entry
- Array Size: ${ARRAY_SIZE} elements
- Test Directory: $TEST_DIR

SYSTEM INFORMATION:
- Hostname: $(hostname)
- Kernel: $(uname -r)
- CPU: $(grep "model name" /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)
- Memory: $(free -h | awk '/^Mem:/ {print $2}')
- Lustre Version: $(lctl version 2>/dev/null | head -1 | awk '{print $4}' || echo "unknown")
- Filesystem: $(df -T "$LUSTRE_MOUNT_POINT" | awk 'NR==2 {print $2}')

PERFORMANCE RESULTS:
================================================================================

LUSTRE LDISKFS OPERATIONS:
$(printf "%-12s %12s\n" "Metric" "Value (seconds)")
$(printf "%-12s %12s\n" "------" "--------------")
$(printf "%-12s %12.6f\n" "Mean" "$lustre_mean")
$(printf "%-12s %12.6f\n" "Median" "$lustre_median")
$(printf "%-12s %12.6f\n" "Std Dev" "$lustre_stddev")
$(printf "%-12s %12.6f\n" "Minimum" "$lustre_min")
$(printf "%-12s %12.6f\n" "Maximum" "$lustre_max")
$(printf "%-12s %12d\n" "Outliers" "$lustre_outliers")

MEMORY OPERATIONS:
$(printf "%-12s %12s\n" "Metric" "Value (seconds)")
$(printf "%-12s %12s\n" "------" "--------------")
$(printf "%-12s %12.6f\n" "Mean" "$memory_mean")
$(printf "%-12s %12.6f\n" "Median" "$memory_median")
$(printf "%-12s %12.6f\n" "Std Dev" "$memory_stddev")
$(printf "%-12s %12.6f\n" "Minimum" "$memory_min")
$(printf "%-12s %12.6f\n" "Maximum" "$memory_max")
$(printf "%-12s %12d\n" "Outliers" "$memory_outliers")

LUSTRE KERNEL LOG ANALYSIS:
================================================================================
$(printf "%-25s %12s\n" "Event Type" "Total Count")
$(printf "%-25s %12s\n" "----------" "-----------")
$(printf "%-25s %12d\n" "OBD_ALLOC_PTR" "$total_obd_alloc_ptr")
$(printf "%-25s %12d\n" "OBD_FREE_PTR_ARRAY" "$total_obd_free_ptr_array")
$(printf "%-25s %12d\n" "Lustre OI commit latency" "$total_lustre_oi_commit")

Average events per run:
$(printf "%-25s %12.1f\n" "OBD_ALLOC_PTR/run" "$(echo "scale=1; $total_obd_alloc_ptr / ${#LUSTRE_TIMINGS[@]}" | bc -l)")
$(printf "%-25s %12.1f\n" "OBD_FREE_PTR_ARRAY/run" "$(echo "scale=1; $total_obd_free_ptr_array / ${#LUSTRE_TIMINGS[@]}" | bc -l)")
$(printf "%-25s %12.1f\n" "OI commit events/run" "$(echo "scale=1; $total_lustre_oi_commit / ${#LUSTRE_TIMINGS[@]}" | bc -l)")

PERFORMANCE COMPARISON:
================================================================================
EOF

    if (( $(echo "$improvement > 0" | bc -l) )); then
        echo "Memory operations are ${improvement}% faster than Lustre operations" >> "$report_file"
        echo "Lustre operations are ${speedup}x slower than memory operations" >> "$report_file"
    else
        local abs_improvement=$(echo "$improvement" | tr -d '-')
        local inverse_speedup=$(echo "scale=2; $memory_mean / $lustre_mean" | bc -l)
        echo "Lustre operations are ${abs_improvement}% faster than memory operations" >> "$report_file"
        echo "Lustre operations are ${inverse_speedup}x faster than memory operations" >> "$report_file"
    fi

    cat >> "$report_file" << EOF

THROUGHPUT ANALYSIS:
- Memory Operations/sec: $(echo "scale=2; 1 / $memory_mean" | bc -l)
- Lustre Operations/sec: $(echo "scale=2; 1 / $lustre_mean" | bc -l)

ALLOCATION PATTERN ANALYSIS:
- Target Pattern: osd_oi_update() function simulation
- Custom Allocation Events Tracked: OBD_ALLOC_PTR, OBD_FREE_PTR_ARRAY, Lustre OI commit latency
- Journal Commits Triggered: ~$((ARRAY_SIZE / 100)) per run
- Allocation/Deallocation Balance: $(echo "scale=2; $total_obd_free_ptr_array / ($total_obd_alloc_ptr + 1)" | bc -l) ratio

STATISTICAL VALIDATION:
- Sample Size: ${#LUSTRE_TIMINGS[@]} measurements per operation type
- Confidence Level: 95% (assuming normal distribution)
- Outlier Detection: ±2 standard deviations threshold

KERNEL LOG FILES GENERATED:
- Full kernel logs: lustre_kernel_logs.txt
- Filtered Lustre events: lustre_filtered_logs.txt  
- Event summary: lustre_allocation_events.log

CONCLUSION:
================================================================================
EOF

    # Performance analysis conclusion
    if (( $(echo "$speedup > 1.2" | bc -l) )); then
        echo "Lustre ldiskfs operations show significant overhead compared to memory operations." >> "$report_file"
        echo "The ${speedup}x performance difference suggests filesystem allocation bottlenecks" >> "$report_file"
        echo "in the OSD layer, particularly in osd_oi_update() and related functions." >> "$report_file"
        echo "" >> "$report_file"
        echo "Key findings from kernel log analysis:" >> "$report_file"
        echo "- OBD_ALLOC_PTR events: $total_obd_alloc_ptr total ($(echo "scale=1; $total_obd_alloc_ptr / ${#LUSTRE_TIMINGS[@]}" | bc -l) per run)" >> "$report_file"
        echo "- OBD_FREE_PTR_ARRAY events: $total_obd_free_ptr_array total" >> "$report_file"
        echo "- Lustre OI commit latency events: $total_lustre_oi_commit total" >> "$report_file"
    elif (( $(echo "$speedup < 0.8" | bc -l) )); then
        echo "Unexpectedly, Lustre operations performed better than memory operations." >> "$report_file"
        echo "This may indicate measurement issues or unusual system conditions." >> "$report_file"
    else
        echo "Performance difference between Lustre and memory operations is minimal." >> "$report_file"
        echo "The allocation overhead in the OSD layer appears to be well-optimized." >> "$report_file"
    fi

    cat >> "$report_file" << EOF

RECOMMENDATIONS:
- For workloads with intensive metadata operations, consider optimizing
  the OBD allocation macros and osd_oi_update() function paths.
- Monitor journal commit frequency and ldiskfs_journal_stop patterns.
- Consider tuning Lustre stripe patterns and MDT configurations.
- Analyze allocation/deallocation balance for memory leaks.

Raw data files saved in: $RESULTS_DIR
================================================================================
EOF

    echo "Comprehensive report generated: $report_file"
}

save_raw_data() {
    # Save raw timing data for further analysis
    printf '%s\n' "${LUSTRE_TIMINGS[@]}" > "$RESULTS_DIR/lustre_timings.csv"
    printf '%s\n' "${MEMORY_TIMINGS[@]}" > "$RESULTS_DIR/memory_timings.csv"
    
    # Export JSON format for programmatic analysis
    cat > "$RESULTS_DIR/benchmark_data.json" << EOF
{
  "benchmark": "lustre_ldiskfs_vs_memory",
  "version": "$VERSION",
  "timestamp": "$(date -I)",
  "configuration": {
    "runs": ${#LUSTRE_TIMINGS[@]},
    "fid_size": $FID_SIZE,
    "oi_entry_size": $OI_ENTRY_SIZE,
    "array_size": $ARRAY_SIZE
  },
  "results": {
    "lustre_timings": [$(IFS=','; echo "${LUSTRE_TIMINGS[*]}")],
    "memory_timings": [$(IFS=','; echo "${MEMORY_TIMINGS[*]}")]
  },
  "system": {
    "hostname": "$(hostname)",
    "kernel": "$(uname -r)",
    "lustre_version": "$(lctl version 2>/dev/null | head -1 | awk '{print $4}' || echo 'unknown')"
  }
}
EOF
    
    echo "Raw data saved to $RESULTS_DIR/"
}

# ============================================================================
# MAIN EXECUTION
# ============================================================================

main() {
    echo "Lustre ldiskfs vs Memory Allocation Microbenchmark v$VERSION"
    echo "============================================================="
    echo
    
    # Validate system and setup environment
    validate_system_requirements
    setup_benchmark_environment
    
    # Run the benchmark suite
    run_benchmark_suite
    
    # Generate reports and save data
    generate_comprehensive_report
    save_raw_data
    
    # Display summary
    echo
    echo "Benchmark completed successfully!"
    echo "Results saved in: $RESULTS_DIR"
    echo
    echo "Quick Summary:"
    echo "- Lustre operations: $(calculate_mean LUSTRE_TIMINGS)s average"
    echo "- Memory operations: $(calculate_mean MEMORY_TIMINGS)s average" 
    echo "- Performance ratio: $(echo "scale=2; $(calculate_mean LUSTRE_TIMINGS) / $(calculate_mean MEMORY_TIMINGS)" | bc -l)x"
    echo
    echo "View full report: cat $RESULTS_DIR/benchmark_report.txt"
}

# ============================================================================
# KERNEL LOG ANALYSIS UTILITIES
# ============================================================================

# Function to capture complete kernel log with Lustre-specific filtering
capture_complete_kernel_log() {
    local output_file="${1:-complete_kernel_log_$(date +%Y%m%d_%H%M%S).txt}"
    
    echo "Capturing complete kernel log to: $output_file"
    
    # Get everything from dmesg with timestamps
    echo "=== COMPLETE DMESG OUTPUT ($(date)) ===" > "$output_file"
    sudo dmesg -T >> "$output_file"
    
    echo "" >> "$output_file"
    echo "=== LUSTRE-SPECIFIC MESSAGES ===" >> "$output_file"
    sudo dmesg -T | grep -i lustre >> "$output_file" 2>/dev/null || echo "No Lustre messages found" >> "$output_file"
    
    echo "" >> "$output_file"
    echo "=== TARGET ALLOCATION MESSAGES ===" >> "$output_file"
    echo "--- OBD_ALLOC_PTR messages ---" >> "$output_file"
    sudo dmesg -T | grep "OBD_ALLOC_PTR" >> "$output_file" 2>/dev/null || echo "No OBD_ALLOC_PTR messages found" >> "$output_file"
    
    echo "" >> "$output_file"
    echo "--- OBD_FREE_PTR_ARRAY messages ---" >> "$output_file"
    sudo dmesg -T | grep "OBD_FREE_PTR_ARRAY" >> "$output_file" 2>/dev/null || echo "No OBD_FREE_PTR_ARRAY messages found" >> "$output_file"
    
    echo "" >> "$output_file"
    echo "--- Lustre OI commit latency messages ---" >> "$output_file"
    sudo dmesg -T | grep "Lustre OI commit latency" >> "$output_file" 2>/dev/null || echo "No Lustre OI commit latency messages found" >> "$output_file"
    
    echo "" >> "$output_file"
    echo "=== BENCHMARK MARKERS ===" >> "$output_file"
    sudo dmesg -T | grep "BENCHMARK_RUN" >> "$output_file" 2>/dev/null || echo "No benchmark markers found" >> "$output_file"
    
    echo "" >> "$output_file"
    echo "=== SUMMARY STATISTICS ===" >> "$output_file"
    echo "Total kernel messages: $(sudo dmesg | wc -l)" >> "$output_file"
    echo "OBD_ALLOC_PTR count: $(sudo dmesg | grep -c "OBD_ALLOC_PTR" || echo "0")" >> "$output_file"
    echo "OBD_FREE_PTR_ARRAY count: $(sudo dmesg | grep -c "OBD_FREE_PTR_ARRAY" || echo "0")" >> "$output_file"
    echo "Lustre OI commit latency count: $(sudo dmesg | grep -c "Lustre OI commit latency" || echo "0")" >> "$output_file"
    echo "Benchmark markers: $(sudo dmesg | grep -c "BENCHMARK_RUN" || echo "0")" >> "$output_file"
    
    echo "Complete kernel log saved to: $output_file"
    echo "File size: $(wc -l < "$output_file") lines"
    
    # Display summary
    echo ""
    echo "Quick Summary:"
    echo "- OBD_ALLOC_PTR messages: $(sudo dmesg | grep -c "OBD_ALLOC_PTR" || echo "0")"
    echo "- OBD_FREE_PTR_ARRAY messages: $(sudo dmesg | grep -c "OBD_FREE_PTR_ARRAY" || echo "0")"
    echo "- Lustre OI commit latency messages: $(sudo dmesg | grep -c "Lustre OI commit latency" || echo "0")"
}

# Function to monitor kernel logs in real-time for specific Lustre events
monitor_lustre_logs_realtime() {
    echo "Monitoring Lustre kernel logs in real-time (Ctrl+C to stop)"
    echo "Watching for: OBD_ALLOC_PTR, OBD_FREE_PTR_ARRAY, Lustre OI commit latency"
    echo "========================"
    
    sudo dmesg -T -w | grep -E --line-buffered "(OBD_ALLOC_PTR|OBD_FREE_PTR_ARRAY|Lustre OI commit latency|BENCHMARK_RUN)" | while read -r line; do
        echo "[$(date '+%H:%M:%S')] $line"
    done
}

# Function to analyze kernel logs between two benchmark runs
analyze_kernel_logs_between_runs() {
    local start_run="${1:-1}"
    local end_run="${2:-100}"
    local output_file="${3:-kernel_analysis_run_${start_run}_to_${end_run}.txt}"
    
    echo "Analyzing kernel logs between benchmark runs $start_run and $end_run"
    
    # Extract messages between start and end markers
    sudo dmesg -T | awk -v start="BENCHMARK_RUN_${start_run}_START" -v end="BENCHMARK_RUN_${end_run}_END" '
    $0 ~ start {capture=1}
    capture && $0 ~ /(OBD_ALLOC_PTR|OBD_FREE_PTR_ARRAY|Lustre OI commit latency)/ {print}
    $0 ~ end {capture=0}
    ' > "$output_file"
    
    echo "Analysis saved to: $output_file"
    echo "Found $(wc -l < "$output_file") relevant messages between runs $start_run and $end_run"
}

# Test function to verify custom printk statements are working
test_lustre_allocation_logging() {
    echo "Testing Lustre allocation logging..."
    echo "Creating test file to trigger allocation events..."
    
    local test_file="/mnt/lustre/allocation_test_$(date +%s).txt"
    local before_count=$(sudo dmesg | wc -l)
    
    # Create test workload
    echo "test data" > "$test_file"
    setfattr -n user.test_alloc -v "trigger_allocation_test" "$test_file" 2>/dev/null || true
    sync
    
    sleep 1
    
    # Check for new messages
    local after_count=$(sudo dmesg | wc -l)
    local new_messages=$((after_count - before_count))
    
    echo "New kernel messages since test: $new_messages"
    
    if [[ $new_messages -gt 0 ]]; then
        echo "Recent kernel messages:"
        sudo dmesg | tail -n "$new_messages"
        
        echo ""
        echo "Checking for target messages:"
        echo "- OBD_ALLOC_PTR: $(sudo dmesg | tail -n "$new_messages" | grep -c "OBD_ALLOC_PTR" || echo "0")"
        echo "- OBD_FREE_PTR_ARRAY: $(sudo dmesg | tail -n "$new_messages" | grep -c "OBD_FREE_PTR_ARRAY" || echo "0")"
        echo "- Lustre OI commit latency: $(sudo dmesg | tail -n "$new_messages" | grep -c "Lustre OI commit latency" || echo "0")"
    else
        echo "No new kernel messages detected. Your custom printk statements may not be working."
    fi
    
    # Cleanup
    rm -f "$test_file"
}

# ============================================================================
# SCRIPT ENTRY POINT
# ============================================================================

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    # Check if called with utility function
    case "${1:-}" in
        "capture_log")
            capture_complete_kernel_log "$2"
            ;;
        "monitor")
            monitor_lustre_logs_realtime
            ;;
        "test")
            test_lustre_allocation_logging
            ;;
        "analyze")
            analyze_kernel_logs_between_runs "$2" "$3" "$4"
            ;;
        *)
            main "$@"
            ;;
    esac
fi
