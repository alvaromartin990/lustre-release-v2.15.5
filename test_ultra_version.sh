#!/bin/bash

# Quick test for ultra-simplified version
echo "=== Testing Ultra-Simplified Version ==="
echo "Date: $(date)"
echo "Directory: $(pwd)"
echo ""

echo "1. Testing ultra-simplified version (no is_vmalloc_addr)..."
if [[ -f "ultra_simple_test.c" ]] && [[ -f "Makefile.ultra" ]]; then
    echo "   Cleaning..."
    make -f Makefile.ultra clean 2>/dev/null || true
    
    echo "   Building..."
    if make -f Makefile.ultra 2>&1 | tee build_ultra.log; then
        echo "   ✓ Build completed"
        
        if [[ -f "ultra_simple_test.ko" ]]; then
            echo "   ✓ Module file created: ultra_simple_test.ko"
            echo "   Size: $(ls -lh ultra_simple_test.ko | awk '{print $5}')"
            
            # Test module info
            if modinfo ultra_simple_test.ko > /dev/null 2>&1; then
                echo "   ✓ Module is valid"
                echo "   Module info:"
                modinfo ultra_simple_test.ko | head -10
                
                # Test loading (if root)
                if [[ $EUID -eq 0 ]]; then
                    echo ""
                    echo "   Testing module load..."
                    if insmod ultra_simple_test.ko; then
                        echo "   ✓ Module loaded successfully"
                        sleep 3
                        echo ""
                        echo "   Test results:"
                        dmesg | grep "ultra_test" | tail -20
                        echo ""
                        rmmod ultra_simple_test 2>/dev/null || true
                        echo "   ✓ Module unloaded"
                        echo ""
                        echo "   SUCCESS: Ultra-simplified test worked!"
                    else
                        echo "   ✗ Module load failed"
                        dmesg | tail -5
                    fi
                else
                    echo ""
                    echo "   Not root - skipping load test"
                    echo "   To test: sudo insmod ultra_simple_test.ko"
                    echo "   SUCCESS: Module compiled successfully!"
                fi
            else
                echo "   ✗ Module validation failed"
            fi
        else
            echo "   ✗ Module file not created"
            echo "   Build log:"
            cat build_ultra.log
            echo ""
            echo "   If this still fails, run the ultimate diagnostic:"
            echo "   sudo ./ultimate_diagnostic.sh"
        fi
    else
        echo "   ✗ Build failed"
        echo "   Build log:"
        cat build_ultra.log
    fi
else
    echo "   Ultra-simplified test files not found"
fi

echo ""
echo "=== Ultra Test Complete ==="
