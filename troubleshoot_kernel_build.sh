#!/bin/bash

# Comprehensive troubleshooting script for kernel module build issues
# This script diagnoses common problems with kernel module compilation

echo "=== Kernel Module Build Troubleshooting ==="
echo "Date: $(date)"
echo "System: $(uname -a)"
echo "User: $(whoami)"
echo ""

# Function to check kernel version compatibility
check_kernel_version() {
    echo "1. Kernel Version Analysis"
    echo "   Kernel version: $(uname -r)"
    echo "   Kernel type: $(uname -v)"
    
    # Check if this is a custom kernel (like Lustre kernel)
    if echo "$(uname -r)" | grep -q "lustre"; then
        echo "   ✓ Lustre kernel detected"
        echo "   This may require special handling for module compilation"
    fi
    
    # Check kernel configuration
    if [[ -f "/boot/config-$(uname -r)" ]]; then
        echo "   ✓ Kernel config found: /boot/config-$(uname -r)"
        
        # Check if modules are enabled
        if grep -q "CONFIG_MODULES=y" "/boot/config-$(uname -r)"; then
            echo "   ✓ Kernel modules enabled"
        else
            echo "   ✗ Kernel modules may be disabled"
        fi
        
        # Check if loadable module support is enabled
        if grep -q "CONFIG_MODULE_UNLOAD=y" "/boot/config-$(uname -r)"; then
            echo "   ✓ Module unloading enabled"
        else
            echo "   ⚠ Module unloading may be disabled"
        fi
    else
        echo "   ⚠ Kernel config not found"
    fi
    echo ""
}

# Function to check build environment
check_build_environment() {
    echo "2. Build Environment Analysis"
    
    # Check kernel headers
    KERNEL_BUILD="/lib/modules/$(uname -r)/build"
    if [[ -d "$KERNEL_BUILD" ]]; then
        echo "   ✓ Kernel build directory: $KERNEL_BUILD"
        
        # Check if it's a real directory or symlink
        if [[ -L "$KERNEL_BUILD" ]]; then
            echo "   ✓ Symlink target: $(readlink -f $KERNEL_BUILD)"
        fi
        
        # Check key files
        if [[ -f "$KERNEL_BUILD/Makefile" ]]; then
            echo "   ✓ Kernel Makefile found"
        else
            echo "   ✗ Kernel Makefile missing"
        fi
        
        if [[ -f "$KERNEL_BUILD/scripts/Makefile.build" ]]; then
            echo "   ✓ Build scripts found"
        else
            echo "   ✗ Build scripts missing"
        fi
        
        # Check include directory
        if [[ -d "$KERNEL_BUILD/include" ]]; then
            echo "   ✓ Include directory found"
            echo "   Include files: $(find $KERNEL_BUILD/include -name "*.h" 2>/dev/null | wc -l) header files"
        else
            echo "   ✗ Include directory missing"
        fi
        
    else
        echo "   ✗ Kernel build directory not found: $KERNEL_BUILD"
        echo "   Possible solutions:"
        echo "   - Install kernel headers: yum install kernel-devel"
        echo "   - Or: apt-get install linux-headers-$(uname -r)"
    fi
    
    # Check compiler
    if command -v gcc > /dev/null 2>&1; then
        echo "   ✓ GCC compiler: $(gcc --version | head -1)"
    else
        echo "   ✗ GCC compiler not found"
    fi
    
    # Check make
    if command -v make > /dev/null 2>&1; then
        echo "   ✓ Make utility: $(make --version | head -1)"
    else
        echo "   ✗ Make utility not found"
    fi
    
    echo ""
}

# Function to test simple module build
test_simple_build() {
    echo "3. Simple Module Build Test"
    
    # Create a minimal test module
    cat > test_simple.c << 'EOF'
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simple test module");
MODULE_VERSION("1.0");

static int __init simple_init(void)
{
    printk(KERN_INFO "simple_test: Module loaded\n");
    return 0;
}

static void __exit simple_exit(void)
{
    printk(KERN_INFO "simple_test: Module unloaded\n");
}

module_init(simple_init);
module_exit(simple_exit);
EOF
    
    cat > Makefile.test_simple << 'EOF'
obj-m += test_simple.o
KERNEL_DIR := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
EOF
    
    echo "   Building simple test module..."
    if make -f Makefile.test_simple clean > /dev/null 2>&1; then
        echo "   ✓ Clean succeeded"
    else
        echo "   ⚠ Clean failed (may be normal)"
    fi
    
    if make -f Makefile.test_simple 2>&1 | tee build_simple.log; then
        echo "   ✓ Build succeeded"
        
        if [[ -f "test_simple.ko" ]]; then
            echo "   ✓ Module file created: test_simple.ko"
            echo "   Size: $(ls -lh test_simple.ko | awk '{print $5}')"
            
            # Test module info
            if modinfo test_simple.ko > /dev/null 2>&1; then
                echo "   ✓ Module is valid"
            else
                echo "   ✗ Module validation failed"
            fi
            
            # Clean up
            rm -f test_simple.ko test_simple.o test_simple.mod* .test_simple.* 2>/dev/null
        else
            echo "   ✗ Module file not created"
            echo "   Build output:"
            cat build_simple.log
        fi
    else
        echo "   ✗ Build failed"
        echo "   Build output:"
        cat build_simple.log
    fi
    
    # Clean up test files
    rm -f test_simple.c Makefile.test_simple build_simple.log
    make -f Makefile.test_simple clean > /dev/null 2>&1 || true
    
    echo ""
}

# Function to diagnose specific issues
diagnose_issues() {
    echo "4. Common Issue Diagnosis"
    
    # Check for SELinux issues
    if command -v getenforce > /dev/null 2>&1; then
        SELINUX_STATUS=$(getenforce 2>/dev/null || echo "Unknown")
        echo "   SELinux status: $SELINUX_STATUS"
        if [[ "$SELINUX_STATUS" == "Enforcing" ]]; then
            echo "   ⚠ SELinux enforcing mode may prevent module loading"
        fi
    fi
    
    # Check disk space
    DISK_SPACE=$(df -h . | tail -1 | awk '{print $4}')
    echo "   Available disk space: $DISK_SPACE"
    
    # Check memory
    echo "   Available memory: $(free -h | grep Mem | awk '{print $7}')"
    
    # Check for common file permission issues
    echo "   Current directory permissions: $(ls -ld . | awk '{print $1}')"
    
    # Check for loaded modules that might conflict
    if lsmod | grep -q "test"; then
        echo "   ⚠ Test modules already loaded:"
        lsmod | grep test
    fi
    
    echo ""
}

# Function to provide recommendations
provide_recommendations() {
    echo "5. Recommendations"
    echo "   Based on the analysis above, try the following:"
    echo ""
    echo "   A. If kernel headers are missing:"
    echo "      sudo yum install kernel-devel kernel-headers"
    echo "      # or for Debian/Ubuntu:"
    echo "      sudo apt-get install linux-headers-\$(uname -r)"
    echo ""
    echo "   B. If build tools are missing:"
    echo "      sudo yum groupinstall 'Development Tools'"
    echo "      # or for Debian/Ubuntu:"
    echo "      sudo apt-get install build-essential"
    echo ""
    echo "   C. If SELinux is blocking:"
    echo "      sudo setenforce 0  # Temporary"
    echo "      # or configure SELinux policies properly"
    echo ""
    echo "   D. If modules won't load:"
    echo "      # Check dmesg for detailed error messages"
    echo "      dmesg | tail -20"
    echo ""
    echo "   E. For Lustre-specific issues:"
    echo "      # Ensure Lustre development packages are installed"
    echo "      # Check Lustre documentation for module development"
    echo ""
}

# Main execution
main() {
    check_kernel_version
    check_build_environment
    test_simple_build
    diagnose_issues
    provide_recommendations
    
    echo "=== Troubleshooting Complete ==="
    echo "If the simple module build test passed, your environment should work."
    echo "If it failed, follow the recommendations above."
}

# Run the analysis
main
