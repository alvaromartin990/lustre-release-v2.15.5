#!/bin/bash
set -e

echo "Loading and running allocation benchmark..."
dmesg -C                     # clear kernel log buffer
insmod osd_alloc_bench.ko
sleep 1                      # give it time to print
RESULT=$(dmesg | tail -n1)
echo "$RESULT"

# Parse out the two numbers
IFS=' ' read -r _ _ avg_obd _ avg_k <<<"$RESULT"
echo "Average OBD_ALLOC_PTR() time: ${avg_obd} ns"
echo "Average kmalloc() time:      ${avg_k} ns"

