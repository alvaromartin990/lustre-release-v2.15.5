#!/bin/bash

# Ensure we are running with sufficient privileges for dmesg and lfs commands
if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root or with sudo." 
   exit 1
fi

TESTDIR="/mnt/lustre/flr_resync_test_$(date +%s)"
LOGFILE="/tmp/flr_resync_test_$(date +%s).log"
LUSTRE_FS_NAME=$(df /mnt/lustre | tail -1 | awk '{print $1}' | cut -d'@' -f1) # Attempt to get FS name

# Check if Lustre mount exists
if ! mount | grep -q /mnt/lustre; then
    echo "Error: /mnt/lustre does not seem to be mounted." | tee -a $LOGFILE
    exit 1
fi

# Check for at least 2 OSTs
OST_COUNT=$(lfs df -h | grep -c '\[OST:')
if [ "$OST_COUNT" -lt 2 ]; then
    echo "Error: Need at least 2 OSTs for File Level Replication. Found $OST_COUNT." | tee -a $LOGFILE
    exit 1
fi

echo "=== FLR and RESYNC Test ===" | tee -a $LOGFILE
echo "Started at $(date)" | tee -a $LOGFILE
echo "Lustre Filesystem: $LUSTRE_FS_NAME" | tee -a $LOGFILE
echo "Test directory: $TESTDIR" | tee -a $LOGFILE
echo "Log file: $LOGFILE" | tee -a $LOGFILE
echo "Testing: File Level Replication (FLR) and RESYNC operations" | tee -a $LOGFILE
echo "-------------" | tee -a $LOGFILE

# Function to show operation counts from dmesg
show_operation_counts() {
    local test_name="$1"
    sleep 1 # Wait a moment for dmesg to catch up

    local create_count=$(sudo dmesg | grep -i "MDT_TIMING.*Operation CREATE" | wc -l)
    local open_count=$(sudo dmesg | grep -i "MDT_TIMING.*Operation OPEN" | wc -l)
    local migrate_count=$(sudo dmesg | grep -i "MDT_TIMING.*Operation MIGRATE" | wc -l)
    local resync_count=$(sudo dmesg | grep -i "MDT_TIMING.*Operation RESYNC" | wc -l)
    local setattr_count=$(sudo dmesg | grep -i "MDT_TIMING.*Operation SETATTR" | wc -l)
    local mirror_create_count=$(sudo dmesg | grep -i "MDT_TIMING.*Operation MIRROR_CREATE" | wc -l)

    echo "$test_name:" | tee -a $LOGFILE
    echo "  CREATE operations: $create_count" | tee -a $LOGFILE
    echo "  OPEN operations: $open_count" | tee -a $LOGFILE
    echo "  MIGRATE operations: $migrate_count" | tee -a $LOGFILE
    echo "  MIRROR_CREATE operations: $mirror_create_count" | tee -a $LOGFILE
    echo "  RESYNC operations: $resync_count" | tee -a $LOGFILE
    echo "  SETATTR operations: $setattr_count" | tee -a $LOGFILE
    echo "---" | tee -a $LOGFILE
}

# Clear kernel log
echo "Clearing kernel log buffer..." | tee -a $LOGFILE
sudo dmesg -c > /dev/null
echo "Kernel log cleared." | tee -a $LOGFILE

# Create test directory
echo "Creating test directory: $TESTDIR" | tee -a $LOGFILE
mkdir -p "$TESTDIR"
if [ $? -ne 0 ]; then
    echo "Error: Could not create test directory $TESTDIR. Exiting." | tee -a $LOGFILE
    exit 1
fi

# Show initial filesystem status
echo "=== Initial Filesystem Status ===" | tee -a $LOGFILE
lfs df -h | tee -a $LOGFILE
lfs pool list 2>&1 | tee -a $LOGFILE # Added 2>&1 to capture errors if pools aren't supported
echo "-------------" | tee -a $LOGFILE

echo "=== TEST: FLR Creation and RESYNC Operation ===" | tee -a $LOGFILE
echo "Attempting FLR and RESYNC..." | tee -a $LOGFILE

MIRROR_FILE="$TESTDIR/mirror_test_flr.txt"
FLR_ENABLED=false # Flag to track if we managed to create mirrors

# 1. Create a plain file first
echo "Creating base file: $MIRROR_FILE" | tee -a $LOGFILE
echo "Initial content for mirroring." > "$MIRROR_FILE"
if [ $? -ne 0 ]; then
    echo "Error: Could not create base file. Exiting." | tee -a $LOGFILE
    rm -rf "$TESTDIR"
    exit 1
fi
echo "Base file created." | tee -a $LOGFILE
lfs getstripe "$MIRROR_FILE" | tee -a $LOGFILE

# 2. Attempt FLR using 'lfs mirror create'
echo "Attempting to create 2 replicas using 'lfs mirror create -N 2'..." | tee -a $LOGFILE
lfs mirror create -N 2 "$MIRROR_FILE" 2>&1 | tee -a $LOGFILE
MIRROR_CREATE_STATUS=$?

# 3. Verify if mirror creation was successful
lfs getstripe "$MIRROR_FILE" | tee -a $LOGFILE
if [ $MIRROR_CREATE_STATUS -eq 0 ] && lfs getstripe "$MIRROR_FILE" | grep -q -E "lcm_mirror_count: 2|Mirror_count=2"; then
    echo "✓ Mirrored file created successfully using 'lfs mirror create'." | tee -a $LOGFILE
    FLR_ENABLED=true
else
    echo "⚠ 'lfs mirror create' failed. This is likely due to the 'Inappropriate ioctl' error." | tee -a $LOGFILE
    echo "   >>> This strongly suggests FLR is NOT enabled or supported on your Lustre MDT/OSTs. <<<" | tee -a $LOGFILE
    echo "   Please verify your Lustre configuration and ensure FLR features are active on the servers." | tee -a $LOGFILE
    echo "   (Skipping setstripe attempt as it also failed previously)." | tee -a $LOGFILE
    FLR_ENABLED=false
fi

# 4. Continue only if FLR seems enabled
if [ "$FLR_ENABLED" = true ]; then
    echo "Writing more data to the mirrored file..." | tee -a $LOGFILE
    echo "This content should be on both mirrors." >> "$MIRROR_FILE"
    sync # Ensure data is flushed

    echo "Performing manual RESYNC operation..." | tee -a $LOGFILE
    lfs mirror resync "$MIRROR_FILE" 2>&1 | tee -a $LOGFILE
    RESYNC_STATUS=$?

    if [ $RESYNC_STATUS -eq 0 ]; then
        echo "✓ RESYNC command completed." | tee -a $LOGFILE
    else
        echo "⚠ RESYNC command failed with status $RESYNC_STATUS." | tee -a $LOGFILE
    fi

    echo "Verifying mirror status..." | tee -a $LOGFILE
    lfs mirror verify "$MIRROR_FILE" 2>&1 | tee -a $LOGFILE
    VERIFY_STATUS=$?
    if [ $VERIFY_STATUS -eq 0 ]; then
         echo "✓ Mirror verification successful." | tee -a $LOGFILE
    else
         echo "⚠ Mirror verification reported issues." | tee -a $LOGFILE
    fi
else
    echo "Skipping RESYNC/VERIFY steps due to FLR creation failure." | tee -a $LOGFILE
fi

# Show operation counts
show_operation_counts "After FLR/RESYNC attempts"

echo "=== TIMING ANALYSIS ===" | tee -a $LOGFILE
echo "Recent MDT_TIMING entries:" | tee -a $LOGFILE
sudo dmesg | grep "MDT_TIMING" | tail -40 | tee -a $LOGFILE

echo "-------------" | tee -a $LOGFILE
echo "=== FINAL SUMMARY ===" | tee -a $LOGFILE
echo "Please refer to 'After FLR/RESYNC attempts' for counts." | tee -a $LOGFILE
echo "-------------" | tee -a $LOGFILE
echo "PERFORMANCE ANALYSIS (Based on recent entries):" | tee -a $LOGFILE

DMESG_OUTPUT=$(sudo dmesg | grep "MDT_TIMING")
FINAL_RESYNC_COUNT=$(echo "$DMESG_OUTPUT" | grep -i "Operation RESYNC" | wc -l)

if [ "$FINAL_RESYNC_COUNT" -gt 0 ]; then
    avg_resync_time=$(echo "$DMESG_OUTPUT" | grep -i "Operation RESYNC" | sed 's/.*took \([0-9]*\) microseconds.*/\1/' | awk '{sum+=$1} END {if(NR>0) print sum/NR; else print 0}')
    echo "  Average RESYNC operation time: ${avg_resync_time} microseconds" | tee -a $LOGFILE
else
    echo "  No RESYNC operations recorded in dmesg." | tee -a $LOGFILE
fi

echo "-------------" | tee -a $LOGFILE
echo "Directory contents:" | tee -a $LOGFILE
ls -la "$TESTDIR"/ | tee -a $LOGFILE
echo "Stripe info for test file (if exists):" | tee -a $LOGFILE
lfs getstripe "$MIRROR_FILE" 2>/dev/null | tee -a $LOGFILE

echo "-------------" | tee -a $LOGFILE
echo "Test completed!" | tee -a $LOGFILE
echo "Results saved to: $LOGFILE"

# Optional: Clean up 
# echo "Cleaning up test directory $TESTDIR..."
# rm -rf "$TESTDIR"
# echo "Cleanup complete."
