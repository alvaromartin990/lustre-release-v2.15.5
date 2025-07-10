#!/bin/bash

# Detailed compilation diagnostic script
# This script will show exactly what's happening during compilation

echo "=== Detailed Compilation Diagnostic ==="
echo "Date: $(date)"
echo "Directory: $(pwd)"
echo ""

# Check if there are any compilation errors hidden in the build process
echo "1. Verbose build test..."
echo "   Building with maximum verbosity to see compilation errors..."

# Create a test Makefile with verbose output
cat > Makefile.verbose << 'EOF'
obj-m += simple_obd_alloc_test.o

KERNEL_DIR := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules V=1

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean V=1
EOF

echo "   Cleaning first..."
make -f Makefile.verbose clean 2>/dev/null || true

echo ""
echo "   Building with verbose output (V=1)..."
if make -f Makefile.verbose 2>&1 | tee build_verbose.log; then
    echo "   Build process completed"
else
    echo "   Build process failed"
fi

echo ""
echo "2. Analyzing build output..."
echo "   Looking for compilation commands in verbose output..."

# Check if we can see the actual gcc commands
if grep -q "gcc.*simple_obd_alloc_test" build_verbose.log; then
    echo "   ✓ Found compilation command for simple_obd_alloc_test"
    echo "   Compilation command:"
    grep "gcc.*simple_obd_alloc_test" build_verbose.log
else
    echo "   ✗ No compilation command found for simple_obd_alloc_test"
    echo "   This suggests the source file is not being compiled"
fi

# Check for any error messages
if grep -i "error" build_verbose.log; then
    echo "   ✗ Found error messages in build output:"
    grep -i "error" build_verbose.log
else
    echo "   ✓ No explicit error messages found"
fi

# Check for warning messages
if grep -i "warning" build_verbose.log; then
    echo "   ⚠ Found warning messages in build output:"
    grep -i "warning" build_verbose.log
else
    echo "   ✓ No warning messages found"
fi

echo ""
echo "3. Checking build artifacts..."
echo "   Looking for intermediate files..."

# Check for .o files (object files)
if ls -la *.o 2>/dev/null; then
    echo "   ✓ Found object files"
else
    echo "   ✗ No object files found"
fi

# Check for .ko files
if ls -la *.ko 2>/dev/null; then
    echo "   ✓ Found kernel module files"
else
    echo "   ✗ No kernel module files found"
fi

# Check for .mod files
if ls -la *.mod* 2>/dev/null; then
    echo "   ✓ Found module metadata files"
else
    echo "   ✗ No module metadata files found"
fi

echo ""
echo "4. Testing direct compilation..."
echo "   Trying to compile the source file directly..."

# Get the exact gcc command from kernel build system
KERNEL_BUILD="/lib/modules/$(uname -r)/build"
INCLUDES="-I${KERNEL_BUILD}/include -I${KERNEL_BUILD}/arch/x86/include -I${KERNEL_BUILD}/arch/x86/include/generated"
DEFINES="-D__KERNEL__ -DMODULE -DCONFIG_X86_64 -DCONFIG_AS_CFI=1 -DCONFIG_AS_CFI_SIGNAL_FRAME=1 -DCONFIG_AS_CFI_SECTIONS=1 -DCONFIG_AS_FXSAVEQ=1 -DCONFIG_AS_SSSE3=1 -DCONFIG_AS_CRC32=1 -DCONFIG_AS_AVX=1 -DCONFIG_AS_AVX2=1 -DCONFIG_AS_AVX512=1 -DCONFIG_AS_SHA1_NI=1 -DCONFIG_AS_SHA256_NI=1"
CFLAGS="-Wall -Wundef -Wstrict-prototypes -Wno-trigraphs -fno-strict-aliasing -fno-common -fshort-wchar -Werror-implicit-function-declaration -Wno-format-security -std=gnu89 -fno-PIE -mno-sse -mno-mmx -mno-sse2 -mno-3dnow -mno-avx -m64 -falign-jumps=1 -falign-loops=1 -mno-80387 -mno-fp-ret-in-387 -mpreferred-stack-boundary=3 -mskip-rax-setup -mtune=generic -mno-red-zone -mcmodel=kernel -funit-at-a-time -maccumulate-outgoing-args -DCONFIG_X86_X32_ABI -DCONFIG_AS_CFI=1 -DCONFIG_AS_CFI_SIGNAL_FRAME=1 -DCONFIG_AS_CFI_SECTIONS=1 -DCONFIG_AS_FXSAVEQ=1 -DCONFIG_AS_SSSE3=1 -DCONFIG_AS_CRC32=1 -DCONFIG_AS_AVX=1 -DCONFIG_AS_AVX2=1 -DCONFIG_AS_AVX512=1 -DCONFIG_AS_SHA1_NI=1 -DCONFIG_AS_SHA256_NI=1 -pipe -Wno-sign-compare -fno-asynchronous-unwind-tables -mindirect-branch=thunk-extern -mindirect-branch-register -DRETPOLINE -fno-delete-null-pointer-checks -O2 --param=allow-store-data-races=0 -Wframe-larger-than=2048 -fstack-protector-strong -Wno-unused-but-set-variable -fno-omit-frame-pointer -fno-optimize-sibling-calls -fno-var-tracking-assignments -g -pg -mrecord-mcount -mfentry -DCC_USING_FENTRY -Wdeclaration-after-statement -Wno-pointer-sign -fno-strict-overflow -fno-merge-all-constants -fmerge-constants -fno-stack-check -fconserve-stack -Werror=implicit-int -Werror=strict-prototypes -Werror=date-time -Werror=incompatible-pointer-types -Werror=designated-init -DMODULE -DKBUILD_BASENAME='\"simple_obd_alloc_test\"' -DKBUILD_MODNAME='\"simple_obd_alloc_test\"'"

echo "   Attempting direct compilation..."
echo "   Command: gcc $INCLUDES $DEFINES $CFLAGS -c simple_obd_alloc_test.c -o simple_obd_alloc_test.o"

if gcc $INCLUDES $DEFINES $CFLAGS -c simple_obd_alloc_test.c -o simple_obd_alloc_test.o 2>&1 | tee direct_compile.log; then
    echo "   ✓ Direct compilation succeeded"
    if [[ -f "simple_obd_alloc_test.o" ]]; then
        echo "   ✓ Object file created: simple_obd_alloc_test.o"
        ls -la simple_obd_alloc_test.o
    else
        echo "   ✗ Object file not created despite successful compilation"
    fi
else
    echo "   ✗ Direct compilation failed"
    echo "   Compilation errors:"
    cat direct_compile.log
fi

echo ""
echo "5. Source file analysis..."
echo "   Checking source file for issues..."

# Check file size
FILE_SIZE=$(stat -c%s simple_obd_alloc_test.c 2>/dev/null || echo "0")
echo "   File size: $FILE_SIZE bytes"

# Check for basic module structure
echo "   Checking module structure:"
if grep -q "module_init" simple_obd_alloc_test.c; then
    echo "   ✓ module_init found"
else
    echo "   ✗ module_init missing"
fi

if grep -q "module_exit" simple_obd_alloc_test.c; then
    echo "   ✓ module_exit found"
else
    echo "   ✗ module_exit missing"
fi

if grep -q "MODULE_LICENSE" simple_obd_alloc_test.c; then
    echo "   ✓ MODULE_LICENSE found"
else
    echo "   ✗ MODULE_LICENSE missing"
fi

# Check for problematic functions
echo "   Checking for potentially problematic functions:"
if grep -q "get_random_int" simple_obd_alloc_test.c; then
    echo "   ⚠ Uses get_random_int (may not be available in all kernel versions)"
fi

if grep -q "get_random_bytes" simple_obd_alloc_test.c; then
    echo "   ⚠ Uses get_random_bytes (may not be available in all kernel versions)"
fi

if grep -q "vmalloc" simple_obd_alloc_test.c; then
    echo "   ⚠ Uses vmalloc (check if linux/vmalloc.h is included)"
fi

if grep -q "is_vmalloc_addr" simple_obd_alloc_test.c; then
    echo "   ⚠ Uses is_vmalloc_addr (may not be available in all kernel versions)"
fi

echo ""
echo "6. Kernel version specific checks..."
KERNEL_VERSION=$(uname -r)
echo "   Kernel version: $KERNEL_VERSION"

# Check for specific function availability
RANDOM_HEADER="/lib/modules/$KERNEL_VERSION/build/include/linux/random.h"
if [[ -f "$RANDOM_HEADER" ]]; then
    echo "   Checking random.h for available functions:"
    if grep -q "get_random_int" "$RANDOM_HEADER"; then
        echo "   ✓ get_random_int available"
    else
        echo "   ✗ get_random_int not available"
    fi
    
    if grep -q "get_random_bytes" "$RANDOM_HEADER"; then
        echo "   ✓ get_random_bytes available"
    else
        echo "   ✗ get_random_bytes not available"
    fi
else
    echo "   ✗ random.h not found"
fi

echo ""
echo "7. Cleanup..."
rm -f Makefile.verbose build_verbose.log direct_compile.log simple_obd_alloc_test.o 2>/dev/null || true

echo ""
echo "=== Diagnostic Complete ==="
echo "Check the output above for compilation errors or missing functions."
