#!/bin/bash

# Lustre ZFS FILE CREATE Performance Testing Script
# This script creates files and captures kernel timing logs for REINT_OPEN operations
# Enhanced version for osd-zfs backend with Stage 1-5 timing logs

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
        echo "=== Looking for MDT_TIMING, MDD_TIMING, OSD_TIMING, REINT_OPEN, and Stage messages ==="
        echo ""
        
        # Method 1: dmesg (most common) - only get logs since our marker
        if command -v dmesg >/dev/null 2>&1; then
            echo "=== DMESG OUTPUT (since test start) ==="
            dmesg | awk -v marker="$KERNEL_LOG_MARK" '
            BEGIN { found = 0 }
            {
                if (found || index($0, marker)) {
                    found = 1
                    if (match($0, /(MDT_TIMING|MDD_TIMING|OSD_TIMING|REINT_OPEN|Stage [1-5]|MDT_DEBUG.*OPEN)/)) {
                        print $0
                    }
                }
            }'
            echo ""
        fi
        
        # Method 2: journalctl (systemd systems) - since our marker timestamp
        if command -v journalctl >/dev/null 2>&1; then
            echo "=== JOURNALCTL OUTPUT (since test start) ==="
            local marker_timestamp=$(echo "$KERNEL_LOG_MARK" | grep -o '[0-9]\+')
            if [[ -n "$marker_timestamp" ]]; then
                local marker_date=$(date -d "@$marker_timestamp" "+%Y-%m-%d %H:%M:%S" 2>/dev/null)
                if [[ -n "$marker_date" ]]; then
                    journalctl --dmesg --no-pager --since "$marker_date" 2>/dev/null | grep -E "(MDT_TIMING|MDD_TIMING|OSD_TIMING|REINT_OPEN|Stage [1-5]|MDT_DEBUG.*OPEN)"
                fi
            fi
            echo ""
        fi
        
    } > "$output_file"
    
    print_success "Kernel logs captured to $output_file"
}

# Function to perform the OPEN tests
perform_open_tests() {
    print_info "Starting OPEN file tests..."
    print_info "Creating $NUM_FILES files in $LUSTRE_MOUNT_POINT"
    
    # Record start time
    local start_time=$(date +%s.%N)
    
    # Create files one by one
    for i in $(seq 1 $NUM_FILES); do
        test_file="$LUSTRE_MOUNT_POINT/${TEST_FILE_PREFIX}_$i.txt"
        
        print_info "Creating file $i/$NUM_FILES: $(basename $test_file)"
        
        # Record individual operation time
        local op_start=$(date +%s.%N)
        
        # Create file using touch (triggers REINT_OPEN)
        if touch "$test_file" 2>/dev/null; then
            local op_end=$(date +%s.%N)
            # Use awk for floating point math if bc is not available
            if command -v bc >/dev/null 2>&1; then
                local op_duration=$(echo "$op_end - $op_start" | bc -l)
                printf "  ✓ Created in %.3f seconds\n" $op_duration
            else
                local op_duration_ms=$(awk "BEGIN {printf \"%.3f\", ($op_end - $op_start) * 1000}")
                printf "  ✓ Created in %s ms\n" $op_duration_ms
            fi
            
            # Add a small delay to ensure logs are separated
            sleep 0.1
        else
            print_error "Failed to create $test_file"
        fi
    done
    
    # Record end time and calculate totals
    local end_time=$(date +%s.%N)
    if command -v bc >/dev/null 2>&1; then
        local total_duration=$(echo "$end_time - $start_time" | bc -l)
        printf "${GREEN}Total time: %.3f seconds${NC}\n" $total_duration
        printf "${GREEN}Average time per file: %.3f seconds${NC}\n" $(echo "$total_duration / $NUM_FILES" | bc -l)
    else
        local total_duration_ms=$(awk "BEGIN {printf \"%.3f\", ($end_time - $start_time) * 1000}")
        printf "${GREEN}Total time: %s ms${NC}\n" $total_duration_ms
        local avg_duration_ms=$(awk "BEGIN {printf \"%.3f\", $total_duration_ms / $NUM_FILES}")
        printf "${GREEN}Average time per file: %s ms${NC}\n" $avg_duration_ms
    fi
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
    
    print_info "Analyzing timing data from $log_file..."
    echo ""
    
    # Extract and summarize timing information
    echo "=== TIMING ANALYSIS ==="
    
    # Stage-by-stage analysis (fixed parsing)
    echo "Stage Analysis (File Creation Process for ZFS backend):"
    echo "Note: Stages may execute out of logical order due to Lustre's internal flow"
    echo ""
    
    for stage in 1 2 3 4 5; do
        # Fixed: properly count stage occurrences without newline issues
        local stage_count=$(grep -c "Stage $stage" "$log_file" 2>/dev/null | head -1)
        # Remove any trailing newlines or whitespace
        stage_count=$(echo "$stage_count" | tr -d '\n\r ')
        # Default to 0 if empty
        stage_count=${stage_count:-0}
        
        echo "  Stage $stage messages ($stage_count occurrences):"
        case $stage in
            1) echo "    Function: mdt_reint_internal (MDT layer)" ;;
            2) echo "    Function: __osd_object_create (ZFS DMU create)" ;;
            3) echo "    Function: osd_create (FID allocation)" ;;
            4) echo "    Function: osd_oi_find_or_create (OI mapping)" ;;
            5) echo "    Function: osd_xattr_update (xattr setting)" ;;
        esac
        
        if [[ $stage_count -gt 0 ]]; then
            grep "Stage $stage" "$log_file" 2>/dev/null | head -3 | sed 's/^/    /'
        else
            echo "    [No Stage $stage messages found]"
            case $stage in
                4) echo "    Note: Stage 4 may not execute if OI mapping already exists" ;;
            esac
        fi
        echo ""
    done
    
    # MDT layer timings (REINT_OPEN operations)
    echo "MDT Layer Timings (REINT_OPEN):"
    local mdt_count=$(grep -c "MDT_TIMING.*OPEN" "$log_file" 2>/dev/null || echo "0")
    if [[ $mdt_count -gt 0 ]]; then
        grep "MDT_TIMING.*OPEN" "$log_file" | head -10
    else
        echo "  [No MDT_TIMING OPEN messages found]"
    fi
    echo ""
    
    # MDD layer timings
    echo "MDD Layer Timings:"
    local mdd_count=$(grep -c "MDD_TIMING" "$log_file" 2>/dev/null || echo "0")
    if [[ $mdd_count -gt 0 ]]; then
        grep "MDD_TIMING" "$log_file" | head -10
    else
        echo "  [No MDD_TIMING messages found]"
    fi
    echo ""
    
    # OSD layer timings (ZFS specific)
    echo "OSD Layer Timings (ZFS specific):"
    local osd_count=$(grep -c "OSD_TIMING" "$log_file" 2>/dev/null || echo "0")
    if [[ $osd_count -gt 0 ]]; then
        grep "OSD_TIMING" "$log_file" | head -10
    else
        echo "  [No OSD_TIMING messages found]"
    fi
    echo ""
    
    # Summary statistics
    echo "=== SUMMARY STATISTICS ==="
    
    # Stage statistics
    echo "Stage Occurrence Count:"
    for stage in 1 2 3 4 5; do
        local count=$(grep -c "Stage $stage" "$log_file" 2>/dev/null | head -1)
        count=$(echo "$count" | tr -d '\n\r ')
        count=${count:-0}
        printf "  Stage %d: %s occurrences\n" $stage $count
    done
    echo ""
    
    # Total files created count
    local total_files=$(grep -c "MDT_DEBUG.*OPEN" "$log_file" 2>/dev/null || echo "0")
    echo "  Total files created: $total_files"
    echo ""
    
    # Extract timing statistics if available
    if command -v awk >/dev/null 2>&1; then
        echo "MDT reint_rec OPEN timings (microseconds):"
        local mdt_timings=$(grep "mdt_reint_rec OPEN took" "$log_file" 2>/dev/null | awk '{print $(NF-1)}' | sort -n)
        if [[ -n "$mdt_timings" ]]; then
            echo "$mdt_timings" | awk '
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
                }
            }'
        else
            echo "  [No timing data found]"
        fi
        echo ""
        
        echo "ZFS OSD Operations (milliseconds):"
        local zfs_timings=$(grep -E "(DMU create|FID Allocation|xattr_update).*took.*ms" "$log_file" 2>/dev/null)
        if [[ -n "$zfs_timings" ]]; then
            echo "$zfs_timings" | head -10
        else
            echo "  [No ZFS timing data found]"
        fi
        echo ""
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
    echo "  -n, --num-files NUM        Number of files to create (default: $NUM_FILES)"
    echo "  -p, --prefix PREFIX        File name prefix (default: $TEST_FILE_PREFIX)"
    echo "  -o, --output FILE          Output log file (default: $LOG_FILE)"
    echo "  -c, --cleanup-only         Only cleanup existing test files"
    echo "  -a, --analyze-only FILE    Only analyze existing log file"
    echo "  --no-complete-logs         Don't display complete kernel logs at the end"
    echo "  -h, --help                 Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                         # Run with default settings"
    echo "  $0 -n 20 -m /lustre        # Create 20 files in /lustre"
    echo "  $0 --no-complete-logs      # Run test but don't show complete logs"
    echo "  $0 -c                      # Only cleanup test files"
    echo "  $0 -a timing.log           # Only analyze existing log file"
    echo ""
    echo "Note: This is the ZFS backend version. The script captures both filtered"
    echo "      timing logs and complete kernel logs from test start."
    echo "      Enhanced version captures Stage 1-5 messages from modified Lustre source."
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
    print_info "Lustre ZFS OPEN File Performance Test (Enhanced with Stage Analysis)"
    print_info "===================================================================="
    
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
    
    # Perform the tests
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
    print_warning "bc (calculator) not found. Will use awk for calculations."
    print_info "For better precision, install bc:"
    print_info "On RHEL/CentOS: yum install bc"
    print_info "On Ubuntu/Debian: apt install bc"
fi

# Run main function
main 