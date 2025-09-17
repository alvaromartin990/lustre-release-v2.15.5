# Improved Makefile for CXL Memory Allocator
MODULE_NAME := cxl_kmem_allocator
TEST_MODULE := test_cxl_allocator

KERNEL_VERSION ?= $(shell uname -r)
KERNEL_DIR := /lib/modules/$(KERNEL_VERSION)/build
PWD := $(shell pwd)

# Source files
MAIN_SOURCE := cxl_kmem_allocator_simple.c
TEST_SOURCE := test_cxl_allocator.c

# Module objects - this tells kbuild which files to compile
obj-m += $(MODULE_NAME).o
obj-m += $(TEST_MODULE).o

# Tell kbuild that our .ko file should be built from the simple.c file
$(MODULE_NAME)-objs := cxl_kmem_allocator_simple.o

# Compiler flags
ccflags-y += -I$(PWD) -Wall -Wextra
ccflags-y += -DDEBUG  # Enable debug messages

# Check if we're building for the right kernel
CURRENT_KERNEL := $(shell uname -r)
TARGET_KERNEL := $(KERNEL_VERSION)

.PHONY: all module clean load unload reload status test help check-kernel

all: check-kernel module

check-kernel:
	@echo "Building for kernel: $(TARGET_KERNEL)"
	@echo "Current kernel: $(CURRENT_KERNEL)"
	@if [ "$(TARGET_KERNEL)" != "$(CURRENT_KERNEL)" ]; then \
		echo "WARNING: Building for different kernel version"; \
	fi
	@if [ ! -d "$(KERNEL_DIR)" ]; then \
		echo "ERROR: Kernel headers not found at $(KERNEL_DIR)"; \
		echo "Install with: sudo apt-get install linux-headers-$(KERNEL_VERSION)"; \
		exit 1; \
	fi

module: check-kernel
	@echo "Building CXL allocator module..."
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules
	@echo "Build complete:"
	@ls -la *.ko 2>/dev/null || echo "No .ko files generated"

# Build only the main module
main-only: check-kernel
	@echo "Building main module only..."
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) $(MODULE_NAME).ko

# Build only the test module
test-only: check-kernel $(MODULE_NAME).ko
	@echo "Building test module only..."
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) $(TEST_MODULE).ko

clean:
	@echo "Cleaning build artifacts..."
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	rm -f *.o *.ko *.mod.c *.mod.o *.symvers *.order
	rm -f .*.cmd *.mod modules.order Module.symvers
	rm -rf .tmp_versions/
	rm -f cxl_test  # Clean user-space test binary too

# Module management
load: module
	@echo "Loading CXL allocator module..."
	@if lsmod | grep -q $(MODULE_NAME); then \
		echo "Module already loaded, unloading first..."; \
		$(MAKE) unload; \
	fi
	sudo insmod $(MODULE_NAME).ko \
		cxl_dax_device="/dev/dax0.0" \
		enable_fallback=1 \
		pool_size_mb=64
	@echo "Module loaded successfully"
	@$(MAKE) status

load-test: load
	@echo "Loading test module..."
	@if [ -f "$(TEST_MODULE).ko" ]; then \
		if lsmod | grep -q $(TEST_MODULE); then \
			sudo rmmod $(TEST_MODULE); \
		fi; \
		sudo insmod $(TEST_MODULE).ko; \
		echo "Test module loaded"; \
	else \
		echo "Test module not found, run 'make module' first"; \
	fi

unload:
	@echo "Unloading modules..."
	@sudo rmmod $(TEST_MODULE) 2>/dev/null || true
	@sudo rmmod $(MODULE_NAME) 2>/dev/null || true
	@echo "Modules unloaded"

reload: unload load

status:
	@echo "=== Module Status ==="
	@lsmod | grep -E "($(MODULE_NAME)|$(TEST_MODULE))" || echo "No CXL modules loaded"
	@echo ""
	@echo "=== Recent kernel messages ==="
	@dmesg | grep CXL | tail -10 || echo "No recent CXL messages"
	@echo ""
	@echo "=== DAX device status ==="
	@ls -la /dev/dax* 2>/dev/null || echo "No DAX devices found"

# Testing targets
test-userspace: 
	@echo "Building and testing user-space CXL access..."
	@if [ -f "cxl_test.c" ]; then \
		gcc -o cxl_test cxl_test.c -Wall; \
		echo "Running user-space test (requires sudo):"; \
		sudo ./cxl_test; \
	else \
		echo "cxl_test.c not found"; \
	fi

test-complete: module
	@echo "Running complete test suite..."
	@if [ -f "scripts/test_complete.sh" ]; then \
		chmod +x scripts/test_complete.sh; \
		./scripts/test_complete.sh; \
	else \
		echo "Complete test script not found"; \
		$(MAKE) test-userspace; \
		$(MAKE) load-test; \
		$(MAKE) status; \
	fi

# Debugging helpers
modinfo: module
	@echo "=== Main Module Information ==="
	@modinfo $(MODULE_NAME).ko
	@if [ -f "$(TEST_MODULE).ko" ]; then \
		echo ""; \
		echo "=== Test Module Information ==="; \
		modinfo $(TEST_MODULE).ko; \
	fi

debug-load: module
	@echo "Loading module with debug info..."
	@echo "8" | sudo tee /proc/sys/kernel/printk > /dev/null
	$(MAKE) load
	@echo "Check dmesg for detailed debug output"

# Development helpers
format:
	@echo "Formatting source code..."
	@if command -v clang-format >/dev/null 2>&1; then \
		clang-format -i *.c *.h; \
		echo "Code formatted"; \
	else \
		echo "clang-format not available"; \
	fi

check-syntax: 
	@echo "Checking syntax..."
	@gcc -fsyntax-only -I$(KERNEL_DIR)/include $(MAIN_SOURCE) && \
		echo "Main module syntax: OK" || echo "Main module syntax: ERROR"
	@if [ -f "$(TEST_SOURCE)" ]; then \
		gcc -fsyntax-only -I$(KERNEL_DIR)/include $(TEST_SOURCE) && \
			echo "Test module syntax: OK" || echo "Test module syntax: ERROR"; \
	fi

install: module
	@echo "Installing module (optional)..."
	sudo cp $(MODULE_NAME).ko /lib/modules/$(KERNEL_VERSION)/extra/
	sudo depmod -a
	@echo "Module installed to /lib/modules/$(KERNEL_VERSION)/extra/"

help:
	@echo "CXL Memory Allocator Build System"
	@echo "=================================="
	@echo ""
	@echo "Build targets:"
	@echo "  module          - Build both main and test modules (default)"
	@echo "  main-only       - Build only the main CXL allocator module"
	@echo "  test-only       - Build only the test module"
	@echo "  clean           - Clean all build artifacts"
	@echo ""
	@echo "Module management:"
	@echo "  load            - Load the main CXL allocator module"
	@echo "  load-test       - Load both main and test modules"
	@echo "  unload          - Unload all modules"
	@echo "  reload          - Unload and reload modules"
	@echo "  status          - Show module and system status"
	@echo ""
	@echo "Testing:"
	@echo "  test-userspace  - Build and run user-space CXL test"
	@echo "  test-complete   - Run complete test suite"
	@echo ""
	@echo "Development:"
	@echo "  modinfo         - Show module information"
	@echo "  debug-load      - Load with debug output enabled"
	@echo "  format          - Format source code (requires clang-format)"
	@echo "  check-syntax    - Check code syntax"
	@echo ""
	@echo "Installation:"
	@echo "  install         - Install module to kernel modules directory"
	@echo ""
	@echo "Module parameters:"
	@echo "  cxl_dax_device  - DAX device path (default: /dev/dax0.0)"
	@echo "  enable_fallback - Enable system memory fallback (default: true)"
	@echo "  pool_size_mb    - Test pool size in MB (default: 64)"