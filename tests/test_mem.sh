#!/bin/bash

# Lustre FILE CREATE/DELETE Performance Testing Script
# This script creates and deletes files and captures kernel timing logs for REINT_OPEN operations
# Enhanced version that also captures Stage 1-5 timing logs and memory allocation tracking

# Configuration variables
LUSTRE_MOUNT_POINT="/mnt/lustre"  # Change this to your Lustre mount point
TEST_FILE_PREFIX="test_create_file"
NUM_FILES=1
LOG_FILE="lustre_open_timing.log"
KERNEL_LOG_MARK="LUSTRE_OPEN_TEST_START_$(date +%s)"
SHOW_COMPLETE_LOGS=true

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to check if running as root (needed for some kernel log access)
check_root() {
    if [[ $EUID -ne 0 ]]; then
        print_warning "Not running as root. Some kernel log access might be limited."
        print_info "Consider running with 'sudo' for complete log access."
    fi
}

# Function to check if Lustre mount point exists
check_lustre_mount() {
    if [[ ! -d "$LUSTRE_MOUNT_POINT" ]]; then
        print_error "Lustre mount point $LUSTRE_MOUNT_POINT does not exist!"
        exit 1
    fi
    
    # Check if it's actually a Lustre filesystem
    if ! df -t lustre "$LUSTRE_MOUNT_POINT" >/dev/null 2>&1; then
        print_warning "$LUSTRE_MOUNT_POINT might not be a Lustre filesystem"
        print_info "Continuing anyway..."
    else
        print_success "Lustre mount point $LUSTRE_MOUNT_POINT verified"
    fi
}

# Function to create a marker in kernel logs
create_log_marker() {
    echo "$KERNEL_LOG_MARK" | sudo tee /dev/kmsg >/dev/null 2>&1
    print_info "Created kernel log marker: $KERNEL_LOG_MARK"
}

# Function to cleanup test files
cleanup_test_files() {
    print_info "Cleaning up test files..."
    for i in $(seq 1 $NUM_FILES); do
        test_file="$LUSTRE_MOUNT_POINT/${TEST_FILE_PREFIX}_$i.txt"
        if [[ -f "$test_file" ]]; then
            rm -f "$test_file" 2>/dev/null
        fi
    done
    print_success "Cleanup completed"
}

# Function to capture kernel logs
capture_kernel_logs() {
    local output_file="$1"
    print_info "Capturing kernel logs to $output_file..."
    
    # Try multiple methods to get kernel logs
    {
        echo "=== KERNEL LOGS CAPTURED AT $(date) ==="
        echo "=== Looking for MDT_TIMING, MDD_TIMING, OSD_TIMING, REINT_OPEN, Stage messages, and Memory Allocations ==="
        echo ""
        
        # Method 1: dmesg (most common)
        if command -v dmesg >/dev/null 2>&1; then
            echo "=== DMESG OUTPUT ==="
            dmesg | grep -E "(MDT_TIMING|MDD_TIMING|OSD_TIMING|REINT_OPEN|Stage [1-5]|Stage|OBD_ALLOC_PTR|OBD_ALLOC_PTR_ARRAY_LARGE|OBD_ALLOC_PTR_ARRAY|OBD_ALLOC|OBD_SLAB_ALLOC_PTR|$KERNEL_LOG_MARK)" | tail -300
            echo ""
        fi
        
        # Method 2: journalctl (systemd systems)
        if command -v journalctl >/dev/null 2>&1; then
            echo "=== JOURNALCTL OUTPUT ==="
            journalctl --dmesg --no-pager | grep -E "(MDT_TIMING|MDD_TIMING|OSD_TIMING|REINT_OPEN|Stage [1-5]|Stage|OBD_ALLOC_PTR|OBD_ALLOC_PTR_ARRAY_LARGE|OBD_ALLOC_PTR_ARRAY|OBD_ALLOC|OBD_SLAB_ALLOC_PTR|$KERNEL_LOG_MARK)" | tail -300
            echo ""
        fi
        
        # Method 3: /var/log/messages or /var/log/kern.log
        for log_file in /var/log/messages /var/log/kern.log; do
            if [[ -r "$log_file" ]]; then
                echo "=== $log_file OUTPUT ==="
                grep -E "(MDT_TIMING|MDD_TIMING|OSD_TIMING|REINT_OPEN|Stage [1-5]|Stage|OBD_ALLOC_PTR|OBD_ALLOC_PTR_ARRAY_LARGE|OBD_ALLOC_PTR_ARRAY|OBD_ALLOC|OBD_SLAB_ALLOC_PTR|$KERNEL_LOG_MARK)" "$log_file" | tail -200
                echo ""
            fi
        done
        
    } > "$output_file"
    
    print_success "Kernel logs captured to $output_file"
}

# Function to perform the OPEN tests with file deletion
perform_open_tests() {
    print_info "Starting OPEN file tests (create and delete)..."
    print_info "Creating and deleting $NUM_FILES files in $LUSTRE_MOUNT_POINT"
    
    # Record start time
    local start_time=$(date +%s.%N)
    
    # Create and delete files one by one
    for i in $(seq 1 $NUM_FILES); do
        test_file="$LUSTRE_MOUNT_POINT/${TEST_FILE_PREFIX}_$i.txt"
        
        print_info "Processing file $i/$NUM_FILES: $(basename $test_file)"
        
        # === FILE CREATION ===
        print_info "  → Creating file $(basename $test_file)"
        local create_start=$(date +%s.%N)
        
        # Create file using touch (triggers REINT_OPEN)
        if touch "$test_file" 2>/dev/null; then
            local create_end=$(date +%s.%N)
            local create_duration=$(echo "$create_end - $create_start" | bc -l)
            printf "    ✓ Created in %.3f seconds\n" $create_duration
            
            # Add a small delay to ensure logs are separated
            sleep 0.2
        else
            print_error "Failed to create $test_file"
            continue
        fi
        
        # === FILE DELETION ===
        print_info "  → Deleting file $(basename $test_file)"
        local delete_start=$(date +%s.%N)
        
        # Delete file (triggers REINT_UNLINK)
        if rm -f "$test_file" 2>/dev/null; then
            local delete_end=$(date +%s.%N)
            local delete_duration=$(echo "$delete_end - $delete_start" | bc -l)
            printf "    ✓ Deleted in %.3f seconds\n" $delete_duration
            
            # Add a small delay to ensure logs are separated
            sleep 0.2
        else
            print_error "Failed to delete $test_file"
        fi
        
        # Calculate total time for this file
        local file_total=$(echo "$delete_end - $create_start" | bc -l)
        printf "  → Total time for file $i: %.3f seconds\n" $file_total
        echo ""
    done
    
    # Record end time
    local end_time=$(date +%s.%N)
    local total_duration=$(echo "$end_time - $start_time" | bc -l)
    
    print_success "All files processed (created and deleted)"
    printf "${GREEN}Total time: %.3f seconds${NC}\n" $total_duration
    printf "${GREEN}Average time per file (create+delete): %.3f seconds${NC}\n" $(echo "$total_duration / $NUM_FILES" | bc -l)
}

# Function to capture complete kernel logs from test start
capture_complete_kernel_logs() {
    local output_file="$1"
    print_info "Capturing complete kernel logs from test start to $output_file..."
    
    {
        echo "=== COMPLETE KERNEL LOGS FROM TEST START ==="
        echo "=== Captured at $(date) ==="
        echo "=== All kernel messages since marker: $KERNEL_LOG_MARK ==="
        echo ""
        
        # Method 1: dmesg (most common) - get all messages since our marker
        if command -v dmesg >/dev/null 2>&1; then
            echo "=== COMPLETE DMESG OUTPUT ==="
            # Get all dmesg output and find our marker, then print everything after it
            dmesg | awk -v marker="$KERNEL_LOG_MARK" '
            BEGIN { found = 0 }
            {
                if (found || index($0, marker)) {
                    found = 1
                    print $0
                }
            }'
            echo ""
        fi
        
        # Method 2: journalctl (systemd systems) - since our marker timestamp
        if command -v journalctl >/dev/null 2>&1; then
            echo "=== COMPLETE JOURNALCTL OUTPUT ==="
            # Extract timestamp from our marker and get logs since then
            local marker_timestamp=$(echo "$KERNEL_LOG_MARK" | grep -o '[0-9]\+')
            if [[ -n "$marker_timestamp" ]]; then
                local marker_date=$(date -d "@$marker_timestamp" "+%Y-%m-%d %H:%M:%S" 2>/dev/null)
                if [[ -n "$marker_date" ]]; then
                    journalctl --dmesg --no-pager --since "$marker_date" 2>/dev/null
                else
                    # Fallback: get recent logs
                    journalctl --dmesg --no-pager --since "5 minutes ago" 2>/dev/null
                fi
            else
                # Fallback: get recent logs
                journalctl --dmesg --no-pager --since "5 minutes ago" 2>/dev/null
            fi
            echo ""
        fi
        
        # Method 3: /var/log/messages or /var/log/kern.log - recent entries
        for log_file in /var/log/messages /var/log/kern.log; do
            if [[ -r "$log_file" ]]; then
                echo "=== COMPLETE $log_file OUTPUT (last 300 lines) ==="
                tail -300 "$log_file" | awk -v marker="$KERNEL_LOG_MARK" '
                BEGIN { found = 0 }
                {
                    if (found || index($0, marker)) {
                        found = 1
                        print $0
                    }
                }'
                echo ""
            fi
        done
        
    } > "$output_file"
    
    print_success "Complete kernel logs captured to $output_file"
}

# Function to analyze the captured logs
analyze_logs() {
    local log_file="$1"
    
    if [[ ! -f "$log_file" ]]; then
        print_error "Log file $log_file not found!"
        return 1
    fi
    
    print_info "Analyzing timing and memory allocation data from $log_file..."
    echo ""
    
    # Extract and summarize timing information
    echo "=== TIMING ANALYSIS ==="
    
    # Stage-by-stage analysis
    echo "Stage Analysis (File Creation/Deletion Process):"
    echo "Note: Stages may execute out of logical order due to Lustre's internal flow"
    echo ""
    
    for stage in 1 2 3 4 5; do
        local stage_count=$(grep -c "Stage $stage" "$log_file" 2>/dev/null)
        [[ -z "$stage_count" || ! "$stage_count" =~ ^[0-9]+$ ]] && stage_count=0
        echo "  Stage $stage messages ($stage_count occurrences):"
        if [[ $stage_count -gt 0 ]]; then
            grep "Stage $stage" "$log_file" 2>/dev/null | head -5
        else
            echo "    [No Stage $stage messages found]"
        fi
        echo ""
    done
    
    # Show actual execution order per file operation
    echo "Execution Order Analysis (per file operation):"
    echo "Showing order of stage execution for each file:"
    local file_num=1
    grep -E "(MDT_DEBUG.*(OPEN|UNLINK)|Stage [1-5])" "$log_file" 2>/dev/null | while read -r line; do
        if echo "$line" | grep -q "MDT_DEBUG.*OPEN"; then
            echo "  File $file_num creation sequence:"
        elif echo "$line" | grep -q "MDT_DEBUG.*UNLINK"; then
            echo "  File $file_num deletion sequence:"
            file_num=$((file_num + 1))
        elif echo "$line" | grep -q "Stage"; then
            local stage=$(echo "$line" | grep -o "Stage [0-9]")
            echo "    → $stage"
        fi
    done
    
    # MDT layer timings (REINT_OPEN and REINT_UNLINK operations)
    echo "MDT Layer Timings (REINT operations):"
    echo "  REINT_OPEN (file creation):"
    grep -E "(MDT_TIMING.*OPEN|mdt_reint_rec.*OPEN)" "$log_file" 2>/dev/null | head -10
    echo "  REINT_UNLINK (file deletion):"
    grep -E "(MDT_TIMING.*UNLINK|mdt_reint_rec.*UNLINK)" "$log_file" 2>/dev/null | head -10
    echo ""
    
    # MDD layer timings
    echo "MDD Layer Timings:"
    grep "MDD_TIMING" "$log_file" 2>/dev/null | head -20
    echo ""
    
    # OSD layer timings
    echo "OSD Layer Timings:"
    grep "OSD_TIMING" "$log_file" 2>/dev/null | head -20
    echo ""
    
    # === NEW: MEMORY ALLOCATION ANALYSIS ===
    echo "=== MEMORY ALLOCATION ANALYSIS ==="
    
    echo "Memory Allocation Operations Detected:"
    
    # Count each type of allocation with better error handling
    local obd_alloc_ptr_count=$(grep -c "OBD_ALLOC_PTR[^_]" "$log_file" 2>/dev/null)
    local obd_alloc_ptr_array_large_count=$(grep -c "OBD_ALLOC_PTR_ARRAY_LARGE" "$log_file" 2>/dev/null)
    local obd_alloc_ptr_array_count=$(grep -c "OBD_ALLOC_PTR_ARRAY[^_]" "$log_file" 2>/dev/null)
    local obd_alloc_count=$(grep -c "OBD_ALLOC[^_]" "$log_file" 2>/dev/null)
    local obd_slab_alloc_ptr_count=$(grep -c "OBD_SLAB_ALLOC_PTR" "$log_file" 2>/dev/null)
    
    # Ensure all variables are numeric
    [[ -z "$obd_alloc_ptr_count" || ! "$obd_alloc_ptr_count" =~ ^[0-9]+$ ]] && obd_alloc_ptr_count=0
    [[ -z "$obd_alloc_ptr_array_large_count" || ! "$obd_alloc_ptr_array_large_count" =~ ^[0-9]+$ ]] && obd_alloc_ptr_array_large_count=0
    [[ -z "$obd_alloc_ptr_array_count" || ! "$obd_alloc_ptr_array_count" =~ ^[0-9]+$ ]] && obd_alloc_ptr_array_count=0
    [[ -z "$obd_alloc_count" || ! "$obd_alloc_count" =~ ^[0-9]+$ ]] && obd_alloc_count=0
    [[ -z "$obd_slab_alloc_ptr_count" || ! "$obd_slab_alloc_ptr_count" =~ ^[0-9]+$ ]] && obd_slab_alloc_ptr_count=0
    
    printf "  %-30s: %d occurrences\n" "OBD_ALLOC_PTR" "$obd_alloc_ptr_count"
    printf "  %-30s: %d occurrences\n" "OBD_ALLOC_PTR_ARRAY_LARGE" "$obd_alloc_ptr_array_large_count"
    printf "  %-30s: %d occurrences\n" "OBD_ALLOC_PTR_ARRAY" "$obd_alloc_ptr_array_count"
    printf "  %-30s: %d occurrences\n" "OBD_ALLOC" "$obd_alloc_count"
    printf "  %-30s: %d occurrences\n" "OBD_SLAB_ALLOC_PTR" "$obd_slab_alloc_ptr_count"
    echo ""
    
    local total_allocations=$((obd_alloc_ptr_count + obd_alloc_ptr_array_large_count + obd_alloc_ptr_array_count + obd_alloc_count + obd_slab_alloc_ptr_count))
    echo "  Total Memory Allocations: $total_allocations"
    echo ""
    
    # Show detailed allocation messages
    echo "Detailed Memory Allocation Messages:"
    for alloc_type in "OBD_ALLOC_PTR[^_]" "OBD_ALLOC_PTR_ARRAY_LARGE" "OBD_ALLOC_PTR_ARRAY[^_]" "OBD_ALLOC[^_]" "OBD_SLAB_ALLOC_PTR"; do
        local clean_type=$(echo "$alloc_type" | sed 's/\[.*\]//')
        local count=$(grep -c "$alloc_type" "$log_file" 2>/dev/null)
        [[ -z "$count" || ! "$count" =~ ^[0-9]+$ ]] && count=0
        if [[ $count -gt 0 ]]; then
            echo "  $clean_type allocations ($count found):"
            grep -E "$alloc_type" "$log_file" 2>/dev/null | head -5 | sed 's/^/    /'
            echo ""
        fi
    done
    
    # Summary statistics
    echo "=== SUMMARY STATISTICS ==="
    
    # Stage statistics
    echo "Stage Occurrence Count:"
    for stage in 1 2 3 4 5; do
        local count=$(grep -c "Stage $stage" "$log_file" 2>/dev/null)
        [[ -z "$count" || ! "$count" =~ ^[0-9]+$ ]] && count=0
        printf "  Stage %d: %d occurrences\n" $stage $count
    done
    echo ""
    
    # Memory allocation per file operation
    echo "Memory Allocation Statistics:"
    local files_processed=$(grep -c "MDT_DEBUG.*OPEN" "$log_file" 2>/dev/null)
    [[ -z "$files_processed" || ! "$files_processed" =~ ^[0-9]+$ ]] && files_processed=1
    if [[ $files_processed -eq 0 ]]; then
        files_processed=1
    fi
    printf "  Files processed: %d\n" "$files_processed"
    if [[ $total_allocations -gt 0 && $files_processed -gt 0 ]]; then
        if command -v bc >/dev/null 2>&1; then
            local avg_alloc=$(echo "scale=1; $total_allocations / $files_processed" | bc -l 2>/dev/null)
            [[ -n "$avg_alloc" ]] && printf "  Average allocations per file: %s\n" "$avg_alloc"
        else
            local avg_alloc=$((total_allocations / files_processed))
            printf "  Average allocations per file: %d\n" "$avg_alloc"
        fi
    fi
    echo ""
    
    # Extract microsecond values and calculate statistics
    if command -v awk >/dev/null 2>&1; then
        echo "MDT reint_rec OPEN timings (microseconds):"
        local timing_data=$(grep "mdt_reint_rec OPEN took" "$log_file" 2>/dev/null | awk '{print $(NF-1)}' | sort -n)
        if [[ -n "$timing_data" ]]; then
            echo "$timing_data" | awk '
            {
                sum += $1
                values[NR] = $1
            }
            END {
                if (NR > 0) {
                    avg = sum / NR
                    printf "  Count: %d\n", NR
                    printf "  Average: %.2f µs\n", avg
                    printf "  Min: %.2f µs\n", values[1]
                    printf "  Max: %.2f µs\n", values[NR]
                } else {
                    print "  No timing data found"
                }
            }'
        else
            echo "  No timing data found"
        fi
        echo ""
        
        echo "MDT reint_rec UNLINK timings (microseconds):"
        local timing_data=$(grep "mdt_reint_rec UNLINK took" "$log_file" 2>/dev/null | awk '{print $(NF-1)}' | sort -n)
        if [[ -n "$timing_data" ]]; then
            echo "$timing_data" | awk '
            {
                sum += $1
                values[NR] = $1
            }
            END {
                if (NR > 0) {
                    avg = sum / NR
                    printf "  Count: %d\n", NR
                    printf "  Average: %.2f µs\n", avg
                    printf "  Min: %.2f µs\n", values[1]
                    printf "  Max: %.2f µs\n", values[NR]
                } else {
                    print "  No timing data found"
                }
            }'
        else
            echo "  No timing data found"
        fi
        echo ""
        
        echo "OSD file creation timings (microseconds):"
        local timing_data=$(grep -E "(osd.*create.*took|__osd_object_create.*took)" "$log_file" 2>/dev/null | awk '{print $(NF-4)}' | sort -n)
        if [[ -n "$timing_data" ]]; then
            echo "$timing_data" | awk '
            {
                sum += $1
                values[NR] = $1
            }
            END {
                if (NR > 0) {
                    avg = sum / NR
                    printf "  Count: %d\n", NR
                    printf "  Average: %.2f µs\n", avg
                    printf "  Min: %.2f µs\n", values[1]
                    printf "  Max: %.2f µs\n", values[NR]
                } else {
                    print "  No timing data found"
                }
            }'
        else
            echo "  No timing data found"
        fi
        echo ""
        
        echo "Overall REINT operation timings (microseconds):"
        local timing_data=$(grep -E "REINT_(OPEN|UNLINK).*took" "$log_file" 2>/dev/null | awk '{print $(NF-1)}' | sort -n)
        if [[ -n "$timing_data" ]]; then
            echo "$timing_data" | awk '
            {
                sum += $1
                values[NR] = $1
            }
            END {
                if (NR > 0) {
                    avg = sum / NR
                    printf "  Count: %d\n", NR
                    printf "  Average: %.2f µs\n", avg
                    printf "  Min: %.2f µs\n", values[1]
                    printf "  Max: %.2f µs\n", values[NR]
                } else {
                    print "  No timing data found"
                }
            }'
        else
            echo "  No timing data found"
        fi
        echo ""
        
        # Extract timing information from Stage messages if they contain timing data
        echo "Stage Timing Analysis:"
        echo "Current stage execution order (from logs):"
        
        local total_files=$(grep -c "MDT_DEBUG.*OPEN" "$log_file" 2>/dev/null)
        [[ -z "$total_files" || ! "$total_files" =~ ^[0-9]+$ ]] && total_files=0
        echo "  Total files created: $total_files"
        echo ""
        
        for stage in 1 2 3 4 5; do
            local stage_count=$(grep -c "Stage $stage" "$log_file" 2>/dev/null)
            [[ -z "$stage_count" || ! "$stage_count" =~ ^[0-9]+$ ]] && stage_count=0
            if [[ $stage_count -gt 0 ]]; then
                echo "  Stage $stage ($stage_count occurrences):"
                case $stage in
                    1) echo "    Function: mdt_reint_internal (MDT layer)" ;;
                    2) echo "    Function: osd_ea_fid_set (FID allocation)" ;;
                    3) echo "    Function: __osd_create (Inode creation)" ;;
                    4) echo "    Function: [Missing] LMA xattr setting" ;;
                    5) echo "    Function: [Missing] OI mapping insert" ;;
                esac
                grep "Stage $stage" "$log_file" 2>/dev/null | head -3 | sed 's/^/    /'
                echo ""
            else
                echo "  Stage $stage: [Missing - check placement in source code]"
                case $stage in
                    4) echo "    Expected location: osd_ea_fid_set() or __osd_xattr_set() in osd_handler.c" ;;
                    5) echo "    Expected location: osd_oi_insert() in osd_oi.c" ;;
                esac
                echo ""
            fi
        done
        
        # Show comprehensive timing breakdown
        echo "Comprehensive Timing Breakdown (per operation):"
        echo "Format: [Layer] Operation: Average time"
        echo ""
        
        # OSD layer breakdown
        echo "OSD Layer Operations:"
        for op in "osd_trans_exec_op" "osd_create_type_f" "unlock_new_inode" "osd_attr_init" "osd_object_init0" "osd_trans_exec_check" "__osd_create total" "osd_ea_fid_set" "__osd_oi_insert" "osd_idc_find_and_init" "Total osd_create"; do
            local avg=$(grep "$op took" "$log_file" 2>/dev/null | awk '{print $(NF-1)}' | awk '{sum+=$1; count++} END {if(count>0) printf "%.1f", sum/count; else print "0"}' 2>/dev/null)
            [[ -z "$avg" ]] && avg="0"
            printf "  %-35s: %s µs\n" "$op" "$avg"
        done
        echo ""
        
        # MDD layer breakdown  
        echo "MDD Layer Operations:"
        for op in "mdo_create_object" "mdd_create_object_internal total" "mdo_attr_set"; do
            local avg=$(grep "$op took" "$log_file" 2>/dev/null | awk '{print $(NF-1)}' | awk '{sum+=$1; count++} END {if(count>0) printf "%.1f", sum/count; else print "0"}' 2>/dev/null)
            [[ -z "$avg" ]] && avg="0"
            printf "  %-35s: %s µs\n" "$op" "$avg"
        done
    fi
}

# Function to print complete kernel logs to console
print_complete_logs() {
    local complete_log_file="complete_$LOG_FILE"
    
    print_info "Displaying complete kernel logs from test execution..."
    echo ""
    echo "======================================================================="
    echo "               COMPLETE KERNEL LOGS FROM TEST START"
    echo "======================================================================="
    
    if [[ -f "$complete_log_file" ]]; then
        cat "$complete_log_file"
    else
        print_warning "Complete log file $complete_log_file not found!"
        print_info "Attempting to capture logs directly..."
        
        # Try to get logs directly
        echo ""
        echo "=== DIRECT DMESG OUTPUT ==="
        if command -v dmesg >/dev/null 2>&1; then
            dmesg | awk -v marker="$KERNEL_LOG_MARK" '
            BEGIN { found = 0 }
            {
                if (found || index($0, marker)) {
                    found = 1
                    print $0
                }
            }'
        else
            print_error "dmesg command not available"
        fi
    fi
    
    echo ""
    echo "======================================================================="
    echo "                     END OF COMPLETE KERNEL LOGS"
    echo "======================================================================="
}

# Function to display usage
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -m, --mount MOUNT_POINT    Lustre mount point (default: $LUSTRE_MOUNT_POINT)"
    echo "  -n, --num-files NUM        Number of files to create/delete (default: $NUM_FILES)"
    echo "  -p, --prefix PREFIX        File name prefix (default: $TEST_FILE_PREFIX)"
    echo "  -o, --output FILE          Output log file (default: $LOG_FILE)"
    echo "  -c, --cleanup-only         Only cleanup existing test files"
    echo "  -a, --analyze-only FILE    Only analyze existing log file"
    echo "  --no-complete-logs         Don't display complete kernel logs at the end"
    echo "  -h, --help                 Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                         # Run with default settings"
    echo "  $0 -n 20 -m /lustre        # Create/delete 20 files in /lustre"
    echo "  $0 --no-complete-logs      # Run test but don't show complete logs"
    echo "  $0 -c                      # Only cleanup test files"
    echo "  $0 -a timing.log           # Only analyze existing log file"
    echo ""
    echo "Note: The script will create and delete files, capturing both filtered timing logs"
    echo "      and complete kernel logs from the test start. It also tracks memory allocations."
    echo "      Enhanced version captures Stage 1-5 messages and memory allocation patterns"
    echo "      (OBD_ALLOC_PTR, OBD_ALLOC_PTR_ARRAY_LARGE, etc.) from modified Lustre source."
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -m|--mount)
            LUSTRE_MOUNT_POINT="$2"
            shift 2
            ;;
        -n|--num-files)
            NUM_FILES="$2"
            shift 2
            ;;
        -p|--prefix)
            TEST_FILE_PREFIX="$2"
            shift 2
            ;;
        -o|--output)
            LOG_FILE="$2"
            shift 2
            ;;
        --no-complete-logs)
            SHOW_COMPLETE_LOGS=false
            shift
            ;;
        -c|--cleanup-only)
            check_lustre_mount
            cleanup_test_files
            exit 0
            ;;
        -a|--analyze-only)
            analyze_logs "$2"
            exit 0
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Main execution
main() {
    print_info "Lustre CREATE/DELETE File Performance Test (Enhanced with Stage Analysis & Memory Tracking)"
    print_info "=============================================================================================="
    
    # Checks
    check_root
    check_lustre_mount
    
    # Cleanup any existing test files
    cleanup_test_files
    
    # Set up trap to cleanup on exit
    trap cleanup_test_files EXIT
    
    # Create log marker
    create_log_marker
    
    # Wait a moment for the marker to appear in logs
    sleep 1
    
    # Perform the tests (create and delete)
    perform_open_tests
    
    # Wait a moment for all kernel messages to be written
    sleep 2
    
    # Capture filtered kernel logs for analysis
    capture_kernel_logs "$LOG_FILE"
    
    # Capture complete kernel logs from test start
    local complete_log_file="complete_$LOG_FILE"
    capture_complete_kernel_logs "$complete_log_file"
    
    # Analyze the results
    analyze_logs "$LOG_FILE"
    
    print_success "Test completed! Results saved to $LOG_FILE"
    print_success "Complete logs saved to $complete_log_file"
    print_info "You can re-analyze the results anytime with: $0 -a $LOG_FILE"
    
    # Print complete kernel logs to console (unless disabled)
    if [[ "$SHOW_COMPLETE_LOGS" == "true" ]]; then
        echo ""
        print_complete_logs
    else
        print_info "Complete kernel logs display disabled. Check $complete_log_file for full logs."
    fi
}

# Check if bc is available for floating point calculations
if ! command -v bc >/dev/null 2>&1; then
    print_warning "bc (calculator) not found. Install it for better timing calculations."
    print_info "On RHEL/CentOS: yum install bc"
    print_info "On Ubuntu/Debian: apt install bc"
fi

# Run main function
main
