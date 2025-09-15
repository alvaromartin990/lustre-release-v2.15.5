MODULE_NAME := cxl_kmem_allocator
TEST_MODULE := test_cxl_allocator
KERNEL_VERSION ?= $(shell uname -r)
KERNEL_DIR := /lib/modules/$(KERNEL_VERSION)/build
PWD := $(shell pwd)

# Build both modules
obj-m += $(MODULE_NAME).o
obj-m += $(TEST_MODULE).o

# Minimal compiler flags
ccflags-y += -I$(PWD)

all: modules

modules:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
	rm -f *.o *.ko *.mod.c *.mod.o *.symvers *.order

# Load main CXL allocator
load: modules
	sudo insmod $(MODULE_NAME).ko cxl_dax_device="/dev/dax0.0" enable_fallback=1 pool_size_mb=64

unload:
	sudo rmmod $(MODULE_NAME) || true
	sudo rmmod $(TEST_MODULE) || true

# Run tests
test: modules load
	@echo "Running CXL allocator tests..."
	sudo insmod $(TEST_MODULE).ko
	@sleep 1
	@echo "=== Test Results ==="
	dmesg | grep -A 20 "CXL Allocator Test Starting" | tail -25
	sudo rmmod $(TEST_MODULE)

# Quick status check
status:
	@echo "=== Module Status ==="
	lsmod | grep -E "(cxl_kmem|test_cxl)" || echo "No CXL modules loaded"
	@echo "=== Recent CXL Messages ==="
	dmesg | grep CXL | tail -10

reload: unload load

help:
	@echo "CXL Allocator Build and Test System"
	@echo "===================================="
	@echo "  modules  - Build all modules"
	@echo "  load     - Load CXL allocator"
	@echo "  test     - Run allocation tests"
	@echo "  status   - Show current status"
	@echo "  clean    - Clean build files"
	@echo "  unload   - Unload all modules"

.PHONY: all modules clean load unload test status reload help