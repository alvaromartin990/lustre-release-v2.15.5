#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LUSTRE_MOUNT="/mnt/lustre"
# Fallback to tmp for testing if Lustre not available
if [ ! -d "$LUSTRE_MOUNT" ] || [ ! -w "$LUSTRE_MOUNT" ]; then
    LUSTRE_MOUNT="/tmp"
    echo "Warning: Using $LUSTRE_MOUNT instead of Lustre for testing"
fi
RESULTS_DIR="$SCRIPT_DIR/results_$(date +%Y%m%d_%H%M%S)"
SUMMARY_FILE="$RESULTS_DIR/test_summary.txt"

function check_prerequisites() {
    echo "Checking prerequisites..."
    
    if [ ! -d "$LUSTRE_MOUNT" ]; then
        echo "ERROR: Lustre mount point $LUSTRE_MOUNT not found"
        echo "Please ensure Lustre is mounted before running tests"
        exit 1
    fi
    
    if [ ! -w "$LUSTRE_MOUNT" ]; then
        echo "ERROR: No write permission to $LUSTRE_MOUNT"
        echo "Please check Lustre mount permissions"
        exit 1
    fi
    
    # Check for required tools
    for tool in touch mkdir stat chmod setfattr getfattr time; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo "ERROR: Required tool '$tool' not found"
            exit 1
        fi
    done
    
    echo "Prerequisites check passed"
}

function setup_results_directory() {
    echo "Setting up results directory: $RESULTS_DIR"
    mkdir -p "$RESULTS_DIR"
    
    # Copy test scripts to results directory for reference
    cp "$SCRIPT_DIR"/*.sh "$RESULTS_DIR/"
    
    echo "Test run started at $(date)" > "$SUMMARY_FILE"
    echo "Lustre mount point: $LUSTRE_MOUNT" >> "$SUMMARY_FILE"
    echo "Results directory: $RESULTS_DIR" >> "$SUMMARY_FILE"
    echo "" >> "$SUMMARY_FILE"
}

function run_test() {
    local test_script="$1"
    local test_name="$2"
    
    echo "=========================================="
    echo "Running $test_name test..."
    echo "=========================================="
    
    local start_time=$(date +%s)
    local test_start=$(date)
    
    # Change to results directory to capture output files there
    cd "$RESULTS_DIR"
    
    echo "Test: $test_name" >> "$SUMMARY_FILE"
    echo "Started: $test_start" >> "$SUMMARY_FILE"
    
    # Run the test and capture both stdout and stderr
    if timeout 1800 bash "$SCRIPT_DIR/$test_script" > "${test_name}_output.log" 2>&1; then
        local end_time=$(date +%s)
        local duration=$((end_time - start_time))
        local test_end=$(date)
        
        echo "Completed: $test_end" >> "$SUMMARY_FILE"
        echo "Duration: ${duration}s" >> "$SUMMARY_FILE"
        echo "Status: SUCCESS" >> "$SUMMARY_FILE"
        echo "" >> "$SUMMARY_FILE"
        
        echo "$test_name completed successfully in ${duration}s"
        
        # Move CSV result file if it exists
        for csv_file in *.csv; do
            if [ -f "$csv_file" ] && [[ "$csv_file" == *"results.csv" ]]; then
                mv "$csv_file" "${test_name}_results.csv"
            fi
        done
        
        return 0
    else
        local end_time=$(date +%s)
        local duration=$((end_time - start_time))
        local test_end=$(date)
        
        echo "Completed: $test_end" >> "$SUMMARY_FILE"
        echo "Duration: ${duration}s" >> "$SUMMARY_FILE"
        echo "Status: FAILED/TIMEOUT" >> "$SUMMARY_FILE"
        echo "" >> "$SUMMARY_FILE"
        
        echo "ERROR: $test_name failed or timed out after ${duration}s"
        return 1
    fi
}

function generate_combined_results() {
    echo "Generating combined results..."
    
    cd "$RESULTS_DIR"
    
    # Create combined CSV file
    local combined_csv="combined_results.csv"
    echo "test_name,timestamp,operation,iteration,latency_ns,details,errors" > "$combined_csv"
    
    for csv_file in *_results.csv; do
        if [ -f "$csv_file" ]; then
            local test_name=$(echo "$csv_file" | sed 's/_results.csv$//')
            # Skip header and add test name prefix
            tail -n +2 "$csv_file" | sed "s/^/$test_name,/" >> "$combined_csv"
        fi
    done
    
    echo "Combined results saved to: $combined_csv"
}

function print_summary_statistics() {
    echo "=========================================="
    echo "Generating summary statistics..."
    echo "=========================================="
    
    cd "$RESULTS_DIR"
    
    echo "" >> "$SUMMARY_FILE"
    echo "=== SUMMARY STATISTICS ===" >> "$SUMMARY_FILE"
    
    for csv_file in *_results.csv; do
        if [ -f "$csv_file" ]; then
            local test_name=$(echo "$csv_file" | sed 's/_results.csv$//')
            echo "" >> "$SUMMARY_FILE"
            echo "--- $test_name ---" >> "$SUMMARY_FILE"
            
            # Count total operations
            local total_ops=$(tail -n +2 "$csv_file" | wc -l)
            echo "Total operations: $total_ops" >> "$SUMMARY_FILE"
            
            # Count errors
            local total_errors=$(tail -n +2 "$csv_file" | awk -F',' '{sum += $NF} END {print sum+0}')
            echo "Total errors: $total_errors" >> "$SUMMARY_FILE"
            
            # Calculate latency statistics (convert from ns to μs)
            tail -n +2 "$csv_file" | awk -F',' '{print $4/1000}' | sort -n > temp_latencies.txt
            
            if [ -s temp_latencies.txt ]; then
                local min_latency=$(head -n 1 temp_latencies.txt)
                local max_latency=$(tail -n 1 temp_latencies.txt)
                local median_latency=$(awk 'NR == int((NF+1)/2) {print}' temp_latencies.txt)
                local avg_latency=$(awk '{sum += $1; count++} END {print sum/count}' temp_latencies.txt)
                
                printf "Min latency: %.2fμs\n" "$min_latency" >> "$SUMMARY_FILE"
                printf "Max latency: %.2fμs\n" "$max_latency" >> "$SUMMARY_FILE"
                printf "Median latency: %.2fμs\n" "$median_latency" >> "$SUMMARY_FILE"
                printf "Average latency: %.2fμs\n" "$avg_latency" >> "$SUMMARY_FILE"
                
                # Calculate 95th and 99th percentiles
                local p95_line=$((total_ops * 95 / 100))
                local p99_line=$((total_ops * 99 / 100))
                local p95_latency=$(sed -n "${p95_line}p" temp_latencies.txt)
                local p99_latency=$(sed -n "${p99_line}p" temp_latencies.txt)
                
                printf "95th percentile: %.2fμs\n" "$p95_latency" >> "$SUMMARY_FILE"
                printf "99th percentile: %.2fμs\n" "$p99_latency" >> "$SUMMARY_FILE"
            fi
            
            rm -f temp_latencies.txt
        fi
    done
    
    echo "" >> "$SUMMARY_FILE"
    echo "Test run completed at $(date)" >> "$SUMMARY_FILE"
}

function cleanup_lustre() {
    echo "Cleaning up any remaining test files on Lustre..."
    rm -rf "$LUSTRE_MOUNT"/dram_test_* 2>/dev/null || true
}

function main() {
    local run_quick=false
    
    # Parse command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --quick)
                run_quick=true
                shift
                ;;
            --help|-h)
                echo "Usage: $0 [--quick] [--help]"
                echo ""
                echo "Options:"
                echo "  --quick    Run abbreviated tests for quick validation"
                echo "  --help     Show this help message"
                echo ""
                echo "This script runs a comprehensive suite of Lustre metadata overhead tests"
                echo "to measure performance impact of mmap-backed DRAM changes."
                exit 0
                ;;
            *)
                echo "Unknown option: $1"
                echo "Use --help for usage information"
                exit 1
                ;;
        esac
    done
    
    echo "========================================"
    echo "Lustre DRAM Overhead Test Suite"
    echo "========================================"
    echo "Started at: $(date)"
    
    if [ "$run_quick" = true ]; then
        echo "Running in QUICK mode"
    fi
    
    check_prerequisites
    setup_results_directory
    cleanup_lustre
    
    local failed_tests=0
    local total_tests=0
    
    # Test definitions: script_name:display_name
    local tests=(
        "test_file_creation.sh:file_creation"
        "test_directory_creation.sh:directory_creation"
        "test_attributes.sh:attributes"
        "test_lookups.sh:lookups"
        "test_fid_stress.sh:fid_stress"
        "test_fld_cache.sh:fld_cache"
        "test_repeated_metadata.sh:repeated_metadata"
    )
    
    # Override for quick mode
    if [ "$run_quick" = true ]; then
        echo "Quick mode: reducing test iterations..."
        # In quick mode, we would need to modify the test scripts
        # or create quick versions. For now, just run fewer tests.
        tests=(
            "test_file_creation.sh:file_creation"
            "test_attributes.sh:attributes"
            "test_lookups.sh:lookups"
        )
    fi
    
    for test_def in "${tests[@]}"; do
        IFS=':' read -r script_name display_name <<< "$test_def"
        total_tests=$((total_tests + 1))
        
        if ! run_test "$script_name" "$display_name"; then
            failed_tests=$((failed_tests + 1))
        fi
        
        # Small delay between tests
        sleep 2
    done
    
    generate_combined_results
    print_summary_statistics
    cleanup_lustre
    
    echo ""
    echo "========================================"
    echo "Test Suite Complete"
    echo "========================================"
    echo "Total tests: $total_tests"
    echo "Failed tests: $failed_tests"
    echo "Success rate: $(( (total_tests - failed_tests) * 100 / total_tests ))%"
    echo "Results saved to: $RESULTS_DIR"
    echo "Summary: $SUMMARY_FILE"
    echo "Completed at: $(date)"
    
    if [ $failed_tests -eq 0 ]; then
        echo "All tests completed successfully!"
        exit 0
    else
        echo "Some tests failed. Check individual test logs for details."
        exit 1
    fi
}

main "$@"