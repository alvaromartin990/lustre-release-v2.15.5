#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
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
        if (is_vmalloc_addr(ptr))                                  \
            vfree(ptr);                                            \
        else                                                       \
            kfree(ptr);                                            \
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
    // Flush last byte to ensure complete coverage
    clflush_memory(addr + size - 1);
    // Memory fence to ensure flush completion
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

void init_random_idmap_cache_entry(struct osd_idmap_cache *idc, int index) {
    /**
    This function initializes a single osd_idmap_cache entry with random data.

    Args:
        idc (struct osd_idmap_cache*): Pointer to the cache entry to initialize.
        index (int): Index of the entry for logging purposes.
    **/
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

void test_obd_alloc_idmap_cache(int array_size) {
    /*** 
    This function tests the allocation of an array of osd_idmap_cache entries,
    initializes them with random data, and measures the time taken for allocation,
    initialization, and deallocation.

    This function simulates the usage of the OBD_ALLOC_PTR_ARRAY_LARGE macro
    to allocate a large array of osd_idmap_cache structures, similar to what is done
    in the Lustre OSD handler code. However, this is intended to work in a user-space context
    for testing purposes.
    ***/
    printf("\nTesting allocation of %d osd_idmap_cache entries\n", array_size);
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    // let's also use rdtsc_start and rdtsc_end for high-resolution timing
    uint64_t start_cycles = rdtsc_start();

    //struct osd_idmap_cache *idc_array = malloc(array_size * sizeof(struct osd_idmap_cache));
    // using our simplified allocation macro SIMPLE_FREE_PTR_ARRAY_LARGE
    struct osd_idmap_cache *idc_array = NULL;
    SIMPLE_ALLOC_PTR_ARRAY_LARGE(idc_array, array_size);
    
    if (!idc_array) {
        fprintf(stderr, "Allocation failed!\n");
        return;
    }

    printf("Allocated osd_idmap_cache array of size %d at %p\n", array_size, idc_array);
    uint64_t end_cycles = rdtsc_end();
    printf("Allocation took %lu cycles\n", end_cycles - start_cycles);

    // Initialize entries with random data
    printf("Initializing entries with random data...\n");

    for (int i = 0; i < array_size; i++) {
        init_random_idmap_cache_entry(&idc_array[i], i);
    }

    printf("Initialization complete.\n");

    clock_gettime(CLOCK_MONOTONIC, &end);
    long alloc_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    printf("Successfully allocated %d entries in %ld ns\n", array_size, alloc_time_ns);

    printf("Sample verification:\n");
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

    // Now, let's flush the memory region
    printf("Flushing memory region...\n");
    start_cycles = rdtsc_start();
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    flush_memory_region(idc_array, array_size * sizeof(struct osd_idmap_cache));
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    long flush_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    printf("Memory flushed in %ld ns\n", flush_time_ns);
    printf("Just flushed osd_idmap_cache array at %p\n", idc_array);
    printf("Memory flush completed in %lu cycles.\n", end_cycles - start_cycles);

    printf("Proceeding to free the allocated memory...\n");
    clock_gettime(CLOCK_MONOTONIC, &start);
    free(idc_array);
    clock_gettime(CLOCK_MONOTONIC, &end);
    long free_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    printf("Successfully freed %d entries in %ld ns\n", array_size, free_time_ns);
}

void perform_simple_memory_operations(int array_size) {
    // print about to perform simple memory operation
    printf("Performing simple memory operations...\n");

    struct timespec start_simple, end_simple;

    // add a timer for that
    uint64_t simple_start = rdtsc_start();
    clock_gettime(CLOCK_MONOTONIC, &start_simple);
    
    // Variable allocation and assignment
    int a = 0;
    volatile int *a_ptr = &a;
    *a_ptr = 42;  // a = 42
    volatile int a_prime = *a_ptr;  // a' = a
    
    // Use a_prime to avoid compiler warning
    (void)a_prime;

    uint64_t simple_end = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &start_simple);
    long simple_time_ns = (end_simple.tv_sec - start_simple.tv_sec) * 1e9 + (end_simple.tv_nsec - start_simple.tv_nsec);
    
    printf("Simple memory operation completed in %ld ns\n", simple_time_ns);
    printf("Simple memory operation completed in %lu cycles.\n", simple_end - simple_start);

    // print about to perform complex memory allocation
    printf("Performing complex memory allocation...\n");

    // time complex memory allocation
    struct timespec start_complex, end_complex;
    uint64_t complex_start = rdtsc_start();
    clock_gettime(CLOCK_MONOTONIC, &start_complex);
    
    // Additional memory operations to stress allocation patterns
    void *temp_ptrs[10];
    for (int i = 0; i < 10; i++) {
        temp_ptrs[i] = malloc(1024);
        memset(temp_ptrs[i], 0xAA, 1024);
    }

    uint64_t complex_end = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end_complex);
    long complex_time_ns = (end_complex.tv_sec - start_complex.tv_sec) * 1e9 + (end_complex.tv_nsec - start_complex.tv_nsec);
    printf("Complex memory allocation completed in %ld ns\n", complex_time_ns);
    printf("Complex memory allocation completed in %lu cycles.\n", complex_end - complex_start);

    // let's print a message to indicate we are about to flush memory
    printf("Performing flushing...\n");

    // add a timer to see how long it takes to perform the flush
    struct timespec time_flush_start, time_flush_end;
    uint64_t flush_start = rdtsc_start();
    clock_gettime(CLOCK_MONOTONIC, &time_flush_start);
    
    // CLFLUSH operations
    flush_memory_region(temp_ptrs, sizeof(temp_ptrs));
    for (int i = 0; i < 10; i++) {
        flush_memory_region(temp_ptrs[i], 1024);
    }

    uint64_t flush_end = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &time_flush_end);
    long flush_time_ns = (time_flush_end.tv_sec - time_flush_start.tv_sec) * 1e9 + (time_flush_end.tv_nsec - time_flush_start.tv_nsec);
    printf("Memory flushed in %ld ns\n", flush_time_ns);
    printf("Memory flush completed in %lu cycles.\n", flush_end - flush_start);
    
    // Clean up
    for (int i = 0; i < 10; i++) {
        free(temp_ptrs[i]);
    }
}

void write_idmap_cache_to_file(const char *filename, struct osd_idmap_cache *array, int count) {
    FILE *fp = fopen(filename, "wb");
    struct timespec start, end;

    if (!fp) {
        printf("Segmentation fault: fopen failed\n");
        perror("fopen");
        return;
    }

    uint64_t flush_start = rdtsc_start();
    clock_gettime(CLOCK_MONOTONIC, &start);

    printf("About to write");

    fwrite(array, sizeof(struct osd_idmap_cache), count, fp);
    fclose(fp);

    uint64_t flush_end = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    long flush_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    printf("Data written to %s in %ld ns\n", filename, flush_time_ns);
    printf("File write completed in %lu cycles.\n", flush_end - flush_start);
    printf("Wrote %d entries to %s\n", count, filename);
}

int read_idmap_cache_from_file(const char *filename, struct osd_idmap_cache **array_out) {
    FILE *fp = fopen(filename, "rb");
    struct timespec start, end;
    
    if (!fp) {
        perror("fopen");
        return -1;
    }

    uint64_t flush_start = rdtsc_start();
    clock_gettime(CLOCK_MONOTONIC, &start);

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    int count = size / sizeof(struct osd_idmap_cache);
    *array_out = malloc(size);
    if (!*array_out) {
        perror("malloc");
        fclose(fp);
        return -1;
    }

    fread(*array_out, sizeof(struct osd_idmap_cache), count, fp);
    fclose(fp);

    uint64_t flush_end = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    long flush_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    printf("Data read from %s in %ld ns\n", filename, flush_time_ns);
    printf("File read completed in %lu cycles.\n", flush_end - flush_start);

    printf("Read %d entries from %s\n", count, filename);
    return count;
}



int main() {
    srand(time(NULL));
    test_obd_alloc_idmap_cache(10);
    perform_simple_memory_operations(10);

    printf("Testing file operations...\n");

    int array_size = 10;

    printf("Creating idmap cache array of size %d\n", array_size);
    struct osd_idmap_cache *idc_array = NULL;

    // Test writing and reading idmap cache to/from file
    printf("Writing idmap cache to file...\n");
    write_idmap_cache_to_file("idmap_cache.bin", idc_array, array_size);
    
    struct osd_idmap_cache *loaded_array = NULL;
    int loaded_count = read_idmap_cache_from_file("idmap_cache.bin", &loaded_array);
    if (loaded_count > 0) {
        // Optionally print or verify loaded_array
        free(loaded_array);
    }

    return 0;
}
