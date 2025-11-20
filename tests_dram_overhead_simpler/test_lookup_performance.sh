#!/bin/bash
# Simple lookup performance test - measures file/directory lookup latency

set -e

# Configuration
LUSTRE_MOUNT="/mnt/lustre"
if [ ! -d "$LUSTRE_MOUNT" ] || [ ! -w "$LUSTRE_MOUNT" ]; then
    LUSTRE_MOUNT="/tmp"
    echo "Warning: Using $LUSTRE_MOUNT for testing (Lustre not available)"
fi

TEST_DIR="$LUSTRE_MOUNT/lookup_test_$$"
OUTPUT_FILE="lookup_performance_results.csv"
NUM_FILES=10

# Cleanup function
cleanup() {
    rm -rf "$TEST_DIR" 2>/dev/null || true
}
trap cleanup EXIT

echo "operation,iteration,latency_us" > "$OUTPUT_FILE"

mkdir -p "$TEST_DIR"
cd "$TEST_DIR"

echo "Running lookup performance test..."

# Setup: Create test files
for i in $(seq 1 $NUM_FILES); do
    touch "testfile_$i"
    mkdir "testdir_$i"
    echo "content $i" > "testfile_$i"
done

# Test 1: File existence checks
for i in $(seq 1 $NUM_FILES); do
    start=$(date +%s%6N)
    test -f "testfile_$i"
    end=$(date +%s%6N)
    latency=$((end - start))
    echo "file_lookup,$i,$latency" >> "../$OUTPUT_FILE"
done

# Test 2: Directory existence checks
for i in $(seq 1 $NUM_FILES); do
    start=$(date +%s%6N)
    test -d "testdir_$i"
    end=$(date +%s%6N)
    latency=$((end - start))
    echo "dir_lookup,$i,$latency" >> "../$OUTPUT_FILE"
done

# Test 3: File access (read first line)
for i in $(seq 1 $NUM_FILES); do
    start=$(date +%s%6N)
    head -n1 "testfile_$i" > /dev/null
    end=$(date +%s%6N)
    latency=$((end - start))
    echo "file_access,$i,$latency" >> "../$OUTPUT_FILE"
done

# Test 4: Directory listing
for i in $(seq 1 5); do
    start=$(date +%s%6N)
    ls testdir_$i > /dev/null
    end=$(date +%s%6N)
    latency=$((end - start))
    echo "dir_list,$i,$latency" >> "../$OUTPUT_FILE"
done

cd ..
echo "Test completed. Results saved to: $OUTPUT_FILE"
echo "Operations tested: file lookup, directory lookup, file access, directory listing"