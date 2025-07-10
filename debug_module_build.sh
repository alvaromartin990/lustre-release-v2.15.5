#!/bin/bash

# Simple test script to verify module building and loading
# This script helps debug the "No such file or directory" error

echo "=== Module Build and Load Test ==="
echo "Date: $(date)"
echo "Directory: $(pwd)"
echo "User: $(whoami)"
echo ""

# Check if source files exist
echo "1. Checking source files..."
if [[ -f "simple_obd_alloc_test.c" ]]; then
    echo "   ✓ simple_obd_alloc_test.c exists"
else
    echo "   ✗ simple_obd_alloc_test.c missing"
    exit 1
fi

if [[ -f "Makefile.simple" ]]; then
    echo "   ✓ Makefile.simple exists"
else
    echo "   ✗ Makefile.simple missing"
    exit 1
fi

# Check kernel build environment
echo ""
echo "2. Checking kernel build environment..."
KERNEL_BUILD="/lib/modules/$(uname -r)/build"
if [[ -d "$KERNEL_BUILD" ]]; then
    echo "   ✓ Kernel build directory: $KERNEL_BUILD"
else
    echo "   ✗ Kernel build directory not found: $KERNEL_BUILD"
    exit 1
fi

# Clean previous builds
echo ""
echo "3. Cleaning previous builds..."
make -f Makefile.simple clean 2>/dev/null || true
echo "   ✓ Clean completed"

# Test basic compilation first
echo ""
echo "3a. Testing basic compilation..."
echo "   Checking if we can compile a minimal module first..."

if [[ -f "minimal_test.c" ]] && [[ -f "Makefile.minimal_test" ]]; then
    echo "   Building minimal test module..."
    make -f Makefile.minimal_test clean 2>/dev/null || true
    if make -f Makefile.minimal_test 2>&1 | tee build_minimal_test.log; then
        if [[ -f "minimal_test.ko" ]]; then
            echo "   ✓ Minimal module built successfully"
            echo "   ✓ Basic kernel module compilation works"
            rm -f minimal_test.ko minimal_test.o minimal_test.mod* 2>/dev/null || true
        else
            echo "   ✗ Minimal module build succeeded but no .ko file created"
            echo "   This indicates a kernel build system issue"
        fi
    else
        echo "   ✗ Minimal module build failed"
        echo "   This indicates a fundamental kernel build problem"
        cat build_minimal_test.log
        exit 1
    fi
else
    echo "   Minimal test files not found, skipping basic test"
fi

# Build the module
echo ""
echo "4. Building module..."
echo "   Build command: make -f Makefile.simple modules"
echo "   Build directory: $(pwd)"
echo "   Build output:"
if make -f Makefile.simple modules 2>&1 | tee build.log; then
    echo "   ✓ Build completed successfully"
    echo ""
    echo "   Build log contents:"
    cat build.log
else
    echo "   ✗ Build failed"
    echo "   Build log:"
    cat build.log
    exit 1
fi

# Test with minimal Makefile
echo ""
echo "4a. Testing with minimal Makefile..."
if [[ -f "Makefile.minimal" ]]; then
    echo "   Using Makefile.minimal for comparison test"
    make -f Makefile.minimal clean 2>/dev/null || true
    if make -f Makefile.minimal 2>&1 | tee build_minimal.log; then
        echo "   ✓ Minimal build completed successfully"
        echo "   Checking for .ko file after minimal build:"
        if [[ -f "simple_obd_alloc_test.ko" ]]; then
            echo "   ✓ Module created with minimal Makefile"
        else
            echo "   ✗ Module not created with minimal Makefile either"
            find . -name "*.ko" -type f 2>/dev/null || echo "   Still no .ko files found"
        fi
    else
        echo "   ✗ Minimal build failed"
        cat build_minimal.log
    fi
else
    echo "   Makefile.minimal not found, skipping minimal test"
fi

# Check if module file was created
echo ""
echo "5. Checking module file..."
echo "   Current directory: $(pwd)"
echo "   All files in current directory:"
ls -la
echo ""
echo "   Looking for .ko files recursively:"
find . -name "*.ko" -type f 2>/dev/null || echo "   No .ko files found anywhere"
echo ""
echo "   Looking for .o files:"
find . -name "*.o" -type f 2>/dev/null || echo "   No .o files found"
echo ""
echo "   Checking for build artifacts:"
find . -name "*.mod*" -type f 2>/dev/null || echo "   No .mod files found"
echo ""

if [[ -f "simple_obd_alloc_test.ko" ]]; then
    echo "   ✓ Module file created: simple_obd_alloc_test.ko"
    echo "   File size: $(ls -lh simple_obd_alloc_test.ko | awk '{print $5}')"
    echo "   Full path: $(pwd)/simple_obd_alloc_test.ko"
else
    echo "   ✗ Module file not created in current directory"
    
    # Check if it was created elsewhere
    KO_FILES=$(find . -name "simple_obd_alloc_test.ko" -type f 2>/dev/null)
    if [[ -n "$KO_FILES" ]]; then
        echo "   ✓ Found module file at: $KO_FILES"
        # Copy it to current directory
        cp "$KO_FILES" ./
        echo "   ✓ Copied module file to current directory"
    else
        echo "   ✗ Module file not found anywhere"
        echo "   This indicates a build issue. Let's check the Makefile:"
        echo ""
        echo "   Makefile.simple contents:"
        cat Makefile.simple
        exit 1
    fi
fi

# Verify module info
echo ""
echo "6. Verifying module..."
if modinfo simple_obd_alloc_test.ko 2>/dev/null; then
    echo "   ✓ Module is valid"
else
    echo "   ✗ Module verification failed"
    exit 1
fi

# Test module loading (requires root)
echo ""
echo "7. Testing module loading..."
if [[ $EUID -eq 0 ]]; then
    echo "   Running as root - attempting module load test..."
    
    # Try to load with relative path
    echo "   Trying: insmod simple_obd_alloc_test.ko"
    if insmod simple_obd_alloc_test.ko 2>&1; then
        echo "   ✓ Module loaded successfully (relative path)"
        rmmod simple_obd_alloc_test 2>/dev/null || true
    else
        echo "   ✗ Module load failed (relative path)"
        
        # Try to load with absolute path
        echo "   Trying: insmod $(pwd)/simple_obd_alloc_test.ko"
        if insmod "$(pwd)/simple_obd_alloc_test.ko" 2>&1; then
            echo "   ✓ Module loaded successfully (absolute path)"
            rmmod simple_obd_alloc_test 2>/dev/null || true
        else
            echo "   ✗ Module load failed (absolute path)"
            echo "   Error details:"
            dmesg | tail -5
        fi
    fi
else
    echo "   Not running as root - skipping load test"
    echo "   To test loading, run: sudo insmod $(pwd)/simple_obd_alloc_test.ko"
fi

# Check source file for obvious issues
echo ""
echo "1a. Checking source file for potential issues..."
echo "   Source file size: $(ls -lh simple_obd_alloc_test.c | awk '{print $5}')"
echo "   Line count: $(wc -l < simple_obd_alloc_test.c)"
echo "   Checking for basic module structure..."

if grep -q "module_init" simple_obd_alloc_test.c; then
    echo "   ✓ module_init found"
else
    echo "   ✗ module_init not found"
fi

if grep -q "module_exit" simple_obd_alloc_test.c; then
    echo "   ✓ module_exit found"
else
    echo "   ✗ module_exit not found"
fi

if grep -q "MODULE_LICENSE" simple_obd_alloc_test.c; then
    echo "   ✓ MODULE_LICENSE found"
else
    echo "   ✗ MODULE_LICENSE not found"
fi

echo "   Checking for obvious syntax issues..."
if grep -q "get_random_int" simple_obd_alloc_test.c; then
    echo "   ✓ Found get_random_int usage"
    # Check if the function exists in kernel
    if grep -q "get_random_int" /lib/modules/$(uname -r)/build/include/linux/random.h 2>/dev/null; then
        echo "   ✓ get_random_int available in kernel headers"
    else
        echo "   ⚠ get_random_int may not be available in this kernel version"
        echo "   This could cause compilation issues"
    fi
fi

echo ""
echo "=== Test Complete ==="
echo "If all checks passed, the module should work correctly."
echo "Try running: sudo $(pwd)/run_obd_alloc_test.sh"
