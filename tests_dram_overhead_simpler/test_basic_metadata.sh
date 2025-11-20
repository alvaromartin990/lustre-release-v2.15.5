#!/bin/bash
# Simple metadata operations test - measures basic file/directory operations

set -e

# Configuration
LUSTRE_MOUNT="/mnt/lustre"
if [ ! -d "$LUSTRE_MOUNT" ] || [ ! -w "$LUSTRE_MOUNT" ]; then
    LUSTRE_MOUNT="/tmp"
    echo "Warning: Using $LUSTRE_MOUNT for testing (Lustre not available)"
fi

TEST_DIR="$LUSTRE_MOUNT/simple_test_$$"
OUTPUT_FILE="basic_metadata_results.csv"
NUM_OPS=10

# Cleanup function
cleanup() {
    rm -rf "$TEST_DIR" 2>/dev/null || true
}
trap cleanup EXIT

echo "operation,iteration,latency_us" > "$OUTPUT_FILE"

mkdir -p "$TEST_DIR"
cd "$TEST_DIR"

echo "Running basic metadata operations test..."

# Test 1: File creation
for i in $(seq 1 $NUM_OPS); do
    start=$(date +%s%6N)
    touch "file_$i"
    end=$(date +%s%6N)
    latency=$((end - start))
    echo "file_create,$i,$latency" >> "../$OUTPUT_FILE"
done

# Test 2: Directory creation  
for i in $(seq 1 $NUM_OPS); do
    start=$(date +%s%6N)
    mkdir "dir_$i"
    end=$(date +%s%6N)
    latency=$((end - start))
    echo "dir_create,$i,$latency" >> "../$OUTPUT_FILE"
done

# Test 3: Stat operations
for i in $(seq 1 $NUM_OPS); do
    start=$(date +%s%6N)
    stat "file_$i" > /dev/null 2>&1
    end=$(date +%s%6N)
    latency=$((end - start))
    echo "file_stat,$i,$latency" >> "../$OUTPUT_FILE"
done

# Test 4: Extended attributes (if supported)
if command -v setfattr >/dev/null 2>&1; then
    for i in $(seq 1 5); do
        start=$(date +%s%6N)
        setfattr -n user.test -v "value$i" "file_$i" 2>/dev/null || true
        end=$(date +%s%6N)
        latency=$((end - start))
        echo "xattr_set,$i,$latency" >> "../$OUTPUT_FILE"
    done
fi

cd ..
echo "Test completed. Results saved to: $OUTPUT_FILE"
echo "Operations tested: file creation, directory creation, stat, extended attributes"