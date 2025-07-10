# OBD_ALLOC_PTR_ARRAY_LARGE Test Module

This directory contains kernel-space test modules for testing the `OBD_ALLOC_PTR_ARRAY_LARGE` macro with `osd_idmap_cache` structures in the Lustre filesystem.

## Files

1. **`test_obd_alloc_idmap_cache.c`** - Full Lustre-integrated test module
2. **`simple_obd_alloc_test.c`** - Simplified standalone test module
3. **`Makefile.test`** - Makefile for the full Lustre test
4. **`Makefile.simple`** - Makefile for the simplified test
5. **`README_TEST.md`** - This file

## Test Modules

### Simple Test Module (Recommended)

The `simple_obd_alloc_test.c` module is a standalone test that doesn't require full Lustre dependencies. It:

- Mimics the `osd_idmap_cache` structure with simplified versions
- Tests the allocation/deallocation pattern similar to `OBD_ALLOC_PTR_ARRAY_LARGE`
- Includes timing measurements for allocation and deallocation
- Tests both small (kmalloc) and large (vmalloc) allocations
- Performs stress testing with random allocation sizes

### Full Lustre Test Module

The `test_obd_alloc_idmap_cache.c` module is a complete test integrated with Lustre headers. It requires:

- Full Lustre source tree with headers
- Proper Lustre development environment
- All Lustre dependencies

## Building and Running

### Quick Start (Simple Test)

1. **Build the simple test module:**
   ```bash
   make -f Makefile.simple
   ```

2. **Run the test:**
   ```bash
   make -f Makefile.simple test
   ```

3. **Clean up:**
   ```bash
   make -f Makefile.simple clean
   ```

### Manual Testing

1. **Build:**
   ```bash
   make -f Makefile.simple modules
   ```

2. **Install:**
   ```bash
   sudo insmod simple_obd_alloc_test.ko
   ```

3. **Check results:**
   ```bash
   dmesg | grep "simple_test" | tail -20
   ```

4. **Remove:**
   ```bash
   sudo rmmod simple_obd_alloc_test
   ```

### Full Lustre Test

1. **Set up environment:**
   ```bash
   # Make sure you're in the Lustre source directory
   cd /path/to/lustre-release-v2.15.5
   ```

2. **Build:**
   ```bash
   make -f Makefile.test
   ```

3. **Run:**
   ```bash
   make -f Makefile.test test
   ```

## Test Results

The test modules will output detailed information to the kernel log, including:

- Allocation timing (in nanoseconds)
- Memory allocation method (kmalloc vs vmalloc)
- Sample data verification
- Stress test results
- Success/failure status

### Expected Output

```
simple_test: Loading simple OBD allocation test module
simple_test: sizeof(struct simple_idmap_cache) = 32 bytes
simple_test: KMALLOC_MAX_SIZE = 1048576 bytes
simple_test: Testing allocation of 10 entries (320 bytes)
simple_test: Allocation successful in 1234 ns
simple_test: Memory allocated via kmalloc
simple_test: Entry 0 - FID: [12345:67890:1], Inode: 98765/4321, Remote: 1
simple_test: Deallocation successful in 567 ns
...
simple_test: All tests completed successfully!
```

## Test Coverage

The test modules cover:

1. **Basic Allocation/Deallocation**
   - Small arrays (kmalloc path)
   - Large arrays (vmalloc path)
   - Timing measurements

2. **Edge Cases**
   - Single entry allocation
   - Zero-size allocation
   - Very large allocations

3. **Data Integrity**
   - Random data initialization
   - Structure member verification
   - Memory pattern validation

4. **Stress Testing**
   - Multiple allocation/deallocation cycles
   - Random sizes
   - Performance under load

5. **Memory Management**
   - Proper cleanup
   - Memory leak detection
   - Allocation method verification

## Performance Metrics

The tests measure:

- **Allocation latency** - Time to allocate memory
- **Deallocation latency** - Time to free memory
- **Memory usage** - Total bytes allocated
- **Allocation method** - Whether kmalloc or vmalloc was used

## Troubleshooting

### Common Issues

1. **Module fails to load:**
   - Check kernel log: `dmesg | tail -20`
   - Ensure you have root privileges
   - Verify kernel headers are installed

2. **Compilation errors:**
   - Install kernel development packages
   - Check kernel version compatibility

3. **Memory allocation failures:**
   - Normal for very large allocations
   - Check available memory: `free -h`

### System Requirements

- Linux kernel 3.10+
- Kernel headers installed
- GCC compiler
- Root privileges for module loading

## Customization

You can modify the test parameters in the source files:

```c
#define TEST_ITERATIONS 5      // Number of stress test iterations
#define TEST_MIN_SIZE 1        // Minimum allocation size
#define TEST_MAX_SIZE 1000     // Maximum allocation size
#define KMALLOC_MAX_SIZE (1024 * 1024)  // Threshold for vmalloc
```

## Safety

- The tests are designed to be safe and non-destructive
- All allocations are properly freed
- Module can be safely loaded/unloaded multiple times
- No persistent changes to the system
