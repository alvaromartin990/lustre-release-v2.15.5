/**
 * @file cxl_test1_simultaneous.c
 * @brief Test program for simultaneous read/write capabilities on a CXL device.
 *
 * This test demonstrates concurrent access to a CXL memory device by performing
 * a series of mixed read and write operations on a shared memory region. It is
 * designed to be run with different instances (e.g., S6, S7) to simulate
 * multiple processes or threads interacting with the same CXL-backed memory.
 *
 * Key Features:
 * - Maps a CXL device file (default: /dev/dax0.0) into user space.
 * - Initializes a shared data structure with magic value and counters.
 * - Performs 100 operations, each consisting of:
 *   - Reading current state (counter, operation count).
 *   - Writing updated values (writer ID, counter, operation count, timestamp, status).
 *   - Flushing cache lines and enforcing memory barriers for data consistency.
 * - Prints operation details and final results after completion.
 *
 * Usage:
 *   ./cxl_test1_simultaneous <instance_id>
 *     instance_id: 6 for S6, 7 for S7
 *
 * Data Structure:
 *   cxl_test1_data_t - Contains magic value, counters, writer ID, status string,
 *                      and timestamps for each operation.
 *
 *
 */
// ===== TEST 1: Simultaneous Read/Write Capabilities =====

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <stdint.h>
#include <errno.h>

#define CXL_DEVICE_PATH "/dev/dax0.0"
#define CXL_TEST_SIZE (2 * 1024 * 1024)
#define CXL_MAGIC 0xC7A1C7A1
#define NUM_OPERATIONS 100

typedef struct {
    volatile uint64_t magic;
    volatile uint64_t counter;
    volatile uint64_t writer_id;
    volatile uint64_t operation_count;
    volatile char status[64];
    volatile uint64_t timestamps[NUM_OPERATIONS];
} cxl_test1_data_t;

/**
 * cxl_write_barrier - Ensures write ordering and cache coherency for a memory region.
 * @addr: Pointer to the start of the memory region.
 * @size: Size of the memory region in bytes.
 *
 * This function enforces a write memory barrier using an MFENCE instruction,
 * flushes the cache lines covering the specified memory region using CLFLUSH,
 * and then issues another MFENCE to ensure all writes are globally visible.
 */
static inline void cxl_write_barrier(void *addr, size_t size) {
    __builtin_ia32_mfence();
    for (size_t i = 0; i < size; i += 64) {
        __builtin_ia32_clflush((char*)addr + i);
    }
    __builtin_ia32_mfence();
}

/**
 * cxl_read_barrier - Ensures read ordering for subsequent memory operations.
 *
 * This function enforces a read memory barrier using an LFENCE instruction,
 * Useful for ensuring data consistency when reading from memory-mapped devices.
 */
static inline void cxl_read_barrier(void) {
    __builtin_ia32_lfence();
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <instance_id>\n", argv[0]);
        printf("  instance_id: 6 for S6, 7 for S7\n");
        return 1;
    }
    
    int instance_id = atoi(argv[1]);
    printf("=== TEST 1: Simultaneous Read/Write (S%d) ===\n", instance_id);
    
    int fd = open(CXL_DEVICE_PATH, O_RDWR);
    if (fd == -1) {
        perror("Failed to open CXL device");
        return 1;
    }
    
    cxl_test1_data_t *shared = mmap(NULL, CXL_TEST_SIZE, 
                                    PROT_READ | PROT_WRITE, 
                                    MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) {
        perror("Failed to map CXL memory");
        close(fd);
        return 1;
    }
    
    // Initialize on first access
    if (shared->magic != CXL_MAGIC) {
        shared->magic = CXL_MAGIC;
        shared->counter = 0;
        shared->operation_count = 0;
        strcpy((char*)shared->status, "INIT");
        cxl_write_barrier(shared, sizeof(cxl_test1_data_t));
    }
    
    printf("Starting simultaneous read/write test...\n");
    
    // Perform 100 operations mixing reads and writes
    for (int i = 0; i < NUM_OPERATIONS; i++) {
        cxl_read_barrier();
        
        // Read current state
        uint64_t current_counter = shared->counter;
        uint64_t current_ops = shared->operation_count;
        
        // Write new data
        shared->writer_id = instance_id;
        shared->counter = current_counter + 1;
        shared->operation_count = current_ops + 1;
        shared->timestamps[i % NUM_OPERATIONS] = time(NULL);
        snprintf((char*)shared->status, 64, "S%d_OP_%d", instance_id, i);
        
        cxl_write_barrier(shared, sizeof(cxl_test1_data_t));
        
        printf("S%d Op %d: Counter %lu->%lu, Ops: %lu->%lu\n", 
               instance_id, i, current_counter, shared->counter, 
               current_ops, shared->operation_count);
        
        usleep(100000);  // 100ms delay
    }
    
    printf("\nFinal Results (S%d):\n", instance_id);
    printf("  Final Counter: %lu\n", shared->counter);
    printf("  Total Operations: %lu\n", shared->operation_count);
    printf("  Last Writer: S%lu\n", shared->writer_id);
    printf("  Status: %s\n", shared->status);
    
    munmap(shared, CXL_TEST_SIZE);
    close(fd);
    return 0;
}