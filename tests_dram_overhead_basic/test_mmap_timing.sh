#!/bin/bash
# Basic Lustre metadata operations test with kernel log extraction
# Extracts timing data from mmap-instrumented code

set -e

# Configuration
LUSTRE_MOUNT="/mnt/lustre"
if [ ! -d "$LUSTRE_MOUNT" ] || [ ! -w "$LUSTRE_MOUNT" ]; then
    LUSTRE_MOUNT="/tmp"
    echo "Warning: Using $LUSTRE_MOUNT for testing (Lustre not available)"
fi

TEST_DIR="$LUSTRE_MOUNT/basic_dram_test_$$"
OUTPUT_FILE="mmap_timing_results.csv"

# Cleanup function
cleanup() {
    rm -rf "$TEST_DIR" 2>/dev/null || true
}
trap cleanup EXIT

echo "=== Lustre DRAM Overhead Basic Test ==="
echo "Test directory: $TEST_DIR"
echo "Extracting timing from kernel logs..."

# Clear dmesg buffer to get clean timing data
sudo dmesg -C 2>/dev/null || true

# Create CSV header
echo "timestamp,operation,component,allocation_type,size_bytes,duration_ns" > "$OUTPUT_FILE"

echo "Starting metadata operations..."

# Create test directory and perform basic operations
mkdir -p "$TEST_DIR"
cd "$TEST_DIR"

echo "  1. Creating files..."
# Create several files to trigger FID allocations
for i in {1..5}; do
    touch "test_file_$i"
    echo "content $i" > "test_file_$i"
done

echo "  2. Creating directories..."
# Create directories to trigger more FID allocations
for i in {1..3}; do
    mkdir "test_dir_$i"
done

echo "  3. Accessing files (stat operations)..."
# Access files to trigger FLD cache operations
for i in {1..5}; do
    stat "test_file_$i" > /dev/null 2>&1 || true
    ls -l "test_file_$i" > /dev/null 2>&1 || true
done

echo "  4. Setting attributes..."
# Set extended attributes if supported
if command -v setfattr >/dev/null 2>&1; then
    for i in {1..3}; do
        setfattr -n user.test -v "value$i" "test_file_$i" 2>/dev/null || true
        getfattr -n user.test "test_file_$i" 2>/dev/null || true
    done
fi

echo "  5. Additional file operations..."
# More file operations
for i in {1..3}; do
    cp "test_file_$i" "copy_file_$i" 2>/dev/null || true
    chmod 644 "test_file_$i" 2>/dev/null || true
done

# Wait a moment for any delayed kernel logging
sleep 2

cd ..
echo "Metadata operations completed."

# Extract timing data from kernel logs
echo "Extracting timing data from dmesg..."

# Get dmesg output and filter for our timing markers
DMESG_OUTPUT=$(sudo dmesg 2>/dev/null || dmesg 2>/dev/null || echo "")

if [ -z "$DMESG_OUTPUT" ]; then
    echo "Warning: Could not read kernel logs"
    echo "# No timing data available - check dmesg permissions" >> "$OUTPUT_FILE"
    exit 0
fi

# Process DRAM_TIMING_START and DRAM_TIMING_END pairs
echo "$DMESG_OUTPUT" | grep "DRAM_TIMING" | while read -r line; do
    timestamp=$(date +%s%N)
    
    if echo "$line" | grep -q "DRAM_TIMING_START"; then
        # Extract start timing info
        component=$(echo "$line" | sed -n 's/.*DRAM_TIMING_START: \([^ ]*\) .*/\1/p')
        operation=$(echo "$line" | sed -n 's/.*DRAM_TIMING_START: [^ ]* \([^ ]*\) .*/\1/p')
        size_info=$(echo "$line" | sed -n 's/.*size=\([0-9]*\) .*/\1/p')
        time_info=$(echo "$line" | sed -n 's/.*time=\([0-9]*\)$/\1/p')
        
        # Store start info for matching with end
        echo "START:$component:$operation:$size_info:$time_info" > "/tmp/timing_start_$$"
        
    elif echo "$line" | grep -q "DRAM_TIMING_END"; then
        # Extract end timing info
        duration=$(echo "$line" | sed -n 's/.*duration=\([0-9]*\) .*/\1/p')
        end_time=$(echo "$line" | sed -n 's/.*time=\([0-9]*\)$/\1/p')
        
        # Read matching start info
        if [ -f "/tmp/timing_start_$$" ]; then
            start_info=$(cat "/tmp/timing_start_$$")
            component=$(echo "$start_info" | cut -d: -f2)
            operation=$(echo "$start_info" | cut -d: -f3)
            size_bytes=$(echo "$start_info" | cut -d: -f4)
            
            # Write to CSV
            echo "$timestamp,$operation,$component,mmap,$size_bytes,$duration" >> "$OUTPUT_FILE"
            rm -f "/tmp/timing_start_$$"
        fi
    fi
done

# Clean up any remaining temp files
rm -f "/tmp/timing_start_$$"

# Count the results
result_count=$(tail -n +2 "$OUTPUT_FILE" 2>/dev/null | wc -l | tr -d ' ')

echo ""
echo "=== Test Results ==="
echo "Timing entries captured: $result_count"
echo "Results saved to: $OUTPUT_FILE"

if [ "$result_count" -gt 0 ]; then
    echo ""
    echo "Sample timing data:"
    head -n 6 "$OUTPUT_FILE" | column -t -s','
    echo ""
    echo "Summary statistics:"
    if [ "$result_count" -gt 1 ]; then
        # Calculate basic stats from duration column
        tail -n +2 "$OUTPUT_FILE" | cut -d',' -f6 | awk '
        {
            sum += $1; 
            if (NR == 1) {
                min = max = $1
            } else {
                if ($1 < min) min = $1
                if ($1 > max) max = $1
            }
        } 
        END {
            if (NR > 0) {
                avg = sum / NR
                printf "  Min duration: %.0f ns\n", min
                printf "  Max duration: %.0f ns\n", max  
                printf "  Avg duration: %.0f ns\n", avg
                printf "  Operations: %d\n", NR
            }
        }'
    fi
else
    echo "No timing data captured. This may be normal if:"
    echo "  - Lustre is not using the modified mmap-backed allocation code"
    echo "  - Kernel logging level filters out KERN_ALERT messages"  
    echo "  - Operations did not trigger the instrumented code paths"
fi

echo "Test completed."