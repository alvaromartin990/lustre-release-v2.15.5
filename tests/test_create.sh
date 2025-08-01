#!/bin/bash

# Lustre CREATE Directory Performance Testing Script
# This script creates directories and captures kernel timing logs

# Configuration variables
LUSTRE_MOUNT_POINT="/mnt/lustre"  # Change this to your Lustre mount point
TEST_DIR_PREFIX="test_create_dir"
NUM_DIRS=10
LOG_FILE="lustre_create_timing.log"
KERNEL_LOG_MARK="LUSTRE_CREATE_TEST_START_$(date +%s)"

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

# Function to cleanup test directories
cleanup_test_dirs() {
    print_info "Cleaning up test directories..."
    for i in $(seq 1 $NUM_DIRS); do
        test_dir="$LUSTRE_MOUNT_POINT/${TEST_DIR_PREFIX}_$i"
        if [[ -d "$test_dir" ]]; then
            rmdir "$test_dir" 2>/dev/null
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
        echo "=== Looking for MDT_TIMING, MDD_TIMING, and OSD_TIMING messages ==="
        echo ""
        
        # Method 1: dmesg (most common)
        if command -v dmesg >/dev/null 2>&1; then
            echo "=== DMESG OUTPUT ==="
            dmesg | grep -E "(MDT_TIMING|MDD_TIMING|OSD_TIMING|$KERNEL_LOG_MARK)" | tail -100
            echo ""
        fi
        
        # Method 2: journalctl (systemd systems)
        if command -v journalctl >/dev/null 2>&1; then
            echo "=== JOURNALCTL OUTPUT ==="
            journalctl --dmesg --no-pager | grep -E "(MDT_TIMING|MDD_TIMING|OSD_TIMING|$KERNEL_LOG_MARK)" | tail -100
            echo ""
        fi
        
        # Method 3: /var/log/messages or /var/log/kern.log
        for log_file in /var/log/messages /var/log/kern.log; do
            if [[ -r "$log_file" ]]; then
                echo "=== $log_file OUTPUT ==="
                grep -E "(MDT_TIMING|MDD_TIMING|OSD_TIMING|$KERNEL_LOG_MARK)" "$log_file" | tail -50
                echo ""
            fi
        done
        
    } > "$output_file"
    
    print_success "Kernel logs captured to $output_file"
}

# Function to perform the CREATE tests
perform_create_tests() {
    print_info "Starting CREATE directory tests..."
    print_info "Creating $NUM_DIRS directories in $LUSTRE_MOUNT_POINT"
    
    # Record start time
    local start_time=$(date +%s.%N)
    
    # Create directories one by one
    for i in $(seq 1 $NUM_DIRS); do
        test_dir="$LUSTRE_MOUNT_POINT/${TEST_DIR_PREFIX}_$i"
        
        print_info "Creating directory $i/$NUM_DIRS: $(basename $test_dir)"
        
        # Record individual operation time
        local op_start=$(date +%s.%N)
        
        if mkdir "$test_dir" 2>/dev/null; then
            local op_end=$(date +%s.%N)
            local op_duration=$(echo "$op_end - $op_start" | bc -l)
            printf "  ✓ Created in %.3f seconds\n" $op_duration
            
            # Add a small delay to ensure logs are separated
            sleep 0.1
        else
            print_error "Failed to create $test_dir"
        fi
    done
    
    # Record end time
    local end_time=$(date +%s.%N)
    local total_duration=$(echo "$end_time - $start_time" | bc -l)
    
    print_success "All directories created"
    printf "${GREEN}Total time: %.3f seconds${NC}\n" $total_duration
    printf "${GREEN}Average time per directory: %.3f seconds${NC}\n" $(echo "$total_duration / $NUM_DIRS" | bc -l)
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
    
    # MDT layer timings
    echo "MDT Layer Timings:"
    grep "MDT_TIMING" "$log_file" | head -20
    echo ""
    
    # MDD layer timings
    echo "MDD Layer Timings:"
    grep "MDD_TIMING" "$log_file" | head -20
    echo ""
    
    # OSD layer timings
    echo "OSD Layer Timings:"
    grep "OSD_TIMING" "$log_file" | head -20
    echo ""
    
    # Summary statistics
    echo "=== SUMMARY STATISTICS ==="
    
    # Extract microsecond values and calculate statistics
    if command -v awk >/dev/null 2>&1; then
        echo "MDT reint_rec timings (microseconds):"
        grep "mdt_reint_rec CREATE took" "$log_file" | awk '{print $(NF-1)}' | sort -n | awk '
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
        echo ""
        
        echo "OSD __osd_create timings (microseconds):"
        grep "__osd_create total took" "$log_file" | awk '{print $(NF-4)}' | sort -n | awk '
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
    fi
}

# Function to display usage
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -m, --mount MOUNT_POINT    Lustre mount point (default: $LUSTRE_MOUNT_POINT)"
    echo "  -n, --num-dirs NUM         Number of directories to create (default: $NUM_DIRS)"
    echo "  -p, --prefix PREFIX        Directory name prefix (default: $TEST_DIR_PREFIX)"
    echo "  -o, --output FILE          Output log file (default: $LOG_FILE)"
    echo "  -c, --cleanup-only         Only cleanup existing test directories"
    echo "  -a, --analyze-only FILE    Only analyze existing log file"
    echo "  -h, --help                 Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                         # Run with default settings"
    echo "  $0 -n 20 -m /lustre        # Create 20 directories in /lustre"
    echo "  $0 -c                      # Only cleanup test directories"
    echo "  $0 -a timing.log           # Only analyze existing log file"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -m|--mount)
            LUSTRE_MOUNT_POINT="$2"
            shift 2
            ;;
        -n|--num-dirs)
            NUM_DIRS="$2"
            shift 2
            ;;
        -p|--prefix)
            TEST_DIR_PREFIX="$2"
            shift 2
            ;;
        -o|--output)
            LOG_FILE="$2"
            shift 2
            ;;
        -c|--cleanup-only)
            check_lustre_mount
            cleanup_test_dirs
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
    print_info "Lustre CREATE Directory Performance Test"
    print_info "======================================="
    
    # Checks
    check_root
    check_lustre_mount
    
    # Cleanup any existing test directories
    cleanup_test_dirs
    
    # Set up trap to cleanup on exit
    trap cleanup_test_dirs EXIT
    
    # Create log marker
    create_log_marker
    
    # Wait a moment for the marker to appear in logs
    sleep 1
    
    # Perform the tests
    perform_create_tests
    
    # Wait a moment for all kernel messages to be written
    sleep 2
    
    # Capture kernel logs
    capture_kernel_logs "$LOG_FILE"
    
    # Analyze the results
    analyze_logs "$LOG_FILE"
    
    print_success "Test completed! Results saved to $LOG_FILE"
    print_info "You can re-analyze the results anytime with: $0 -a $LOG_FILE"
}

# Check if bc is available for floating point calculations
if ! command -v bc >/dev/null 2>&1; then
    print_warning "bc (calculator) not found. Install it for better timing calculations."
    print_info "On RHEL/CentOS: yum install bc"
    print_info "On Ubuntu/Debian: apt install bc"
fi

# Run main function
main
