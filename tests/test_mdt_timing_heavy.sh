#!/bin/bash

TESTDIR="/mnt/lustre/mdt_large_test_$(date +%s)"
LOGFILE="/tmp/mdt_large_test_$(date +%s).log"

# Clear kernel log
sudo dmesg -c > /dev/null

echo "Starting large-scale MDT timing tests at $(date)" | tee -a $LOGFILE
echo "Test directory: $TESTDIR" | tee -a $LOGFILE
echo "-------------" | tee -a $LOGFILE

# Create test directory
mkdir -p $TESTDIR
echo "Created test directory"

# Test 1: Large directory with many files
echo "Test 1: Creating directory with 1000 files"
mkdir -p $TESTDIR/large_dir
for i in {1..1000}; do
  touch $TESTDIR/large_dir/file_$i
  if [ $((i % 100)) -eq 0 ]; then
    echo "Created $i files..."
  fi
done
sudo dmesg | grep "MDT_TIMING.*CREATE" | tail -20 | tee -a $LOGFILE

# Test 2: Creating and renaming large number of directories
echo "Test 2: Creating and renaming 100 directories"
for i in {1..100}; do
  mkdir $TESTDIR/dir_$i
  if [ $((i % 20)) -eq 0 ]; then
    echo "Created $i directories..."
  fi
done

echo "Renaming directories..."
for i in {1..100}; do
  mv $TESTDIR/dir_$i $TESTDIR/dir_${i}_renamed
  if [ $((i % 20)) -eq 0 ]; then
    echo "Renamed $i directories..."
  fi
done
sudo dmesg | grep "MDT_TIMING.*RENAME" | tail -20 | tee -a $LOGFILE

# Test 3: Creating large directory tree
echo "Test 3: Creating large directory tree (5x5x5)"
for i in {1..5}; do
  mkdir -p $TESTDIR/tree_$i
  for j in {1..5}; do
    mkdir -p $TESTDIR/tree_$i/branch_$j
    for k in {1..5}; do
      mkdir -p $TESTDIR/tree_$i/branch_$j/leaf_$k
      touch $TESTDIR/tree_$i/branch_$j/leaf_$k/file_{1..5}
    done
  done
  echo "Created tree $i/5..."
done
sudo dmesg | grep "MDT_TIMING" | tail -30 | tee -a $LOGFILE

echo "-------------" | tee -a $LOGFILE
echo "Tests complete! Summary of large-scale timing data:" | tee -a $LOGFILE

# Get all timing data
sudo dmesg | grep MDT_TIMING > /tmp/large_timings.txt
echo "Total operations: $(cat /tmp/large_timings.txt | wc -l)" | tee -a $LOGFILE

# Calculate statistics
echo "Overall statistics:" | tee -a $LOGFILE
avg=$(cat /tmp/large_timings.txt | awk '{sum+=$NF} END {print sum/NR}')
min=$(cat /tmp/large_timings.txt | awk '{print $NF}' | sort -n | head -1)
max=$(cat /tmp/large_timings.txt | awk '{print $NF}' | sort -n | tail -1)
echo "Average: ${avg}μs, Min: ${min}μs, Max: ${max}μs" | tee -a $LOGFILE

echo "Results saved to $LOGFILE"
