#!/bin/bash

# Test script for OBD_ALLOC_PTR_ARRAY_LARGE kernel module testing
# This script automates the building and testing of the kernel modules

set -e  # Exit on any error

# Configuration
SIMPLE_MODULE="simple_obd_alloc_test"
FULL_MODULE="test_obd_alloc_idmap_cache"
LOG_FILE="/tmp/obd_alloc_test_$(date +%Y%m%d_%H%M%S).log"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
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

# Function to check if running as root
check_root() {
    if [[ $EUID -eq 0 ]]; then
        print_warning "Running as root - this is required for module loading"
    else
        print_error "This script requires root privileges for module loading"
        print_status "Please run with sudo: sudo $0"
        exit 1
    fi
}

# Function to check kernel headers
check_kernel_headers() {
    local kernel_version=$(uname -r)
    local headers_dir="/lib/modules/$kernel_version/build"
    
    if [[ ! -d "$headers_dir" ]]; then
        print_error "Kernel headers not found at $headers_dir"
        print_status "Please install kernel headers for your kernel version"
        print_status "On Ubuntu/Debian: sudo apt-get install linux-headers-$(uname -r)"
        print_status "On CentOS/RHEL: sudo yum install kernel-devel"
        exit 1
    fi
    
    print_success "Kernel headers found at $headers_dir"
}

# Function to clean up loaded modules
cleanup_modules() {
    print_status "Cleaning up any loaded test modules..."
    
    if lsmod | grep -q "$SIMPLE_MODULE"; then
        rmmod "$SIMPLE_MODULE" 2>/dev/null || true
        print_status "Removed $SIMPLE_MODULE module"
    fi
    
    if lsmod | grep -q "$FULL_MODULE"; then
        rmmod "$FULL_MODULE" 2>/dev/null || true
        print_status "Removed $FULL_MODULE module"
    fi
}

# Function to build simple module
build_simple_module() {
    print_status "Building simple test module..."
    
    if ! make -f Makefile.simple clean > /dev/null 2>&1; then
        print_warning "Clean failed (may be normal)"
    fi
    
    if make -f Makefile.simple modules; then
        print_success "Simple module built successfully"
        return 0
    else
        print_error "Failed to build simple module"
        return 1
    fi
}

# Function to test simple module
test_simple_module() {
    print_status "Testing simple module..."
    
    # Load the module
    if insmod "${SIMPLE_MODULE}.ko"; then
        print_success "Simple module loaded successfully"
        
        # Wait for tests to complete
        sleep 2
        
        # Capture test output
        local test_output=$(dmesg | grep "simple_test" | tail -20)
        
        # Check for success message
        if echo "$test_output" | grep -q "All tests completed successfully"; then
            print_success "Simple module tests passed!"
            echo "$test_output" >> "$LOG_FILE"
            
            # Show key results
            echo "$test_output" | grep -E "(Loading|sizeof|Testing|successful|completed)"
            
        else
            print_error "Simple module tests failed"
            echo "$test_output" >> "$LOG_FILE"
            echo "$test_output"
        fi
        
        # Unload the module
        if rmmod "$SIMPLE_MODULE"; then
            print_success "Simple module unloaded successfully"
        else
            print_warning "Failed to unload simple module"
        fi
        
    else
        print_error "Failed to load simple module"
        return 1
    fi
}

# Function to build full module (if possible)
build_full_module() {
    print_status "Attempting to build full Lustre module..."
    
    if ! make -f Makefile.test clean > /dev/null 2>&1; then
        print_warning "Clean failed (may be normal)"
    fi
    
    if make -f Makefile.test modules 2>&1; then
        print_success "Full module built successfully"
        return 0
    else
        print_warning "Full module build failed (likely missing Lustre headers)"
        return 1
    fi
}

# Function to run system information
show_system_info() {
    print_status "System Information:"
    echo "Kernel Version: $(uname -r)"
    echo "Architecture: $(uname -m)"
    echo "Memory: $(free -h | head -2 | tail -1)"
    echo "GCC Version: $(gcc --version | head -1)"
    echo "Date: $(date)"
    echo ""
}

# Function to run performance test
run_performance_test() {
    print_status "Running performance analysis..."
    
    # Load module and capture detailed timing
    if insmod "${SIMPLE_MODULE}.ko"; then
        sleep 3
        
        # Extract timing information
        local timing_data=$(dmesg | grep "simple_test" | grep -E "(successful in|bytes)")
        
        if [[ -n "$timing_data" ]]; then
            print_success "Performance data captured:"
            echo "$timing_data"
            echo "$timing_data" >> "$LOG_FILE"
        fi
        
        rmmod "$SIMPLE_MODULE" 2>/dev/null || true
    fi
}

# Main execution
main() {
    print_status "Starting OBD_ALLOC_PTR_ARRAY_LARGE test suite"
    print_status "Log file: $LOG_FILE"
    
    # Initialize log file
    echo "OBD_ALLOC_PTR_ARRAY_LARGE Test Results" > "$LOG_FILE"
    echo "=======================================" >> "$LOG_FILE"
    echo "Date: $(date)" >> "$LOG_FILE"
    echo "Kernel: $(uname -r)" >> "$LOG_FILE"
    echo "" >> "$LOG_FILE"
    
    # System checks
    show_system_info
    check_root
    check_kernel_headers
    cleanup_modules
    
    # Test simple module
    if build_simple_module; then
        test_simple_module
        run_performance_test
    else
        print_error "Cannot proceed without simple module"
        exit 1
    fi
    
    # Attempt full module test
    if build_full_module; then
        print_status "Full module build successful - you can test manually"
    else
        print_warning "Full module build failed - using simple module only"
    fi
    
    # Final cleanup
    cleanup_modules
    
    print_success "Test suite completed!"
    print_status "Detailed results saved to: $LOG_FILE"
    
    # Show summary
    if [[ -f "$LOG_FILE" ]]; then
        echo ""
        print_status "Test Summary:"
        grep -E "(SUCCESS|ERROR|All tests completed)" "$LOG_FILE" | tail -5
    fi
}

# Help function
show_help() {
    echo "Usage: $0 [OPTION]"
    echo ""
    echo "Options:"
    echo "  -h, --help     Show this help message"
    echo "  -c, --clean    Clean build artifacts and exit"
    echo "  -b, --build    Build modules only (no testing)"
    echo "  -t, --test     Run tests only (assume modules are built)"
    echo "  -v, --verbose  Enable verbose output"
    echo ""
    echo "This script tests the OBD_ALLOC_PTR_ARRAY_LARGE macro implementation"
    echo "by building and running kernel test modules."
}

# Parse command line arguments
case "${1:-}" in
    -h|--help)
        show_help
        exit 0
        ;;
    -c|--clean)
        print_status "Cleaning build artifacts..."
        make -f Makefile.simple clean 2>/dev/null || true
        make -f Makefile.test clean 2>/dev/null || true
        print_success "Clean completed"
        exit 0
        ;;
    -b|--build)
        print_status "Building modules only..."
        check_kernel_headers
        build_simple_module
        build_full_module
        exit 0
        ;;
    -t|--test)
        print_status "Running tests only..."
        check_root
        cleanup_modules
        test_simple_module
        exit 0
        ;;
    -v|--verbose)
        set -x
        main
        ;;
    "")
        main
        ;;
    *)
        print_error "Unknown option: $1"
        show_help
        exit 1
        ;;
esac
