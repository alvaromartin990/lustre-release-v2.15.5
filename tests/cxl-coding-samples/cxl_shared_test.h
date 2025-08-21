// ===== SHARED HEADER (save as cxl_shared_test.h) =====
#ifndef CXL_SHARED_TEST_H
#define CXL_SHARED_TEST_H

#include <stdint.h>
#include <time.h>

#define CXL_DEVICE_PATH "/dev/dax0.0"
#define CXL_TEST_SIZE (2 * 1024 * 1024)  // 2MB
#define CXL_MAGIC 0xC7A1C7A1

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