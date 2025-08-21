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
    
    printf("🏁 Starting race condition test (%d iterations)...\n", RACE_ITERATIONS);
    
    int races_found = 0;
    
    for (int i = 0; i < RACE_ITERATIONS; i++) {
        uint64_t start_time = get_timestamp_us();
        
        cxl_read_barrier();
        
        // Check if another writer is active
        if (shared->active_writers > 0) {
            races_found++;
            shared->race_detected++;
            printf("🚨 RACE DETECTED at iteration %d! Active writers: %lu\n", 
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
    
    printf("\n📊 Race Test Results (S%d):\n", instance_id);
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
