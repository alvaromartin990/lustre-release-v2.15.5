#!/bin/bash

# Ultimate diagnostic script to identify the module build issue
# This script will show exactly what's happening during compilation

echo "=== Ultimate Module Build Diagnostic ==="
echo "Date: $(date)"
echo "System: $(uname -a)"
echo "Directory: $(pwd)"
echo "User: $(whoami)"
echo ""

# Function to check if a function exists in kernel headers
check_kernel_function() {
    local func_name="$1"
    local header_file="$2"
    local kernel_build="/lib/modules/$(uname -r)/build"
    
    if [[ -f "$kernel_build/include/linux/$header_file" ]]; then
        if grep -q "$func_name" "$kernel_build/include/linux/$header_file"; then
            echo "   ✓ $func_name found in $header_file"
        else
            echo "   ✗ $func_name NOT found in $header_file"
        fi
    else
        echo "   ✗ Header file $header_file not found"
    fi
}

# 1. Create absolute minimal test module
echo "1. Creating and testing absolute minimal module..."
cat > ultra_minimal.c << 'EOF'
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Ultra minimal test");
MODULE_VERSION("1.0");

static int __init ultra_minimal_init(void)
{
    printk(KERN_INFO "ultra_minimal: Hello from kernel module\n");
    return 0;
}

static void __exit ultra_minimal_exit(void)
{
    printk(KERN_INFO "ultra_minimal: Goodbye from kernel module\n");
}

module_init(ultra_minimal_init);
module_exit(ultra_minimal_exit);
EOF

cat > Makefile.ultra_minimal << 'EOF'
obj-m += ultra_minimal.o
KERNEL_DIR := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules V=1

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
EOF

echo "   Building ultra minimal module with verbose output..."
make -f Makefile.ultra_minimal clean 2>/dev/null || true
if make -f Makefile.ultra_minimal 2>&1 | tee ultra_minimal_build.log; then
    if [[ -f "ultra_minimal.ko" ]]; then
        echo "   ✓ Ultra minimal module built successfully"
        echo "   ✓ Kernel module compilation works"
        rm -f ultra_minimal.ko ultra_minimal.o ultra_minimal.mod* .ultra_minimal.* 2>/dev/null || true
    else
        echo "   ✗ Ultra minimal module build succeeded but no .ko file"
        echo "   This indicates a fundamental kernel build system issue"
        echo "   Build log:"
        cat ultra_minimal_build.log
        exit 1
    fi
else
    echo "   ✗ Ultra minimal module build failed"
    echo "   Build log:"
    cat ultra_minimal_build.log
    exit 1
fi

# 2. Test step-by-step compilation
echo ""
echo "2. Testing step-by-step compilation..."
echo "   Checking kernel API availability..."

check_kernel_function "printk" "printk.h"
check_kernel_function "kmalloc" "slab.h"
check_kernel_function "vmalloc" "vmalloc.h"
check_kernel_function "ktime_get" "ktime.h"

# 3. Create a version that tests problematic functions one by one
echo ""
echo "3. Testing problematic functions..."

# Test kmalloc/vmalloc
cat > test_alloc.c << 'EOF'
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Test allocation functions");
MODULE_VERSION("1.0");

static int __init test_alloc_init(void)
{
    void *ptr1, *ptr2;
    
    printk(KERN_INFO "test_alloc: Testing kmalloc...\n");
    ptr1 = kmalloc(1024, GFP_KERNEL);
    if (ptr1) {
        printk(KERN_INFO "test_alloc: kmalloc succeeded\n");
        kfree(ptr1);
    } else {
        printk(KERN_ERR "test_alloc: kmalloc failed\n");
    }
    
    printk(KERN_INFO "test_alloc: Testing vmalloc...\n");
    ptr2 = vmalloc(1024);
    if (ptr2) {
        printk(KERN_INFO "test_alloc: vmalloc succeeded\n");
        vfree(ptr2);
    } else {
        printk(KERN_ERR "test_alloc: vmalloc failed\n");
    }
    
    return 0;
}

static void __exit test_alloc_exit(void)
{
    printk(KERN_INFO "test_alloc: Module unloaded\n");
}

module_init(test_alloc_init);
module_exit(test_alloc_exit);
EOF

cat > Makefile.test_alloc << 'EOF'
obj-m += test_alloc.o
KERNEL_DIR := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules V=1

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
EOF

echo "   Testing allocation functions..."
make -f Makefile.test_alloc clean 2>/dev/null || true
if make -f Makefile.test_alloc 2>&1 | tee test_alloc_build.log; then
    if [[ -f "test_alloc.ko" ]]; then
        echo "   ✓ Allocation test module built successfully"
        rm -f test_alloc.ko test_alloc.o test_alloc.mod* .test_alloc.* 2>/dev/null || true
    else
        echo "   ✗ Allocation test module build succeeded but no .ko file"
        echo "   Build log:"
        cat test_alloc_build.log
    fi
else
    echo "   ✗ Allocation test module build failed"
    echo "   Build log:"
    cat test_alloc_build.log
fi

# 4. Test is_vmalloc_addr function specifically
echo ""
echo "4. Testing is_vmalloc_addr function..."

cat > test_vmalloc_addr.c << 'EOF'
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/vmalloc.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Test is_vmalloc_addr function");
MODULE_VERSION("1.0");

static int __init test_vmalloc_addr_init(void)
{
    void *ptr;
    
    printk(KERN_INFO "test_vmalloc_addr: Testing vmalloc and is_vmalloc_addr...\n");
    ptr = vmalloc(1024);
    if (ptr) {
        if (is_vmalloc_addr(ptr)) {
            printk(KERN_INFO "test_vmalloc_addr: is_vmalloc_addr works correctly\n");
        } else {
            printk(KERN_ERR "test_vmalloc_addr: is_vmalloc_addr failed\n");
        }
        vfree(ptr);
    } else {
        printk(KERN_ERR "test_vmalloc_addr: vmalloc failed\n");
    }
    
    return 0;
}

static void __exit test_vmalloc_addr_exit(void)
{
    printk(KERN_INFO "test_vmalloc_addr: Module unloaded\n");
}

module_init(test_vmalloc_addr_init);
module_exit(test_vmalloc_addr_exit);
EOF

cat > Makefile.test_vmalloc_addr << 'EOF'
obj-m += test_vmalloc_addr.o
KERNEL_DIR := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules V=1

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
EOF

echo "   Testing is_vmalloc_addr function..."
make -f Makefile.test_vmalloc_addr clean 2>/dev/null || true
if make -f Makefile.test_vmalloc_addr 2>&1 | tee test_vmalloc_addr_build.log; then
    if [[ -f "test_vmalloc_addr.ko" ]]; then
        echo "   ✓ is_vmalloc_addr test module built successfully"
        rm -f test_vmalloc_addr.ko test_vmalloc_addr.o test_vmalloc_addr.mod* .test_vmalloc_addr.* 2>/dev/null || true
    else
        echo "   ✗ is_vmalloc_addr test failed - this is likely the problem!"
        echo "   Build log:"
        cat test_vmalloc_addr_build.log
        echo ""
        echo "   DIAGNOSIS: is_vmalloc_addr() function is not available"
        echo "   This explains why the module builds but no .ko file is created"
    fi
else
    echo "   ✗ is_vmalloc_addr test build failed"
    echo "   Build log:"
    cat test_vmalloc_addr_build.log
fi

# 5. Test ktime functions
echo ""
echo "5. Testing ktime functions..."

cat > test_ktime.c << 'EOF'
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ktime.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Test ktime functions");
MODULE_VERSION("1.0");

static int __init test_ktime_init(void)
{
    ktime_t start, end;
    s64 delta;
    
    printk(KERN_INFO "test_ktime: Testing ktime functions...\n");
    start = ktime_get();
    end = ktime_get();
    delta = ktime_to_ns(ktime_sub(end, start));
    printk(KERN_INFO "test_ktime: ktime test completed, delta = %lld ns\n", delta);
    
    return 0;
}

static void __exit test_ktime_exit(void)
{
    printk(KERN_INFO "test_ktime: Module unloaded\n");
}

module_init(test_ktime_init);
module_exit(test_ktime_exit);
EOF

cat > Makefile.test_ktime << 'EOF'
obj-m += test_ktime.o
KERNEL_DIR := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules V=1

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
EOF

echo "   Testing ktime functions..."
make -f Makefile.test_ktime clean 2>/dev/null || true
if make -f Makefile.test_ktime 2>&1 | tee test_ktime_build.log; then
    if [[ -f "test_ktime.ko" ]]; then
        echo "   ✓ ktime test module built successfully"
        rm -f test_ktime.ko test_ktime.o test_ktime.mod* .test_ktime.* 2>/dev/null || true
    else
        echo "   ✗ ktime test failed"
        echo "   Build log:"
        cat test_ktime_build.log
    fi
else
    echo "   ✗ ktime test build failed"
    echo "   Build log:"
    cat test_ktime_build.log
fi

# 6. Final analysis
echo ""
echo "6. Final Analysis and Recommendations..."

# Check which functions are actually available
KERNEL_BUILD="/lib/modules/$(uname -r)/build"
echo "   Checking kernel version specific issues..."
echo "   Kernel version: $(uname -r)"

# Check specific header files
if [[ -f "$KERNEL_BUILD/include/linux/vmalloc.h" ]]; then
    echo "   ✓ vmalloc.h found"
    if grep -q "is_vmalloc_addr" "$KERNEL_BUILD/include/linux/vmalloc.h"; then
        echo "   ✓ is_vmalloc_addr declared in vmalloc.h"
    else
        echo "   ✗ is_vmalloc_addr NOT declared in vmalloc.h"
        echo "   This is likely causing the build failure"
    fi
else
    echo "   ✗ vmalloc.h not found"
fi

# Clean up test files
echo ""
echo "7. Cleaning up test files..."
rm -f ultra_minimal.c ultra_minimal.ko ultra_minimal.o ultra_minimal.mod* .ultra_minimal.* 2>/dev/null || true
rm -f test_alloc.c test_alloc.ko test_alloc.o test_alloc.mod* .test_alloc.* 2>/dev/null || true
rm -f test_vmalloc_addr.c test_vmalloc_addr.ko test_vmalloc_addr.o test_vmalloc_addr.mod* .test_vmalloc_addr.* 2>/dev/null || true
rm -f test_ktime.c test_ktime.ko test_ktime.o test_ktime.mod* .test_ktime.* 2>/dev/null || true
rm -f Makefile.ultra_minimal Makefile.test_alloc Makefile.test_vmalloc_addr Makefile.test_ktime 2>/dev/null || true
rm -f *_build.log 2>/dev/null || true

echo ""
echo "=== Ultimate Diagnostic Complete ==="
echo "Check the output above to identify which specific function is causing the issue."
