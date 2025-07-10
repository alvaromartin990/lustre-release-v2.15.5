#!/bin/bash

# Quick test script for the fixed version
echo "=== Testing Fixed Version ==="
echo "Date: $(date)"
echo "Directory: $(pwd)"
echo ""

# Test the fixed version
echo "1. Testing simplified fixed version..."
if [[ -f "simple_fixed_test.c" ]] && [[ -f "Makefile.fixed" ]]; then
    echo "   Cleaning..."
    make -f Makefile.fixed clean 2>/dev/null || true
    
    echo "   Building..."
    if make -f Makefile.fixed 2>&1 | tee build_fixed.log; then
        echo "   ✓ Build completed"
        
        if [[ -f "simple_fixed_test.ko" ]]; then
            echo "   ✓ Module file created: simple_fixed_test.ko"
            echo "   Size: $(ls -lh simple_fixed_test.ko | awk '{print $5}')"
            
            # Test module info
            if modinfo simple_fixed_test.ko > /dev/null 2>&1; then
                echo "   ✓ Module is valid"
                
                # Test loading (if root)
                if [[ $EUID -eq 0 ]]; then
                    echo "   Testing module load..."
                    if insmod simple_fixed_test.ko; then
                        echo "   ✓ Module loaded successfully"
                        sleep 2
                        echo "   Test results:"
                        dmesg | grep "simple_test" | tail -10
                        rmmod simple_fixed_test 2>/dev/null || true
                        echo "   ✓ Module unloaded"
                    else
                        echo "   ✗ Module load failed"
                        dmesg | tail -5
                    fi
                else
                    echo "   Not root - skipping load test"
                    echo "   To test: sudo insmod simple_fixed_test.ko"
                fi
            else
                echo "   ✗ Module validation failed"
            fi
        else
            echo "   ✗ Module file not created"
            echo "   Build log:"
            cat build_fixed.log
        fi
    else
        echo "   ✗ Build failed"
        echo "   Build log:"
        cat build_fixed.log
    fi
else
    echo "   Fixed test files not found"
fi

echo ""
echo "=== Test Complete ==="
