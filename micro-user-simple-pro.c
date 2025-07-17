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
#define MAX_ARRAY_SIZES 10
#define RUNS_PER_SIZE 5

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

typedef enum {
    PATTERN_SEQUENTIAL,
    PATTERN_STRIDED,
    PATTERN_RANDOM
} access_pattern_t;

typedef struct {
    int array_size;
    access_pattern_t pattern;
    uint64_t alloc_cycles;
    uint64_t access_cycles;
    uint64_t flush_cycles;
    uint64_t free_cycles;
    long alloc_time_ns;
    long access_time_ns;
    long flush_time_ns;
    long free_time_ns;
} benchmark_result_t;

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
    clflush_memory(addr + size - 1);
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

void access_memory_pattern(struct osd_idmap_cache *array, int size, access_pattern_t pattern) {
    volatile uint64_t dummy = 0;
    
    switch (pattern) {
        case PATTERN_SEQUENTIAL:
            for (int i = 0; i < size; i++) {
                dummy += array[i].oic_fid.f_seq;
            }
            break;
            
        case PATTERN_STRIDED:
            // Access every 8th element, then wrap around
            for (int stride = 0; stride < 8 && stride < size; stride++) {
                for (int i = stride; i < size; i += 8) {
                    dummy += array[i].oic_fid.f_seq;
                }
            }
            break;
            
        case PATTERN_RANDOM:
            // Pseudo-random access pattern
            for (int i = 0; i < size; i++) {
                int idx = (i * 17 + 23) % size;  // Simple pseudo-random
                dummy += array[idx].oic_fid.f_seq;
            }
            break;
    }
    
    // Prevent compiler optimization
    (void)dummy;
}

benchmark_result_t run_benchmark(int array_size, access_pattern_t pattern) {
    benchmark_result_t result = {0};
    result.array_size = array_size;
    result.pattern = pattern;
    
    struct timespec start, end;
    uint64_t start_cycles, end_cycles;
    
    // Allocation phase
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    struct osd_idmap_cache *idc_array = NULL;
    SIMPLE_ALLOC_PTR_ARRAY_LARGE(idc_array, array_size);
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    if (!idc_array) {
        fprintf(stderr, "Allocation failed for size %d!\n", array_size);
        return result;
    }
    
    result.alloc_cycles = end_cycles - start_cycles;
    result.alloc_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    
    // Initialize array
    for (int i = 0; i < array_size; i++) {
        init_random_idmap_cache_entry(&idc_array[i]);
    }
    
    // Access phase
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    access_memory_pattern(idc_array, array_size, pattern);
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    result.access_cycles = end_cycles - start_cycles;
    result.access_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    
    // Flush phase
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    flush_memory_region(idc_array, array_size * sizeof(struct osd_idmap_cache));
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    result.flush_cycles = end_cycles - start_cycles;
    result.flush_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    
    // Free phase
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    free(idc_array);
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    result.free_cycles = end_cycles - start_cycles;
    result.free_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    
    return result;
}

const char* pattern_name(access_pattern_t pattern) {
    switch (pattern) {
        case PATTERN_SEQUENTIAL: return "sequential";
        case PATTERN_STRIDED: return "strided";
        case PATTERN_RANDOM: return "random";
        default: return "unknown";
    }
}

void save_results_csv(benchmark_result_t *results, int num_results, const char *filename) {
    FILE *fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "Failed to open %s for writing\n", filename);
        return;
    }
    
    // Write CSV header
    fprintf(fp, "array_size,pattern,alloc_cycles,access_cycles,flush_cycles,free_cycles,"
               "alloc_time_ns,access_time_ns,flush_time_ns,free_time_ns\n");
    
    // Write data
    for (int i = 0; i < num_results; i++) {
        benchmark_result_t *r = &results[i];
        fprintf(fp, "%d,%s,%lu,%lu,%lu,%lu,%ld,%ld,%ld,%ld\n",
                r->array_size, pattern_name(r->pattern),
                r->alloc_cycles, r->access_cycles, r->flush_cycles, r->free_cycles,
                r->alloc_time_ns, r->access_time_ns, r->flush_time_ns, r->free_time_ns);
    }
    
    fclose(fp);
    printf("Results saved to %s\n", filename);
}

int main() {
    srand(time(NULL));
    
    // Array sizes to test: 1, 10, 100, 1000, 10000
    int array_sizes[] = {1, 10, 50, 100, 500, 1000, 5000, 10000};
    int num_sizes = sizeof(array_sizes) / sizeof(array_sizes[0]);
    
    access_pattern_t patterns[] = {PATTERN_SEQUENTIAL, PATTERN_STRIDED, PATTERN_RANDOM};
    int num_patterns = sizeof(patterns) / sizeof(patterns[0]);
    
    // Calculate total number of benchmark runs
    int total_results = num_sizes * num_patterns * RUNS_PER_SIZE;
    benchmark_result_t *results = malloc(total_results * sizeof(benchmark_result_t));
    
    if (!results) {
        fprintf(stderr, "Failed to allocate results array\n");
        return 1;
    }
    
    int result_index = 0;
    
    printf("Starting micro-benchmark with %d array sizes, %d patterns, %d runs per configuration\n",
           num_sizes, num_patterns, RUNS_PER_SIZE);
    
    for (int s = 0; s < num_sizes; s++) {
        for (int p = 0; p < num_patterns; p++) {
            printf("\nTesting array size %d with %s pattern:\n", 
                   array_sizes[s], pattern_name(patterns[p]));
            
            for (int run = 0; run < RUNS_PER_SIZE; run++) {
                printf("  Run %d/%d... ", run + 1, RUNS_PER_SIZE);
                fflush(stdout);
                
                benchmark_result_t result = run_benchmark(array_sizes[s], patterns[p]);
                results[result_index++] = result;
                
                printf("Done (alloc: %lu cycles, access: %lu cycles)\n", 
                       result.alloc_cycles, result.access_cycles);
            }
        }
    }
    
    // Save results
    save_results_csv(results, total_results, "benchmark_results.csv");
    
    // Print summary
    printf("\nBenchmark completed successfully!\n");
    printf("Total runs: %d\n", total_results);
    printf("Results saved to benchmark_results.csv\n");
    
    free(results);
    return 0;
}