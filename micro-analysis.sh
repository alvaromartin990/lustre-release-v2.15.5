#!/bin/bash

# Complete Lustre Memory Benchmark Execution and Analysis Script
# This script compiles, runs, and analyzes the enhanced micro-user benchmark

set -euo pipefail

# Configuration
BENCHMARK_SOURCE="micro-user-v2.c"
BENCHMARK_BINARY="micro-user-v2"
ANALYSIS_SCRIPT="micro-analysis.py"
OUTPUT_DIR="benchmark_results_$(date +%Y%m%d_%H%M%S)"
LOG_MARKER="LUSTRE_BENCHMARK_$(date +%s)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Usage function
usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Options:
    -h, --help              Show this help message
    -o, --output-dir DIR    Output directory (default: benchmark_results_TIMESTAMP)
    -c, --compile-only      Only compile the benchmark
    -r, --run-only          Only run the benchmark (assumes already compiled)
    -a, --analyze-only      Only analyze existing results
    -k, --kernel-logs       Capture kernel logs during benchmark
    -v, --verbose           Verbose output
    -t, --threads NUM       Number of threads for multithreaded tests
    --no-plots             Skip plot generation
    --iterations NUM        Number of iterations for stress tests
    --min-size NUM          Minimum array size for stress tests
    --max-size NUM          Maximum array size for stress tests

Examples:
    $0                      # Run complete benchmark with analysis
    $0 -k                   # Run with kernel log capture
    $0 -a -o results_dir    # Analyze existing results
    $0 -c                   # Compile only
EOF
}

# Check dependencies
check_dependencies() {
    log_info "Checking dependencies..."
    
    # Check for GCC
    if ! command -v gcc &> /dev/null; then
        log_error "GCC not found. Please install GCC."
        exit 1
    fi
    
    # Check for Python3
    if ! command -v python3 &> /dev/null; then
        log_error "Python3 not found. Please install Python3."
        exit 1
    fi
    
    # Check for required Python packages
    python3 -c "import pandas, matplotlib, seaborn, numpy" 2>/dev/null || {
        log_warning "Some Python packages are missing. Install with:"
        log_warning "pip3 install pandas matplotlib seaborn numpy"
        log_warning "Analysis will continue but plotting may fail."
    }
    
    log_success "Dependencies check completed"
}

# Compile the benchmark
compile_benchmark() {
    log_info "Compiling enhanced benchmark..."
    
    if [[ ! -f "$BENCHMARK_SOURCE" ]]; then
        log_error "Source file $BENCHMARK_SOURCE not found!"
        exit 1
    fi
    
    gcc -o "$BENCHMARK_BINARY" "$BENCHMARK_SOURCE" -lm -lpthread -O2 -Wall -Wextra
    
    if [[ $? -eq 0 ]]; then
        log_success "Benchmark compiled successfully"
    else
        log_error "Compilation failed"
        exit 1
    fi
}

# Set up kernel log monitoring
setup_kernel_logging() {
    log_info "Setting up kernel log monitoring..."
    
    # Create a marker in kernel logs
    echo "Starting benchmark: $LOG_MARKER" | sudo tee /dev/kmsg > /dev/null 2>&1 || {
        log_warning "Could not write to kernel log (requires sudo)"
        return 1
    }
    
    # Start kernel log monitoring in background
    dmesg -T -w > "$OUTPUT_DIR/kernel_log_live.txt" 2>&1 &
    KERNEL_LOG_PID=$!
    
    log_success "Kernel log monitoring started (PID: $KERNEL_LOG_PID)"
    return 0
}

# Stop kernel log monitoring
stop_kernel_logging() {
    if [[ -n "${KERNEL_LOG_PID:-}" ]]; then
        log_info "Stopping kernel log monitoring..."
        kill $KERNEL_LOG_PID 2>/dev/null || true
        wait $KERNEL_LOG_PID 2>/dev/null || true
        
        # Extract logs from our marker
        if [[ -f "$OUTPUT_DIR/kernel_log_live.txt" ]]; then
            grep -A 10000 "$LOG_MARKER" "$OUTPUT_DIR/kernel_log_live.txt" > "$OUTPUT_DIR/kernel_log_benchmark.txt" 2>/dev/null || true
        fi
        
        log_success "Kernel log monitoring stopped"
    fi
}

# Run the benchmark
run_benchmark() {
    log_info "Running enhanced benchmark..."
    
    if [[ ! -f "$BENCHMARK_BINARY" ]]; then
        log_error "Benchmark binary $BENCHMARK_BINARY not found! Compile first."
        exit 1
    fi
    
    # Create output directory
    mkdir -p "$OUTPUT_DIR"
    
    # Set up monitoring
    local monitor_kernel=false
    if [[ "${CAPTURE_KERNEL_LOGS:-}" == "true" ]]; then
        setup_kernel_logging && monitor_kernel=true
    fi
    
    # Run the benchmark
    log_info "Executing benchmark... This may take a while."
    
    # Capture system info
    {
        echo "=== SYSTEM_INFO ==="
        uname -a
        echo "CPU: $(lscpu | grep 'Model name' | cut -d: -f2 | xargs)"
        echo "Memory: $(free -h | grep '^Mem:' | awk '{print $2}')"
        echo "Timestamp: $(date)"
        echo "=== END_SYSTEM_INFO ==="
        echo ""
    } > "$OUTPUT_DIR/benchmark_output.txt"
    
    # Run benchmark and capture output
    if timeout 600 ./"$BENCHMARK_BINARY" >> "$OUTPUT_DIR/benchmark_output.txt" 2>&1; then
        log_success "Benchmark completed successfully"
        
        # Capture final kernel state
        if [[ "$monitor_kernel" == "true" ]]; then
            sleep 2  # Allow kernel logs to flush
            stop_kernel_logging
        fi
        
        # Also capture recent kernel logs even if live monitoring wasn't enabled
        dmesg -T | tail -1000 > "$OUTPUT_DIR/kernel_log_recent.txt" 2>/dev/null || true
        
        return 0
    else
        log_error "Benchmark execution failed or timed out"
        
        if [[ "$monitor_kernel" == "true" ]]; then
            stop_kernel_logging
        fi
        
        return 1
    fi
}

# Analyze results
analyze_results() {
    log_info "Analyzing benchmark results..."
    
    if [[ ! -f "$OUTPUT_DIR/benchmark_output.txt" ]]; then
        log_error "No benchmark output found in $OUTPUT_DIR"
        exit 1
    fi
    
    # Create analysis script if it doesn't exist
    if [[ ! -f "$ANALYSIS_SCRIPT" ]]; then
        log_warning "Analysis script not found. Creating basic analysis..."
        
        cat > "$ANALYSIS_SCRIPT" << 'EOF'
#!/usr/bin/env python3
import sys
import re
import json
import os
from datetime import datetime

def parse_benchmark_output(file_path):
    with open(file_path, 'r') as f:
        content = f.read()
    
    # Extract benchmark data
    benchmark_pattern = r'=== BENCHMARK_END: (\w+) SUCCESS:(\d+) ALLOC_NS:(\d+) INIT_NS:(\d+) ACCESS_NS:(\d+) FLUSH_NS:(\d+) FREE_NS:(\d+) ALLOC_CYCLES:(\d+) ACCESS_CYCLES:(\d+) FLUSH_CYCLES:(\d+) FREE_CYCLES:(\d+) MEMORY_MB:([\d.]+) THROUGHPUT_MB_S:([\d.]+) ==='
    
    matches = re.findall(benchmark_pattern, content)
    
    results = []
    for match in matches:
        result = {
            'test_name': match[0],
            'success': int(match[1]),
            'alloc_time_ns': int(match[2]),
            'total_time_ns': int(match[2]) + int(match[3]) + int(match[4]) + int(match[5]) + int(match[6]),
            'throughput_mb_s': float(match[12])
        }
        results.append(result)
    
    return results

def generate_summary(results):
    if not results:
        return "No benchmark results found"
    
    total_tests = len(results)
    successful_tests = sum(1 for r in results if r['success'])
    
    lustre_tests = [r for r in results if 'lustre' in r['test_name']]
    regular_tests = [r for r in results if 'regular' in r['test_name']]
    
    summary = f"""
Benchmark Analysis Summary
=========================
Total tests: {total_tests}
Successful tests: {successful_tests}
Success rate: {successful_tests/total_tests*100:.1f}%

Lustre tests: {len(lustre_tests)}
Regular tests: {len(regular_tests)}
"""
    
    if lustre_tests and regular_tests:
        avg_lustre_time = sum(r['total_time_ns'] for r in lustre_tests) / len(lustre_tests)
        avg_regular_time = sum(r['total_time_ns'] for r in regular_tests) / len(regular_tests)
        
        summary += f"""
Average Lustre total time: {avg_lustre_time:,.0f} ns
Average Regular total time: {avg_regular_time:,.0f} ns
Performance ratio (Lustre/Regular): {avg_lustre_time/avg_regular_time:.3f}
"""
    
    return summary

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 analyze_benchmark.py <benchmark_output.txt>")
        sys.exit(1)
    
    results = parse_benchmark_output(sys.argv[1])
    summary = generate_summary(results)
    
    print(summary)
    
    # Save results to JSON
    output_dir = os.path.dirname(sys.argv[1])
    with open(f"{output_dir}/analysis_results.json", "w") as f:
        json.dump(results, f, indent=2)
    
    print(f"\nDetailed results saved to {output_dir}/analysis_results.json")
EOF
        chmod +x "$ANALYSIS_SCRIPT"
    fi
    
    # Run analysis
    # python3 "$ANALYSIS_SCRIPT" --log-file "$OUTPUT_DIR/benchmark_output.txt" --output-dir "$OUTPUT_DIR"
    python3 "$ANALYSIS_SCRIPT" --log-file "$OUTPUT_DIR/benchmark_output.txt" --kernel-log "$OUTPUT_DIR/kernel_log_benchmark.txt" --output-dir "$OUTPUT_DIR"

    # Generate CSV summary
    log_info "Generating CSV summary..."
    
    cat > "$OUTPUT_DIR/parse_results.py" << 'EOF'
import re
import csv
import sys

def parse_to_csv(input_file, output_file):
    with open(input_file, 'r') as f:
        content = f.read()
    
    # Parse comparison data
    comparison_pattern = r'COMPARISON: SIZE:(\d+) PATTERN:(\d+) LUSTRE_ALLOC_NS:(\d+) REGULAR_ALLOC_NS:(\d+) RATIO:([\d.]+)'
    matches = re.findall(comparison_pattern, content)
    
    with open(output_file, 'w', newline='') as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow(['Size', 'Pattern', 'Lustre_Alloc_NS', 'Regular_Alloc_NS', 'Ratio'])
        
        for match in matches:
            writer.writerow([
                int(match[0]),  # size
                int(match[1]),  # pattern
                int(match[2]),  # lustre_alloc_ns
                int(match[3]),  # regular_alloc_ns
                float(match[4])  # ratio
            ])

if __name__ == "__main__":
    parse_to_csv(sys.argv[1], sys.argv[2])
EOF
    
    python3 "$OUTPUT_DIR/parse_results.py" "$OUTPUT_DIR/benchmark_output.txt" "$OUTPUT_DIR/comparison_results.csv"
    
    log_success "Analysis completed"
}

# Generate plots using gnuplot if available
generate_plots() {
    if command -v gnuplot &> /dev/null && [[ -f "$OUTPUT_DIR/comparison_results.csv" ]]; then
        log_info "Generating plots with gnuplot..."
        
        cat > "$OUTPUT_DIR/plot_results.gnuplot" << 'EOF'
set terminal png size 800,600
set output "performance_comparison.png"
set title "Lustre vs Regular Memory Allocation Performance"
set xlabel "Array Size"
set ylabel "Performance Ratio (Lustre/Regular)"
set logscale x
set grid
set key bottom right

plot "comparison_results.csv" using 1:5 with linespoints title "Performance Ratio"
EOF
        
        cd "$OUTPUT_DIR"
        gnuplot plot_results.gnuplot
        cd - > /dev/null
        
        log_success "Plot generated: $OUTPUT_DIR/performance_comparison.png"
    else
        log_warning "gnuplot not available or no data to plot"
    fi
}

# Main execution
main() {
    local compile_only=false
    local run_only=false
    local analyze_only=false
    local capture_kernel_logs=false
    local verbose=false
    local no_plots=false
    
    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                usage
                exit 0
                ;;
            -o|--output-dir)
                OUTPUT_DIR="$2"
                shift 2
                ;;
            -c|--compile-only)
                compile_only=true
                shift
                ;;
            -r|--run-only)
                run_only=true
                shift
                ;;
            -a|--analyze-only)
                analyze_only=true
                shift
                ;;
            -k|--kernel-logs)
                capture_kernel_logs=true
                shift
                ;;
            -v|--verbose)
                verbose=true
                shift
                ;;
            --no-plots)
                no_plots=true
                shift
                ;;
            -t|--threads)
                # This would be passed to the benchmark if it supported runtime config
                shift 2
                ;;
            --iterations|--min-size|--max-size)
                # Future enhancement: pass these to benchmark
                shift 2
                ;;
            *)
                log_error "Unknown option: $1"
                usage
                exit 1
                ;;
        esac
    done
    
    # Set global variables
    CAPTURE_KERNEL_LOGS="$capture_kernel_logs"
    
    # Set verbose mode
    if [[ "$verbose" == "true" ]]; then
        set -x
    fi
    
    # Ensure cleanup on exit
    trap 'stop_kernel_logging' EXIT
    
    log_info "Starting Lustre Memory Benchmark Suite"
    log_info "Output directory: $OUTPUT_DIR"
    
    # Check dependencies
    check_dependencies
    
    # Execute based on mode
    if [[ "$analyze_only" == "true" ]]; then
        analyze_results
        if [[ "$no_plots" != "true" ]]; then
            generate_plots
        fi
    elif [[ "$compile_only" == "true" ]]; then
        compile_benchmark
        log_success "Compilation completed"
    elif [[ "$run_only" == "true" ]]; then
        run_benchmark
        log_success "Benchmark execution completed"
    else
        # Full pipeline
        compile_benchmark
        run_benchmark
        analyze_results
        if [[ "$no_plots" != "true" ]]; then
            generate_plots
        fi
        
        log_success "Complete benchmark suite finished!"
        log_info "Results are available in: $OUTPUT_DIR"
        log_info "Key files:"
        log_info "  - benchmark_output.txt: Raw benchmark output"
        log_info "  - analysis_results.json: Parsed results"
        log_info "  - comparison_results.csv: Performance comparison data"
        
        if [[ -f "$OUTPUT_DIR/performance_comparison.png" ]]; then
            log_info "  - performance_comparison.png: Performance plot"
        fi
    fi
}

# Execute main function
main "$@"