#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
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

/* CXL DAX device configuration */
#define CXL_DAX_DEVICE_PATH "/dev/dax1.0"  // Adjust based on your setup
#define CXL_MAPPING_SIZE (128ULL * 1024 * 1024 * 1024)  // 128GB as mentioned in the PDF
#define CXL_ALIGNMENT_SIZE (2 * 1024 * 1024)  // 2MiB alignment requirement

/* Global CXL memory management */
static int cxl_fd = -1;
static void *cxl_base_addr = NULL;
static size_t cxl_current_offset = 0;
static size_t cxl_total_size = 0;

/* Simplified OBD allocation macros adapted for CXL */
#define KMALLOC_MAX_SIZE (1024 * 1024)  /* 1MB threshold */

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

/* CXL Memory Management Functions */
int init_cxl_memory() {
    printf("Initializing CXL memory via DAX device: %s\n", CXL_DAX_DEVICE_PATH);
    
    // Open the DAX device
    cxl_fd = open(CXL_DAX_DEVICE_PATH, O_RDWR);
    if (cxl_fd == -1) {
        perror("Error opening CXL DAX device");
        printf("Note: Make sure the DAX device exists. You might need to run:\n");
        printf("  sudo daxctl reconfigure-device --mode=devdax --force dax0.0\n");
        return -1;
    }
    
    // Memory-map the CXL DAX device
    cxl_base_addr = mmap(NULL, CXL_MAPPING_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, cxl_fd, 0);
    if (cxl_base_addr == MAP_FAILED) {
        perror("Error memory-mapping CXL DAX device");
        close(cxl_fd);
        cxl_fd = -1;
        return -1;
    }
    
    cxl_total_size = CXL_MAPPING_SIZE;
    cxl_current_offset = 0;
    
    printf("Successfully mapped CXL memory: %p (size: %lu GB)\n", 
           cxl_base_addr, cxl_total_size / (1024*1024*1024));
    
    return 0;
}

void cleanup_cxl_memory() {
    if (cxl_base_addr != NULL && cxl_base_addr != MAP_FAILED) {
        if (munmap(cxl_base_addr, cxl_total_size) == -1) {
            perror("Error unmapping CXL memory");
        }
        cxl_base_addr = NULL;
    }
    
    if (cxl_fd != -1) {
        if (close(cxl_fd) == -1) {
            perror("Error closing CXL DAX device");
        }
        cxl_fd = -1;
    }
    
    printf("CXL memory cleanup completed.\n");
}

/* CXL-aware memory allocator */
void* cxl_alloc(size_t size) {
    if (cxl_base_addr == NULL) {
        fprintf(stderr, "CXL memory not initialized!\n");
        return NULL;
    }
    
    // Align allocation to cache line boundary
    size_t aligned_size = (size + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);
    
    if (cxl_current_offset + aligned_size > cxl_total_size) {
        fprintf(stderr, "CXL memory exhausted! Requested: %lu, Available: %lu\n", 
                aligned_size, cxl_total_size - cxl_current_offset);
        return NULL;
    }
    
    void *ptr = (char*)cxl_base_addr + cxl_current_offset;
    cxl_current_offset += aligned_size;
    
    // Zero out the allocated memory
    memset(ptr, 0, size);
    
    return ptr;
}

void cxl_free(void *ptr, size_t size) {
    // For this simple allocator, we don't actually free individual blocks
    // In a production system, you'd implement a proper free list
    (void)ptr;
    (void)size;
}

void cxl_reset_allocator() {
    cxl_current_offset = 0;
    printf("CXL allocator reset. Available memory: %lu GB\n", 
           cxl_total_size / (1024*1024*1024));
}

/* Updated allocation macros for CXL */
#define CXL_ALLOC_GFP(ptr, size, gfp_mask)                         \
do {                                                               \
    (void)(gfp_mask);                                              \
    (ptr) = cxl_alloc(size);                                       \
} while (0)

#define CXL_ALLOC_PTR_ARRAY_LARGE(ptr, n)                         \
    CXL_ALLOC_GFP(ptr, (n) * sizeof(*(ptr)), 0)

#define CXL_FREE_PTR_ARRAY_LARGE(ptr, n)                          \
do {                                                               \
    if (ptr) {                                                     \
        cxl_free(ptr, (n) * sizeof(*(ptr)));                      \
        ptr = NULL;                                                \
    }                                                              \
} while (0)

// High-resolution timing functions (unchanged)
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

// Enhanced CLFLUSH implementation for CXL
static inline void clflush_memory(void *addr) {
    _mm_clflush(addr);
}

static void flush_memory_region(void *ptr, size_t size) {
    char *addr = (char*)ptr;
    for (size_t i = 0; i < size; i += CACHE_LINE_SIZE) {
        clflush_memory(addr + i);
    }
    // Flush last byte to ensure complete coverage
    if (size > 0) {
        clflush_memory(addr + size - 1);
    }
    // Memory fence to ensure flush completion - critical for CXL
    _mm_sfence();
    // Additional memory barrier for CXL consistency
    __asm__ volatile("mfence" ::: "memory");
}

// Random data generation functions (unchanged)
void generate_random_fid(struct lu_fid *fid) {
    fid->f_seq = rand() % 0xFFFFFFFFFFULL;
    fid->f_oid = rand() % 0x100000;
    fid->f_ver = rand() % 100;
}

void generate_random_inode_id(struct osd_inode_id *id) {
    id->oii_ino = rand() % 0x1000000;
    id->oii_gen = rand() % 0x10000;
}

void init_random_idmap_cache_entry(struct osd_idmap_cache *idc, int index) {
    generate_random_fid(&idc->oic_fid);
    generate_random_inode_id(&idc->oic_lid);
    idc->oic_remote = rand() % 2;

    printf("Entry %d initialized - FID: [%llu:%u:%u], Inode: %u/%u, Remote: %d\n",
           index,
           (unsigned long long)idc->oic_fid.f_seq,
           idc->oic_fid.f_oid,
           idc->oic_fid.f_ver,
           idc->oic_lid.oii_ino,
           idc->oic_lid.oii_gen,
           idc->oic_remote);
}

void test_cxl_obd_alloc_idmap_cache(int array_size) {
    printf("\n=== Testing CXL allocation of %d osd_idmap_cache entries ===\n", array_size);
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    uint64_t start_cycles = rdtsc_start();

    // Allocate using CXL memory
    struct osd_idmap_cache *idc_array = NULL;
    CXL_ALLOC_PTR_ARRAY_LARGE(idc_array, array_size);
    
    if (!idc_array) {
        fprintf(stderr, "CXL allocation failed!\n");
        return;
    }

    printf("Allocated osd_idmap_cache array of size %d at CXL address %p\n", 
           array_size, idc_array);
    uint64_t end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    long alloc_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    printf("CXL allocation took %lu cycles (%ld ns)\n", end_cycles - start_cycles, alloc_time_ns);

    // Initialize entries with random data
    printf("Initializing entries with random data in CXL memory...\n");
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();

    for (int i = 0; i < array_size; i++) {
        init_random_idmap_cache_entry(&idc_array[i], i);
    }

    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    long init_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    printf("CXL initialization completed in %lu cycles (%ld ns)\n", 
           end_cycles - start_cycles, init_time_ns);

    // Sample verification
    printf("Sample verification from CXL memory:\n");
    for (int i = 0; i < (array_size < 3 ? array_size : 3); i++) {
        struct osd_idmap_cache *entry = &idc_array[i];
        printf("  Entry %d: FID=[%llu:%u:%u], Inode=%u/%u, Remote=%d\n",
               i,
               (unsigned long long)entry->oic_fid.f_seq,
               entry->oic_fid.f_oid,
               entry->oic_fid.f_ver,
               entry->oic_lid.oii_ino,
               entry->oic_lid.oii_gen,
               entry->oic_remote);
    }

    // Flush CXL memory region - critical for CXL shared memory pools
    printf("Flushing CXL memory region (critical for CXL consistency)...\n");
    start_cycles = rdtsc_start();
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    flush_memory_region(idc_array, array_size * sizeof(struct osd_idmap_cache));
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    long flush_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    printf("CXL memory flushed in %ld ns (%lu cycles)\n", flush_time_ns, end_cycles - start_cycles);

    // Clean up (for this simple allocator, we just mark as freed)
    printf("Marking CXL memory as freed...\n");
    CXL_FREE_PTR_ARRAY_LARGE(idc_array, array_size);
    printf("Successfully completed CXL memory operations for %d entries\n", array_size);
}

void perform_cxl_memory_operations(int array_size) {
    printf("\n=== Performing CXL memory operations ===\n");

    // Simple CXL memory operation
    struct timespec start_simple, end_simple;
    uint64_t simple_start = rdtsc_start();
    clock_gettime(CLOCK_MONOTONIC, &start_simple);
    
    // Allocate a simple variable in CXL memory
    int *a_ptr = (int*)cxl_alloc(sizeof(int));
    if (a_ptr) {
        *a_ptr = 42;
        volatile int a_prime = *a_ptr;
        (void)a_prime;  // Use to avoid warning
        
        // Flush the CXL memory
        flush_memory_region(a_ptr, sizeof(int));
    }

    uint64_t simple_end = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end_simple);
    long simple_time_ns = (end_simple.tv_sec - start_simple.tv_sec) * 1e9 + 
                          (end_simple.tv_nsec - start_simple.tv_nsec);
    
    printf("Simple CXL memory operation completed in %ld ns (%lu cycles)\n", 
           simple_time_ns, simple_end - simple_start);

    // Complex CXL memory allocation
    printf("Performing complex CXL memory allocation...\n");
    struct timespec start_complex, end_complex;
    uint64_t complex_start = rdtsc_start();
    clock_gettime(CLOCK_MONOTONIC, &start_complex);
    
    // Allocate multiple blocks in CXL memory
    void *temp_ptrs[array_size];
    for (int i = 0; i < array_size; i++) {
        temp_ptrs[i] = cxl_alloc(1024);
        if (temp_ptrs[i]) {
            memset(temp_ptrs[i], 0xAA, 1024);
        }
    }

    uint64_t complex_end = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end_complex);
    long complex_time_ns = (end_complex.tv_sec - start_complex.tv_sec) * 1e9 + 
                           (end_complex.tv_nsec - start_complex.tv_nsec);
    printf("Complex CXL memory allocation completed in %ld ns (%lu cycles)\n", 
           complex_time_ns, complex_end - complex_start);

    // Flush all allocated CXL memory
    printf("Performing comprehensive CXL memory flushing...\n");
    struct timespec time_flush_start, time_flush_end;
    uint64_t flush_start = rdtsc_start();
    clock_gettime(CLOCK_MONOTONIC, &time_flush_start);
    
    // Flush each allocated block
    for (int i = 0; i < array_size; i++) {
        if (temp_ptrs[i]) {
            flush_memory_region(temp_ptrs[i], 1024);
        }
    }

    uint64_t flush_end = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &time_flush_end);
    long flush_time_ns = (time_flush_end.tv_sec - time_flush_start.tv_sec) * 1e9 + 
                         (time_flush_end.tv_nsec - time_flush_start.tv_nsec);
    printf("CXL memory comprehensive flush completed in %ld ns (%lu cycles)\n", 
           flush_time_ns, flush_end - flush_start);
    
    printf("CXL memory operations completed successfully.\n");
}

int main() {
    srand(time(NULL));
    int array_size = 10;
    
    printf("=== CXL Memory Benchmark ===\n");
    printf("Array size: %d entries\n", array_size);
    
    // Initialize CXL memory
    if (init_cxl_memory() != 0) {
        fprintf(stderr, "Failed to initialize CXL memory. Exiting.\n");
        return 1;
    }
    
    // Run CXL memory tests
    test_cxl_obd_alloc_idmap_cache(array_size);
    
    // Reset allocator for next test
    cxl_reset_allocator();
    
    perform_cxl_memory_operations(array_size);
    
    // Cleanup
    cleanup_cxl_memory();
    
    printf("\n=== CXL Memory Benchmark Complete ===\n");
    return 0;
}