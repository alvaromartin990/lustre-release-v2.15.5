/*
 * cxl_shared_test.h
 *
 * Shared header for CXL memory communication and cache coherency testing.
 *
 * Defines:
 *   - CXL_DEVICE_PATH: Path to the CXL device.
 *   - CXL_TEST_SIZE: Size of the test memory region (2MB).
 *   - CXL_MAGIC: Magic number for data validation.
 *
 * Structures:
 *   - cxl_shared_data_t: Used for sharing data between processes via CXL memory.
 *     Fields are marked volatile to ensure visibility across CPU caches.
 *     Includes magic, sequence, timestamp, message, and writer_id fields.
 *
 * Functions:
 *   - cxl_write_barrier(void *addr, size_t size):
 *       Ensures cache coherency by flushing cache lines and issuing memory fences.
 *       Should be called after writing to shared memory.
 *   - cxl_read_barrier(void):
 *       Issues a load fence to ensure read operations are completed.
 *       Should be called before reading from shared memory.
 *
 * Usage:
 *   Include this header in both writer and reader processes to facilitate
 *   communication and cache coherency testing using CXL devices.
 */
// ===== SHARED HEADER (save as cxl_shared_test.h) =====
#ifndef CXL_SHARED_TEST_H
#define CXL_SHARED_TEST_H

#include <stdint.h>
#include <time.h>

#define CXL_DEVICE_PATH "/dev/dax0.0"
#define CXL_TEST_SIZE (2 * 1024 * 1024)  // 2MB
#define CXL_MAGIC 0xC7A1C7A1

// Structure for sharing data between processes using CXL memory.
// Fields are marked volatile to ensure visibility across CPU caches.
// Used for testing cache coherency and communication via CXL device.
typedef struct {
    volatile uint64_t magic;
    volatile uint64_t sequence;
    volatile uint64_t timestamp;
    volatile char message[256];
    volatile uint64_t writer_id;
} cxl_shared_data_t;

// CXL cache coherency operations
static inline void cxl_write_barrier(void *addr, size_t size) {
    __builtin_ia32_mfence();  // Memory fence first
    // Flush cache lines
    for (size_t i = 0; i < size; i += 64) {
        __builtin_ia32_clflush((char*)addr + i);
    }
    __builtin_ia32_mfence();  // Fence after flush
}

static inline void cxl_read_barrier(void) {
    __builtin_ia32_lfence();  // Load fence for reads
}

#endif