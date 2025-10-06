/**
 * @file cxl_test2_structure.c
 * @brief Test program for shared data structure behavior using CXL memory.
 *
 * This test demonstrates how multiple processes can interact with a shared data structure
 * mapped to a CXL device. The shared structure contains an array,
 * a sum of its elements, modification metadata, and an operation log. Each process modifies
 * a portion of the array, updates the sum, and logs its actions, ensuring consistency via
 * memory barriers and cache flushes.
 *
 * Key Features:
 * - Maps a region of persistent memory from a CXL device (/dev/dax0.0).
 * - Initializes the shared structure on first access, including a magic number for validation.
 * - Each process (identified by an instance ID) performs 10 rounds of modifications:
 *   - Verifies the consistency of the stored sum versus a freshly calculated sum.
 *   - Modifies a slice of the array based on its instance ID and round number.
 *   - Updates metadata: sum, last modifier, modification count, and operation log.
 *   - Uses memory barriers and cache flushes to ensure changes are visible to other processes.
 * - Prints the final state of the shared structure after all rounds.
 *
 * Usage:
 *   ./cxl_test2_structure <instance_id>
 *
 *
 * Structure Fields:
 *   - magic: Magic number to validate initialization.
 *   - array_sum: Sum of all elements in the array.
 *   - array: Integer array shared among processes.
 *   - last_modifier: Instance ID of the last process to modify the array.
 *   - modification_count: Total number of modifications performed.
 *   - operation_log: Log of the last operation performed.
 *
 * Synchronization:
 *   - Uses mfence and clflush instructions for write barriers.
 *   - Uses lfence for read barriers.
 *
 */
// ===== TEST 2: Shared Data Structure Behavior =====
// Save as cxl_test2_structure.c

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <errno.h>

#define CXL_DEVICE_PATH "/dev/dax0.0"
#define CXL_TEST_SIZE (2 * 1024 * 1024)
#define CXL_MAGIC 0xC7A1C7A1
#define ARRAY_SIZE 1000

typedef struct {
    volatile uint64_t magic;
    volatile uint64_t array_sum;
    volatile int array[ARRAY_SIZE];
    volatile uint64_t last_modifier;
    volatile uint64_t modification_count;
    volatile char operation_log[256];
} cxl_test2_data_t;

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

uint64_t calculate_sum(volatile int *array, int size) {
    uint64_t sum = 0;
    for (int i = 0; i < size; i++) {
        sum += array[i];
    }
    return sum;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <instance_id>\n", argv[0]);
        return 1;
    }
    
    int instance_id = atoi(argv[1]);
    printf("=== TEST 2: Shared Data Structure (S%d) ===\n", instance_id);
    
    int fd = open(CXL_DEVICE_PATH, O_RDWR);
    if (fd == -1) {
        perror("Failed to open CXL device");
        return 1;
    }
    
    cxl_test2_data_t *shared = mmap(NULL, CXL_TEST_SIZE, 
                                    PROT_READ | PROT_WRITE, 
                                    MAP_SHARED, fd, 0);
    if (shared == MAP_FAILED) {
        perror("Failed to map CXL memory");
        close(fd);
        return 1;
    }
    
    // Initialize structure on first access
    if (shared->magic != CXL_MAGIC) {
        printf("Initializing shared structure...\n");
        shared->magic = CXL_MAGIC;
        shared->array_sum = 0;
        shared->modification_count = 0;
        for (int i = 0; i < ARRAY_SIZE; i++) {
            shared->array[i] = i;  // Initialize with index values
        }
        shared->array_sum = calculate_sum(shared->array, ARRAY_SIZE);
        cxl_write_barrier(shared, sizeof(cxl_test2_data_t));
        printf("Structure initialized\n");
    }
    
    printf("Testing shared structure access...\n");
    
    for (int round = 0; round < 10; round++) {
        cxl_read_barrier();
        
        // Read and verify structure consistency
        uint64_t read_sum = shared->array_sum;
        uint64_t calculated_sum = calculate_sum(shared->array, ARRAY_SIZE);
        
        printf("Round %d: Stored sum=%lu, Calculated sum=%lu %s\n", 
               round, read_sum, calculated_sum, 
               (read_sum == calculated_sum) ? "✓" : "INCONSISTENT!");
        
        // Modify part of the array
        int start_idx = (instance_id * 100 + round * 10) % ARRAY_SIZE;
        int end_idx = start_idx + 10;
        
        printf("  S%d modifying indices %d-%d...\n", instance_id, start_idx, end_idx-1);
        
        for (int i = start_idx; i < end_idx && i < ARRAY_SIZE; i++) {
            shared->array[i] += instance_id;  // Add instance ID to values
        }
        
        // Recalculate and update sum
        shared->array_sum = calculate_sum(shared->array, ARRAY_SIZE);
        shared->last_modifier = instance_id;
        shared->modification_count++;
        snprintf((char*)shared->operation_log, 256, 
                "S%d_R%d: Modified %d-%d, Sum=%lu", 
                instance_id, round, start_idx, end_idx-1, shared->array_sum);
        
        cxl_write_barrier(shared, sizeof(cxl_test2_data_t));
        
        sleep(1); // ~~~~
    }
    
    printf("\nFinal Structure State:\n");
    printf("  Array Sum: %lu\n", shared->array_sum);
    printf("  Last Modifier: S%lu\n", shared->last_modifier);
    printf("  Total Modifications: %lu\n", shared->modification_count);
    printf("  Last Operation: %s\n", shared->operation_log);
    
    if (munmap(shared, CXL_TEST_SIZE) == -1) {
        perror("Failed to unmap CXL memory");
    }
    close(fd);
    return 0;
}