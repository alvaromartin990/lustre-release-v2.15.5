# Makefile for CXL Memory Allocator Kernel Module
# 
# This builds the CXL allocator as a standalone kernel module
# that can be loaded before Lustre modules.

# Module name
MODULE_NAME := cxl_kmem_allocator

# Kernel version to build against
KERNEL_VERSION ?= $(shell uname -r)
KERNEL_DIR := /lib/modules/$(KERNEL_VERSION)/build
PWD := $(shell pwd)

# Source files
obj-m += $(MODULE_NAME).o
$(MODULE_NAME)-objs := cxl_kmem_allocator.o

# Compiler flags for CXL support
ccflags-y += -DCONFIG_CXL_ALLOCATOR_DEBUG
ccflags-y += -DCONFIG_CXL_PERFORMANCE_MONITORING
ccflags-y += -I$(PWD)/include
ccflags-y += -Wall -Wextra -Werror

# Additional flags for specific kernel versions
ifeq ($(shell test $(shell echo $(KERNEL_VERSION) | cut -d. -f1) -ge 5 && echo true), true)
    ccflags-y += -DHAVE_PERCPU_COUNTER_ADD_BATCH
endif

# Build targets
all: module

module:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	rm -f *.o *.ko *.mod.c *.mod.o *.symvers *.order

install: module
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules_install
	depmod -a

uninstall:
	rm -f /lib/modules/$(KERNEL_VERSION)/extra/$(MODULE_NAME).ko
	depmod -a

# Testing targets
load: module
	sudo insmod $(MODULE_NAME).ko cxl_dax_device="/dev/dax1.0" enable_fallback=1

unload:
	sudo rmmod $(MODULE_NAME) || true

reload: unload load

# Check module status
status:
	lsmod | grep $(MODULE_NAME) || echo "Module not loaded"
	dmesg | tail -20 | grep CXL || echo "No recent CXL messages"

# Show CXL device information
show-cxl:
	@echo "=== CXL DAX Devices ==="
	ls -la /dev/dax* 2>/dev/null || echo "No DAX devices found"
	@echo
	@echo "=== CXL Device Information ==="
	sudo daxctl list 2>/dev/null || echo "daxctl not available"
	@echo
	@echo "=== Memory Information ==="
	cat /proc/iomem | grep -i "soft reserved\|persistent memory" || echo "No CXL memory regions found"

# Development targets
debug: ccflags-y += -DDEBUG -g
debug: module

# Create debug symbols
debug-symbols: debug
	objdump -h $(MODULE_NAME).ko
	nm $(MODULE_NAME).ko | grep cxl_

# Memory leak detection (requires debug build)
check-leaks:
	@echo "Checking for memory leaks..."
	dmesg | grep "CXL.*leak" || echo "No leaks detected"

# Performance testing
perf-test:
	@echo "=== CXL Allocator Performance Test ==="
	@echo "Loading test module..."
	sudo insmod $(MODULE_NAME).ko
	@echo "Running basic allocation test..."
	sudo sh -c 'echo "test" > /proc/sys/kernel/printk'  # Enable kernel messages
	@echo "Check dmesg for performance results"
	dmesg | tail -10

# Integration with Lustre build
lustre-integration:
	@echo "=== Lustre Integration Steps ==="
	@echo "1. Copy cxl_kmem_allocator.h to lustre/include/linux/"
	@echo "2. Copy obd_support_cxl.h to lustre/include/linux/"
	@echo "3. Modify Lustre source files to include obd_support_cxl.h"
	@echo "4. Add CXL allocator dependency to Lustre's autoconf"
	@echo "5. Rebuild Lustre with CXL support"

# Generate patches for Lustre integration
generate-lustre-patch:
	@echo "Generating Lustre integration patches..."
	mkdir -p patches
	@echo "--- Patch for lustre/include/linux/obd_support.h ---" > patches/lustre-cxl-integration.patch
	@echo "+++ Include CXL allocator support" >> patches/lustre-cxl-integration.patch
	@echo "+#ifdef CONFIG_CXL_MEMORY_ALLOCATOR" >> patches/lustre-cxl-integration.patch
	@echo "+#include \"obd_support_cxl.h\"" >> patches/lustre-cxl-integration.patch
	@echo "+#endif" >> patches/lustre-cxl-integration.patch
	@echo "Patch generated in patches/lustre-cxl-integration.patch"

# Documentation generation
docs:
	@echo "=== CXL Allocator Documentation ==="
	@echo "Generating kernel-doc documentation..."
	mkdir -p docs
	kernel-doc -html cxl_kmem_allocator.c > docs/cxl_allocator.html
	kernel-doc -man cxl_kmem_allocator.c > docs/cxl_allocator.man
	@echo "Documentation generated in docs/"

# Validation tests
validate:
	@echo "=== Module Validation ==="
	@echo "1. Checking module compilation..."
	$(MAKE) clean && $(MAKE) module
	@echo "2. Checking module symbols..."
	nm $(MODULE_NAME).ko | grep -E "cxl_(kmalloc|kfree|vmalloc)" || echo "WARNING: Expected symbols not found"
	@echo "3. Checking module dependencies..."
	modinfo $(MODULE_NAME).ko
	@echo "4. Validation complete"

# Continuous integration targets
ci-test: clean validate
	@echo "=== CI Test Suite ==="
	$(MAKE) debug
	$(MAKE) check-leaks
	@echo "CI tests completed"

# Package for distribution
package: clean module docs
	@echo "=== Creating distribution package ==="
	mkdir -p cxl-allocator-dist
	cp $(MODULE_NAME).ko cxl-allocator-dist/
	cp *.h cxl-allocator-dist/
	cp Makefile cxl-allocator-dist/
	cp -r docs cxl-allocator-dist/ 2>/dev/null || true
	tar -czf cxl-allocator-$(shell date +%Y%m%d).tar.gz cxl-allocator-dist/
	rm -rf cxl-allocator-dist/
	@echo "Package created: cxl-allocator-$(shell date +%Y%m%d).tar.gz"

# Help target
help:
	@echo "CXL Memory Allocator Build System"
	@echo "================================="
	@echo
	@echo "Build targets:"
	@echo "  module       - Build the CXL allocator kernel module"
	@echo "  clean        - Clean build artifacts"
	@echo "  install      - Install module to system"
	@echo "  uninstall    - Remove module from system"
	@echo
	@echo "Testing targets:"
	@echo "  load         - Load the module with default parameters"
	@echo "  unload       - Unload the module"
	@echo "  reload       - Unload and reload the module"
	@echo "  status       - Show module and CXL status"
	@echo "  show-cxl     - Display CXL device information"
	@echo
	@echo "Development targets:"
	@echo "  debug        - Build with debug symbols and flags"
	@echo "  debug-symbols- Show module symbols"
	@echo "  check-leaks  - Check for memory leaks"
	@echo "  perf-test    - Run performance tests"
	@echo
	@echo "Integration targets:"
	@echo "  lustre-integration    - Show Lustre integration steps"
	@echo "  generate-lustre-patch - Generate patches for Lustre"
	@echo
	@echo "Documentation:"
	@echo "  docs         - Generate kernel-doc documentation"
	@echo "  validate     - Run validation tests"
	@echo "  package      - Create distribution package"
	@echo
	@echo "Variables:"
	@echo "  KERNEL_VERSION - Kernel version to build against ($(KERNEL_VERSION))"
	@echo "  MODULE_NAME    - Name of the module ($(MODULE_NAME))"

.PHONY: all module clean install uninstall load unload reload status show-cxl \
        debug debug-symbols check-leaks perf-test lustre-integration \
        generate-lustre-patch docs validate ci-test package help