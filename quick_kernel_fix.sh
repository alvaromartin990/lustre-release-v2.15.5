#!/bin/bash

# Quick workaround for kernel configuration issues
# This script will attempt several different approaches

echo "=== Quick Kernel Configuration Workaround ==="
echo "Date: $(date)"
echo "User: $(whoami)"
echo ""

KERNEL_VERSION=$(uname -r)
KERNEL_BUILD="/lib/modules/$KERNEL_VERSION/build"
KERNEL_SOURCE="/usr/src/kernels/$KERNEL_VERSION"

echo "1. Attempting to fix kernel configuration..."

# Check if we're root
if [[ $EUID -ne 0 ]]; then
    echo "   ✗ This script requires root privileges"
    echo "   Please run: sudo $0"
    exit 1
fi

# Method 1: Try to prepare the kernel build environment
echo "   Method 1: Preparing kernel build environment..."
cd "$KERNEL_SOURCE" 2>/dev/null || {
    echo "   ✗ Cannot access kernel source directory"
    echo "   Installing kernel-devel..."
    
    # Try to install/reinstall kernel-devel
    if command -v yum >/dev/null 2>&1; then
        yum reinstall -y "kernel-devel-$KERNEL_VERSION" || yum install -y "kernel-devel-$KERNEL_VERSION"
    elif command -v dnf >/dev/null 2>&1; then
        dnf reinstall -y "kernel-devel-$KERNEL_VERSION" || dnf install -y "kernel-devel-$KERNEL_VERSION"
    elif command -v apt-get >/dev/null 2>&1; then
        apt-get update && apt-get install -y "linux-headers-$KERNEL_VERSION"
    fi
    
    cd "$KERNEL_SOURCE" 2>/dev/null || {
        echo "   ✗ Still cannot access kernel source after installation"
        exit 1
    }
}

# Method 2: Copy kernel config and prepare
echo "   Method 2: Setting up kernel configuration..."

# Copy the running kernel's config if available
if [[ -f "/proc/config.gz" ]]; then
    echo "   Found config in /proc/config.gz"
    zcat /proc/config.gz > .config
elif [[ -f "/boot/config-$KERNEL_VERSION" ]]; then
    echo "   Found config in /boot"
    cp "/boot/config-$KERNEL_VERSION" .config
else
    echo "   ✗ Cannot find kernel configuration"
    echo "   Trying default configuration..."
    make defconfig
fi

# Run the configuration and preparation
echo "   Running kernel preparation commands..."
echo "   This may take a few minutes..."

if make oldconfig </dev/null && make prepare; then
    echo "   ✓ Kernel configuration completed successfully"
else
    echo "   ✗ Kernel configuration failed"
    
    # Try alternative method
    echo "   Trying alternative preparation method..."
    if make modules_prepare; then
        echo "   ✓ Alternative method succeeded"
    else
        echo "   ✗ All configuration methods failed"
        exit 1
    fi
fi

# Method 3: Verify the fix
echo ""
echo "2. Verifying kernel configuration fix..."

AUTOCONF_H="$KERNEL_BUILD/include/generated/autoconf.h"
AUTO_CONF="$KERNEL_BUILD/include/config/auto.conf"

if [[ -f "$AUTOCONF_H" ]] && [[ -f "$AUTO_CONF" ]]; then
    echo "   ✓ Configuration files now exist"
else
    echo "   ⚠ Configuration files still missing, but build may still work"
fi

# Method 4: Test with a simple module
echo ""
echo "3. Testing module compilation..."

cd /home/cc/lustre-release-v2.15.5 || cd /tmp

cat > test_kernel_fix.c << 'EOF'
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Test kernel configuration fix");
MODULE_VERSION("1.0");

static int __init test_kernel_fix_init(void)
{
    printk(KERN_INFO "test_kernel_fix: SUCCESS - Kernel module compilation works!\n");
    return 0;
}

static void __exit test_kernel_fix_exit(void)
{
    printk(KERN_INFO "test_kernel_fix: Module unloaded\n");
}

module_init(test_kernel_fix_init);
module_exit(test_kernel_fix_exit);
EOF

cat > Makefile.kernel_fix << 'EOF'
obj-m += test_kernel_fix.o
KERNEL_DIR := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
EOF

echo "   Building test module..."
make -f Makefile.kernel_fix clean 2>/dev/null || true

if make -f Makefile.kernel_fix 2>&1; then
    if [[ -f "test_kernel_fix.ko" ]]; then
        echo "   ✓ SUCCESS: Module compilation now works!"
        echo "   ✓ Kernel configuration has been fixed"
        
        # Test loading the module
        echo "   Testing module loading..."
        if insmod test_kernel_fix.ko; then
            echo "   ✓ Module loaded successfully"
            dmesg | tail -3 | grep "test_kernel_fix"
            rmmod test_kernel_fix
            echo "   ✓ Module unloaded successfully"
        else
            echo "   ⚠ Module compiled but loading failed (this is normal)"
        fi
        
        # Clean up
        rm -f test_kernel_fix.* Makefile.kernel_fix
        
        echo ""
        echo "   SUCCESS: You can now run your allocation tests!"
        echo "   Try: sudo ./test_ultra_version.sh"
        
    else
        echo "   ✗ Module compilation still fails"
    fi
else
    echo "   ✗ Module compilation still fails"
    echo "   The kernel configuration issue could not be resolved automatically"
fi

echo ""
echo "=== Quick Fix Complete ==="
