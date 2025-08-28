#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>  // Added for CXL mmap operations
#include <time.h>
#include <errno.h>
#include <sys/xattr.h>
#include <x86intrin.h>
#include <emmintrin.h>
#include <limits.h>

#define _GNU_SOURCE
#include <sys/stat.h>

#define CACHE_LINE_SIZE 64
#define MEMORY_SIZE (1024 * 1024)  // 1MB test memory

/* Simplified OBD allocation macros for testing */
#define KMALLOC_MAX_SIZE (1024 * 1024)  /* 1MB threshold */

#define SIMPLE_ALLOC_GFP(ptr, size, gfp_mask)                      \
do {                                                               \
    (void)(gfp_mask);                                              \
    (ptr) = malloc(size);                                          \
    if (ptr)                                                       \
        memset(ptr, 0, size);                                      \
} while (0)

#define SIMPLE_VMALLOC(ptr, size)                                  \
do {                                                               \
    (ptr) = malloc(size);                                          \
    if (ptr)                                                       \
        memset(ptr, 0, size);                                      \
} while (0)

#define SIMPLE_ALLOC_LARGE(ptr, size)                              \
do {                                                               \
    if ((size) > KMALLOC_MAX_SIZE)                                 \
        ptr = NULL;                                                \
    else                                                           \
        SIMPLE_ALLOC_GFP(ptr, size, 0);                            \
    if (ptr == NULL)                                               \
        SIMPLE_VMALLOC(ptr, size);                                 \
} while (0)

#define SIMPLE_ALLOC_PTR_ARRAY_LARGE(ptr, n)                      \
    SIMPLE_ALLOC_LARGE(ptr, (n) * sizeof(*(ptr)))

#define SIMPLE_FREE_LARGE(ptr, size)                              \
do {                                                               \
    if (ptr) {                                                     \
        free(ptr);                                                 \
        ptr = NULL;                                                \
    }                                                              \
} while (0)

#define SIMPLE_FREE_PTR_ARRAY_LARGE(ptr, n)                       \
    SIMPLE_FREE_LARGE(ptr, (n) * sizeof(*(ptr)))

struct lu_fid {
    uint64_t f_seq;
    uint32_t f_oid;
    uint32_t f_ver;
};

struct osd_inode_id {
    uint32_t oii_ino;
    uint32_t oii_gen;
};

struct osd_idmap_cache {
    struct lu_fid oic_fid;
    struct osd_inode_id oic_lid;
    int oic_remote;
};

// Results structure for storing benchmark data
struct benchmark_result {
    int array_size;
    char pattern[32];
    char allocation_type[32];
    long alloc_time_ns;
    long flush_time_ns;
    long free_time_ns;
    uint64_t alloc_cycles;
    uint64_t flush_cycles;
    uint64_t free_cycles;
};

// Global results array
#define MAX_RESULTS 1500  // Increased to accommodate CXL results
struct benchmark_result results[MAX_RESULTS];
int result_count = 0;

// Test sizes array
int test_sizes[] = {1, 5, 10, 50, 100, 500, 1000, 2000, 5000, 10000};
int num_test_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);

// CXL configuration
#define CXL_DAX_DEVICE "/dev/dax0.0"  // Adjust based on your CXL setup

// High-resolution timing functions
static inline uint64_t rdtsc_start(void) {
    uint32_t hi, lo;
    __asm__ volatile(
        "cpuid\n\t"
        "rdtsc\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        : "=r"(hi), "=r"(lo)
        :: "%rax", "%rbx", "%rcx", "%rdx"
    );
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t rdtsc_end(void) {
    uint32_t hi, lo;
    __asm__ volatile(
        "rdtscp\n\t"
        "mov %%edx, %0\n\t"
        "mov %%eax, %1\n\t"
        "cpuid\n\t"
        : "=r"(hi), "=r"(lo)
        :: "%rax", "%rbx", "%rcx", "%rdx"
    );
    return ((uint64_t)hi << 32) | lo;
}

// CLFLUSH implementation
static inline void clflush_memory(void *addr) {
    _mm_clflush(addr);
}

static void flush_memory_region(void *ptr, size_t size) {
    char *addr = (char*)ptr;
    for (size_t i = 0; i < size; i += CACHE_LINE_SIZE) {
        clflush_memory(addr + i);
    }
    if (size > 0) {
        clflush_memory(addr + size - 1);
    }
    _mm_sfence();
}

void generate_random_fid(struct lu_fid *fid) {
    fid->f_seq = rand() % 0xFFFFFFFFFFULL;
    fid->f_oid = rand() % 0x100000;
    fid->f_ver = rand() % 100;
}

void generate_random_inode_id(struct osd_inode_id *id) {
    id->oii_ino = rand() % 0x1000000;
    id->oii_gen = rand() % 0x10000;
}

void init_random_idmap_cache_entry(struct osd_idmap_cache *idc) {
    generate_random_fid(&idc->oic_fid);
    generate_random_inode_id(&idc->oic_lid);
    idc->oic_remote = rand() % 2;
}

void save_result(int array_size, const char* pattern, const char* alloc_type,
                long alloc_time_ns, long flush_time_ns, long free_time_ns,
                uint64_t alloc_cycles, uint64_t flush_cycles, uint64_t free_cycles) {
    if (result_count < MAX_RESULTS) {
        struct benchmark_result *r = &results[result_count++];
        r->array_size = array_size;
        strncpy(r->pattern, pattern, sizeof(r->pattern) - 1);
        strncpy(r->allocation_type, alloc_type, sizeof(r->allocation_type) - 1);
        r->alloc_time_ns = alloc_time_ns;
        r->flush_time_ns = flush_time_ns;
        r->free_time_ns = free_time_ns;
        r->alloc_cycles = alloc_cycles;
        r->flush_cycles = flush_cycles;
        r->free_cycles = free_cycles;
    }
}

void test_lustre_allocation(int array_size, const char* pattern) {
    printf("Testing Lustre allocation - Size: %d, Pattern: %s\n", array_size, pattern);
    
    struct timespec start, end;
    uint64_t start_cycles, end_cycles;
    
    // Allocation
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    struct osd_idmap_cache *idc_array = NULL;
    SIMPLE_ALLOC_PTR_ARRAY_LARGE(idc_array, array_size);
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    long alloc_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    uint64_t alloc_cycles = end_cycles - start_cycles;
    
    if (!idc_array) {
        printf("Allocation failed for size %d\n", array_size);
        return;
    }
    
    // Initialize with random data
    for (int i = 0; i < array_size; i++) {
        init_random_idmap_cache_entry(&idc_array[i]);
    }
    
    // Flush
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    // flush_memory_region(idc_array, array_size * sizeof(struct osd_idmap_cache));
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    long flush_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    uint64_t flush_cycles = end_cycles - start_cycles;
    
    // Free
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    free(idc_array);
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    long free_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    uint64_t free_cycles = end_cycles - start_cycles;
    
    save_result(array_size, pattern, "lustre", alloc_time_ns, flush_time_ns, free_time_ns,
                alloc_cycles, flush_cycles, free_cycles);
}

void test_regular_allocation(int array_size, const char* pattern) {
    printf("Testing Regular allocation - Size: %d, Pattern: %s\n", array_size, pattern);
    
    struct timespec start, end;
    uint64_t start_cycles, end_cycles;
    
    // Allocation
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    void **temp_ptrs = malloc(array_size * sizeof(void*));
    if (!temp_ptrs) {
        printf("Allocation failed for size %d\n", array_size);
        return;
    }
    
    for (int i = 0; i < array_size; i++) {
        temp_ptrs[i] = malloc(sizeof(struct osd_idmap_cache));
        if (temp_ptrs[i]) {
            memset(temp_ptrs[i], 0xAA, sizeof(struct osd_idmap_cache));
        }
    }
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    long alloc_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    uint64_t alloc_cycles = end_cycles - start_cycles;
    
    // Flush
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    for (int i = 0; i < array_size; i++) {
        if (temp_ptrs[i]) {
            // flush_memory_region(temp_ptrs[i], sizeof(struct osd_idmap_cache));
            printf("No flush test\n");
        }
    }
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    long flush_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    uint64_t flush_cycles = end_cycles - start_cycles;
    
    // Free
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    for (int i = 0; i < array_size; i++) {
        if (temp_ptrs[i]) {
            free(temp_ptrs[i]);
        }
    }
    free(temp_ptrs);
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    long free_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    uint64_t free_cycles = end_cycles - start_cycles;
    
    save_result(array_size, pattern, "regular", alloc_time_ns, flush_time_ns, free_time_ns,
                alloc_cycles, flush_cycles, free_cycles);
}

// NEW: CXL Memory Allocation Test
void test_cxl_allocation(int array_size, const char* pattern) {
    printf("Testing CXL allocation - Size: %d, Pattern: %s\n", array_size, pattern);
    
    const char *dax_device_path = CXL_DAX_DEVICE;
    size_t total_size = array_size * sizeof(struct osd_idmap_cache);
    
    // Ensure alignment to page boundaries (CXL often requires alignment)
    size_t page_size = getpagesize();
    size_t aligned_size = ((total_size + page_size - 1) / page_size) * page_size;
    
    // Ensure minimum 2MB alignment as mentioned in CXL example
    size_t min_alignment = 2 * 1024 * 1024; // 2 MiB
    if (aligned_size < min_alignment) {
        aligned_size = min_alignment;
    }
    
    struct timespec start, end;
    uint64_t start_cycles, end_cycles;
    int fd = -1;
    void *cxl_addr = MAP_FAILED;
    
    // Allocation (open + mmap)
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    fd = open(dax_device_path, O_RDWR);
    if (fd == -1) {
        printf("Failed to open CXL DAX device %s for size %d: %s\n", 
               dax_device_path, array_size, strerror(errno));
        return;
    }
    
    cxl_addr = mmap(NULL, aligned_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (cxl_addr == MAP_FAILED) {
        printf("Failed to mmap CXL memory for size %d: %s\n", array_size, strerror(errno));
        close(fd);
        return;
    }
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    long alloc_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    uint64_t alloc_cycles = end_cycles - start_cycles;
    
    // Initialize with random data (same as other tests)
    struct osd_idmap_cache *idc_array = (struct osd_idmap_cache *)cxl_addr;
    for (int i = 0; i < array_size; i++) {
        init_random_idmap_cache_entry(&idc_array[i]);
    }
    
    // Flush - Critical for CXL Shared Memory Pool as noted in the example
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    // flush_memory_region(cxl_addr, total_size);
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    long flush_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    uint64_t flush_cycles = end_cycles - start_cycles;
    
    // Free (munmap + close)
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    if (munmap(cxl_addr, aligned_size) == -1) {
        printf("Error unmapping CXL memory for size %d: %s\n", array_size, strerror(errno));
    }
    if (close(fd) == -1) {
        printf("Error closing CXL DAX device for size %d: %s\n", array_size, strerror(errno));
    }
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    long free_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    uint64_t free_cycles = end_cycles - start_cycles;
    
    save_result(array_size, pattern, "cxl", alloc_time_ns, flush_time_ns, free_time_ns,
                alloc_cycles, flush_cycles, free_cycles);
}

void run_sequential_pattern() {
    printf("\n=== Running Sequential Pattern ===\n");
    for (int i = 0; i < num_test_sizes; i++) {
        test_lustre_allocation(test_sizes[i], "sequential");
        test_regular_allocation(test_sizes[i], "sequential");
        test_cxl_allocation(test_sizes[i], "sequential");  // NEW: CXL test
    }
}

void run_reverse_pattern() {
    printf("\n=== Running Reverse Pattern ===\n");
    for (int i = num_test_sizes - 1; i >= 0; i--) {
        test_lustre_allocation(test_sizes[i], "reverse");
        test_regular_allocation(test_sizes[i], "reverse");
        test_cxl_allocation(test_sizes[i], "reverse");  // NEW: CXL test
    }
}

void run_strided_pattern() {
    printf("\n=== Running Strided Pattern ===\n");
    // Alternate between small and large allocations
    for (int i = 0; i < num_test_sizes; i += 2) {
        test_lustre_allocation(test_sizes[i], "strided");
        test_regular_allocation(test_sizes[i], "strided");
        test_cxl_allocation(test_sizes[i], "strided");  // NEW: CXL test
        if (i + 1 < num_test_sizes) {
            test_lustre_allocation(test_sizes[num_test_sizes - 1 - i/2], "strided");
            test_regular_allocation(test_sizes[num_test_sizes - 1 - i/2], "strided");
            test_cxl_allocation(test_sizes[num_test_sizes - 1 - i/2], "strided");  // NEW: CXL test
        }
    }
}

void save_results_to_csv() {
    FILE *fp = fopen("benchmark_results.csv", "w");
    if (!fp) {
        printf("Error: Could not open results file for writing\n");
        return;
    }
    
    fprintf(fp, "array_size,pattern,allocation_type,alloc_time_ns,flush_time_ns,free_time_ns,alloc_cycles,flush_cycles,free_cycles\n");
    
    for (int i = 0; i < result_count; i++) {
        struct benchmark_result *r = &results[i];
        fprintf(fp, "%d,%s,%s,%ld,%ld,%ld,%lu,%lu,%lu\n",
                r->array_size, r->pattern, r->allocation_type,
                r->alloc_time_ns, r->flush_time_ns, r->free_time_ns,
                r->alloc_cycles, r->flush_cycles, r->free_cycles);
    }
    
    fclose(fp);
    printf("Results saved to benchmark_results.csv\n");
}

// NEW: Function to check CXL device availability
int check_cxl_device_availability() {
    int fd = open(CXL_DAX_DEVICE, O_RDWR);
    if (fd == -1) {
        printf("Warning: CXL DAX device %s not available: %s\n", 
               CXL_DAX_DEVICE, strerror(errno));
        printf("CXL tests will be skipped. To enable CXL tests:\n");
        printf("1. Ensure CXL device is properly configured\n");
        printf("2. Check device path (currently set to %s)\n", CXL_DAX_DEVICE);
        printf("3. If device path is different, modify CXL_DAX_DEVICE in the code\n");
        return 0;
    }
    close(fd);
    printf("CXL DAX device %s is available\n", CXL_DAX_DEVICE);
    return 1;
}

int main() {
    srand(time(NULL));
    
    printf("Starting Enhanced Memory Allocation Benchmark (with CXL support)\n");
    printf("Testing allocation sizes: ");
    for (int i = 0; i < num_test_sizes; i++) {
        printf("%d ", test_sizes[i]);
    }
    printf("\n");
    
    // Check CXL device availability
    int cxl_available = check_cxl_device_availability();
    if (!cxl_available) {
        printf("Proceeding without CXL tests...\n");
    }
    
    printf("\nTesting allocation types: Lustre, Regular");
    if (cxl_available) {
        printf(", CXL");
    }
    printf("\n");
    
    // Run different allocation patterns
    run_sequential_pattern();
    run_reverse_pattern();
    run_strided_pattern();
    
    // Save results to CSV
    save_results_to_csv();
    
    printf("\nBenchmark completed. Total results: %d\n", result_count);
    
    if (cxl_available) {
        printf("CXL tests completed successfully.\n");
        printf("Check the CSV file for performance comparisons between Regular, Lustre, and CXL allocations.\n");
    } else {
        printf("CXL tests were skipped due to device unavailability.\n");
    }
    
    return 0;
}