#!/bin/bash

# Script to fix kernel configuration issues for module building
# This addresses the "Kernel configuration is invalid" error

echo "=== Kernel Configuration Fix ==="
echo "Date: $(date)"
echo "System: $(uname -a)"
echo "User: $(whoami)"
echo ""

# Get kernel information
KERNEL_VERSION=$(uname -r)
KERNEL_BUILD="/lib/modules/$KERNEL_VERSION/build"
KERNEL_SOURCE="/usr/src/kernels/$KERNEL_VERSION"

echo "1. Checking kernel build environment..."
echo "   Kernel version: $KERNEL_VERSION"
echo "   Kernel build dir: $KERNEL_BUILD"
echo "   Kernel source dir: $KERNEL_SOURCE"
echo ""

# Check if kernel build directory exists
if [[ ! -d "$KERNEL_BUILD" ]]; then
    echo "   ✗ Kernel build directory not found: $KERNEL_BUILD"
    echo "   Please install kernel-devel package"
    exit 1
else
    echo "   ✓ Kernel build directory exists"
fi

# Check if kernel source directory exists
if [[ ! -d "$KERNEL_SOURCE" ]]; then
    echo "   ✗ Kernel source directory not found: $KERNEL_SOURCE"
    echo "   Please install kernel-devel package"
    exit 1
else
    echo "   ✓ Kernel source directory exists"
fi

echo ""
echo "2. Checking kernel configuration files..."

# Check for missing configuration files
AUTOCONF_H="$KERNEL_BUILD/include/generated/autoconf.h"
AUTO_CONF="$KERNEL_BUILD/include/config/auto.conf"

if [[ -f "$AUTOCONF_H" ]]; then
    echo "   ✓ autoconf.h exists"
else
    echo "   ✗ autoconf.h missing: $AUTOCONF_H"
fi

if [[ -f "$AUTO_CONF" ]]; then
    echo "   ✓ auto.conf exists"
else
    echo "   ✗ auto.conf missing: $AUTO_CONF"
fi

# Check if we can fix the configuration
echo ""
echo "3. Attempting to fix kernel configuration..."

# First, check if we have write permissions to the kernel source
if [[ -w "$KERNEL_SOURCE" ]]; then
    echo "   ✓ Have write permissions to kernel source"
    
    # Try to fix the configuration
    echo "   Running 'make oldconfig && make prepare' in kernel source..."
    cd "$KERNEL_SOURCE"
    
    if make oldconfig && make prepare; then
        echo "   ✓ Kernel configuration fixed successfully"
        
        # Verify the fix
        if [[ -f "$AUTOCONF_H" ]] && [[ -f "$AUTO_CONF" ]]; then
            echo "   ✓ Configuration files now exist"
        else
            echo "   ✗ Configuration files still missing after fix attempt"
        fi
    else
        echo "   ✗ Failed to fix kernel configuration"
        echo "   You may need to rebuild the kernel or install proper kernel-devel"
    fi
    
else
    echo "   ✗ No write permissions to kernel source directory"
    echo "   Cannot fix configuration automatically"
    echo ""
    echo "   Manual fix required:"
    echo "   1. cd $KERNEL_SOURCE"
    echo "   2. sudo make oldconfig"
    echo "   3. sudo make prepare"
fi

echo ""
echo "4. Alternative solutions..."

# Check if there's a working kernel config in /boot
BOOT_CONFIG="/boot/config-$KERNEL_VERSION"
if [[ -f "$BOOT_CONFIG" ]]; then
    echo "   ✓ Found kernel config in /boot: $BOOT_CONFIG"
    
    if [[ -w "$KERNEL_SOURCE" ]]; then
        echo "   Copying config to kernel source..."
        cp "$BOOT_CONFIG" "$KERNEL_SOURCE/.config"
        
        cd "$KERNEL_SOURCE"
        if make oldconfig && make prepare; then
            echo "   ✓ Successfully configured kernel using /boot config"
        else
            echo "   ✗ Failed to configure kernel using /boot config"
        fi
    fi
else
    echo "   ✗ No kernel config found in /boot"
fi

echo ""
echo "5. Testing if module compilation works now..."

# Create a simple test module
cat > test_config_fix.c << 'EOF'
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Test module after config fix");
MODULE_VERSION("1.0");

static int __init test_config_fix_init(void)
{
    printk(KERN_INFO "test_config_fix: Module loaded successfully\n");
    return 0;
}

static void __exit test_config_fix_exit(void)
{
    printk(KERN_INFO "test_config_fix: Module unloaded\n");
}

module_init(test_config_fix_init);
module_exit(test_config_fix_exit);
EOF

cat > Makefile.config_fix << 'EOF'
obj-m += test_config_fix.o
KERNEL_DIR := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
EOF

# Go back to original directory
cd - > /dev/null 2>&1 || cd /home/cc/lustre-release-v2.15.5

echo "   Building test module..."
make -f Makefile.config_fix clean 2>/dev/null || true

if make -f Makefile.config_fix 2>&1 | tee config_fix_build.log; then
    if [[ -f "test_config_fix.ko" ]]; then
        echo "   ✓ SUCCESS: Module compilation now works!"
        echo "   ✓ Configuration issue has been resolved"
        
        # Clean up test files
        rm -f test_config_fix.* Makefile.config_fix config_fix_build.log
        
        echo ""
        echo "   You can now run your original tests:"
        echo "   sudo ./test_ultra_version.sh"
    else
        echo "   ✗ Module compilation still fails"
        echo "   Build log:"
        cat config_fix_build.log
    fi
else
    echo "   ✗ Module compilation still fails"
    echo "   Build log:"
    cat config_fix_build.log
fi

echo ""
echo "=== Kernel Configuration Fix Complete ==="

if [[ -f "test_config_fix.ko" ]]; then
    echo "SUCCESS: Your kernel is now properly configured for module building!"
else
    echo "The configuration issue may require manual intervention."
    echo "Please contact your system administrator or:"
    echo "1. Reinstall kernel-devel package"
    echo "2. Run: sudo yum reinstall kernel-devel-$KERNEL_VERSION"
    echo "3. Or rebuild the kernel configuration manually"
fi
