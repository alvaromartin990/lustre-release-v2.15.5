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

# Build the module
echo ""
echo "4. Building module..."
if make -f Makefile.simple modules 2>&1 | tee build.log; then
    echo "   ✓ Build completed successfully"
else
    echo "   ✗ Build failed"
    echo "   Build log:"
    cat build.log
    exit 1
fi

# Check if module file was created
echo ""
echo "5. Checking module file..."
if [[ -f "simple_obd_alloc_test.ko" ]]; then
    echo "   ✓ Module file created: simple_obd_alloc_test.ko"
    echo "   File size: $(ls -lh simple_obd_alloc_test.ko | awk '{print $5}')"
    echo "   Full path: $(pwd)/simple_obd_alloc_test.ko"
else
    echo "   ✗ Module file not created"
    echo "   Looking for any .ko files:"
    ls -la *.ko 2>/dev/null || echo "   No .ko files found"
    exit 1
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

echo ""
echo "=== Test Complete ==="
echo "If all checks passed, the module should work correctly."
echo "Try running: sudo $(pwd)/run_obd_alloc_test.sh"
