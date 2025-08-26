/**
 * @file cxl_test3_race.c
 * @brief Test 3: Concurrent Access Race Conditions on CXL Device
 *
 * This test program demonstrates and detects race conditions when multiple processes
 * concurrently access and modify shared memory mapped from a CXL (Compute Express Link) device.
 * It is designed to be run as separate instances (e.g., S6 and S7) to simulate concurrent writers.
 *
 * Key Features:
 * - Maps a CXL device file (/dev/dax0.0) into shared memory.
 * - Uses a shared data structure (`cxl_test3_data_t`) to track operations, race detection, and counters.
 * - Employs memory barriers and cache flushes to ensure visibility and ordering of memory operations.
 * - Each instance increments its own operation counter and a shared race counter in a critical section.
 * - Detects race conditions by checking if another writer is active during the critical section.
 * - Reports the number of races detected and checks for data corruption at the end.
 *
 * Usage:
 *   ./cxl_test3_race <instance_id>
 *   - instance_id: Integer identifier for the test instance (e.g., 6 or 7).
 *
 * Main Components:
 * - cxl_test3_data_t: Shared structure for synchronization and statistics.
 * - cxl_write_barrier(): Ensures write ordering and flushes cache lines.
 * - cxl_read_barrier(): Ensures read ordering.
 * - get_timestamp_us(): Utility for microsecond-precision timestamps.
 *
 * Output:
 * - Prints progress, race detection events, and final statistics including
 *   operation counts and data integrity check.
 *
 * Note:
 * - Intended for use on systems with CXL device support and appropriate permissions.
 * - Demonstrates the importance of proper synchronization in concurrent memory access.
 */
// ===== TEST 3: Concurrent Access Race Conditions =====
// Save as cxl_test3_race.c

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <stdint.h>
#include <errno.h>

#define CXL_DEVICE_PATH "/dev/dax0.0"
#define CXL_TEST_SIZE (2 * 1024 * 1024)
#define CXL_MAGIC 0xC7A1C7A1
#define RACE_ITERATIONS 1000

typedef struct {
    volatile uint64_t magic;
    volatile uint64_t race_counter;
    volatile uint64_t s6_operations;
    volatile uint64_t s7_operations;
    volatile uint64_t race_detected;
    volatile uint64_t last_timestamp_us;
    volatile uint64_t active_writers;
    volatile char race_log[512];
} cxl_test3_data_t;

static inline void cxl_write_barrier(void *addr, size_t size) {
    __builtin_ia32_mfence();
    for (size_t i = 0; i < size; i += 64) {
        __builtin_ia32_clflush((char*)addr + i);
    }
    __builtin_ia32_mfence();
}

static inline void cxl_read_barrier(void) {
    __builtin_ia32_lfence();
}

uint64_t get_timestamp_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <instance_id>\n", argv[0]);
        return 1;
    }
    
    int instance_id = atoi(argv[1]);
    printf("=== TEST 3: Race Condition Detection (S%d) ===\n", instance_id);
    
    int fd = open(CXL_DEVICE_PATH, O_RDWR);
    if (fd == -1) {
        perror("Failed to open CXL device");
        return 1;
    }
    
    cxl_test3_data_t *shared = mmap(NULL, CXL_TEST_SIZE, 
                                    PROT_READ | PROT_WRITE, 
                                    MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) {
        perror("Failed to map CXL memory");
        close(fd);
        return 1;
    }

    // CRITICAL: Always clear the entire mapped region first to avoid corruption from previous runs
    printf("Clearing CXL memory region to ensure clean test...\n");
    memset(shared, 0, CXL_TEST_SIZE);
    cxl_write_barrier(shared, CXL_TEST_SIZE);

    // Initialize on first access
    if (shared->magic != CXL_MAGIC) {
        shared->magic = CXL_MAGIC;
        shared->race_counter = 0;
        shared->s6_operations = 0;
        shared->s7_operations = 0;
        shared->race_detected = 0;
        shared->active_writers = 0;
        strcpy((char*)shared->race_log, "INIT");
        cxl_write_barrier(shared, sizeof(cxl_test3_data_t));
    }
    
    printf("Starting race condition test (%d iterations)...\n", RACE_ITERATIONS);
    
    int races_found = 0;
    
    for (int i = 0; i < RACE_ITERATIONS; i++) {
        uint64_t start_time = get_timestamp_us();
        
        cxl_read_barrier();
        
        // Check if another writer is active
        if (shared->active_writers > 0) {
            races_found++;
            shared->race_detected++;
            printf("RACE DETECTED at iteration %d! Active writers: %lu\n", 
                   i, shared->active_writers);
        }
        
        // Mark this instance as active writer
        shared->active_writers++;
        cxl_write_barrier(&shared->active_writers, sizeof(uint64_t));
        
        // Simulate critical section with read-modify-write
        uint64_t old_counter = shared->race_counter;
        usleep(10);  // Small delay to increase race window
        shared->race_counter = old_counter + 1;
        
        // Update instance-specific counter
        if (instance_id == 6) {
            shared->s6_operations++;
        } else {
            shared->s7_operations++;
        }
        
        shared->last_timestamp_us = get_timestamp_us();
        
        // Mark writer as inactive
        shared->active_writers--;
        
        cxl_write_barrier(shared, sizeof(cxl_test3_data_t));
        
        if (i % 100 == 0) {
            printf("S%d: %d iterations, Counter: %lu, Races: %d\n", 
                   instance_id, i, shared->race_counter, races_found);
        }
        
        usleep(1000);  // 1ms delay between operations
    }
    
    // Final synchronization check
    sleep(2);
    cxl_read_barrier();
    
    printf("\nRace Test Results (S%d):\n", instance_id);
    printf("  Final Counter: %lu\n", shared->race_counter);
    printf("  Expected Counter: %lu\n", shared->s6_operations + shared->s7_operations);
    printf("  S6 Operations: %lu\n", shared->s6_operations);
    printf("  S7 Operations: %lu\n", shared->s7_operations);
    printf("  Races Detected: %lu\n", shared->race_detected);
    printf("  Local Races Found: %d\n", races_found);
    
    uint64_t expected = shared->s6_operations + shared->s7_operations;
    if (shared->race_counter == expected) {
        printf("  ✅ NO DATA CORRUPTION detected\n");
    } else {
        printf("  ❌ DATA CORRUPTION: %lu missing operations\n", 
               expected - shared->race_counter);
    }
    
    munmap(shared, CXL_TEST_SIZE);
    close(fd);
    return 0;
}
