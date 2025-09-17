# Fixed Makefile for CXL Memory Allocator - Both modules
MODULE_NAME := cxl_kmem_allocator
TEST_MODULE := test_cxl_allocator

KERNEL_VERSION ?= $(shell uname -r)
KERNEL_DIR := /lib/modules/$(KERNEL_VERSION)/build
PWD := $(shell pwd)

# Build both modules
obj-m += $(MODULE_NAME).o
obj-m += $(TEST_MODULE).o

# Compiler flags
ccflags-y += -I$(PWD) -Wall

.PHONY: all module clean load unload reload status test help

all: module

module:
	@echo "Building CXL allocator modules..."
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules
	@echo "Build complete:"
	@ls -la *.ko 2>/dev/null || echo "No .ko files generated"

clean:
	@echo "Cleaning..."
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	rm -f *.o *.ko *.mod.c *.mod.o *.symvers *.order
	rm -f .*.cmd *.mod modules.order Module.symvers
	rm -rf .tmp_versions/

load: module
	@echo "Fixing DAX device permissions..."
	@sudo chmod 666 /dev/dax0.0
	@echo "Loading CXL allocator module..."
	@if lsmod | grep -q $(MODULE_NAME); then \
		echo "Unloading existing modules..."; \
		sudo rmmod $(TEST_MODULE) 2>/dev/null || true; \
		sudo rmmod $(MODULE_NAME); \
	fi
	sudo insmod $(MODULE_NAME).ko \
		cxl_dax_device="/dev/dax0.0" \
		enable_fallback=1 \
		pool_size_mb=64
	@echo "Main module loaded!"

load-test: load
	@if [ -f "$(TEST_MODULE).ko" ]; then \
		echo "Loading test module..."; \
		sudo insmod $(TEST_MODULE).ko; \
		echo "Test module loaded - check dmesg for test results!"; \
	else \
		echo "Test module $(TEST_MODULE).ko not found - build failed"; \
	fi

unload:
	@echo "Unloading modules..."
	@sudo rmmod $(TEST_MODULE) 2>/dev/null || true
	@sudo rmmod $(MODULE_NAME) 2>/dev/null || true
	@echo "Modules unloaded"

reload: unload load-test

status:
	@echo "=== Module Status ==="
	@lsmod | grep -E "($(MODULE_NAME)|$(TEST_MODULE))" || echo "No CXL modules loaded"
	@echo ""
	@echo "=== Recent CXL Messages ==="
	@dmesg | grep CXL | tail -15 || echo "No CXL messages"
	@echo ""
	@echo "=== DAX Device ==="
	@ls -la /dev/dax0.0

test: load-test status

help:
	@echo "CXL Allocator Build & Test"
	@echo "=========================="
	@echo "  module     - Build both main and test modules"
	@echo "  load       - Load main CXL allocator module"
	@echo "  load-test  - Load both modules and run tests"
	@echo "  unload     - Unload all modules"
	@echo "  reload     - Full reload with tests"
	@echo "  status     - Show current status"
	@echo "  test       - Quick test (load and status)"
	@echo "  clean      - Clean build files"