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
#include <pthread.h>
#include <sys/mman.h>
#include <sched.h>

#define _GNU_SOURCE
#include <sys/stat.h>

#define CACHE_LINE_SIZE 64
#define MEMORY_SIZE (1024 * 1024)  // 1MB test memory
#define MAX_THREADS 16
#define MAX_ITERATIONS 1000

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

// Test result structure
struct test_result {
    char test_name[64];
    int array_size;
    long alloc_time_ns;
    long init_time_ns;
    long access_time_ns;
    long flush_time_ns;
    long free_time_ns;
    uint64_t alloc_cycles;
    uint64_t access_cycles;
    uint64_t flush_cycles;
    uint64_t free_cycles;
    size_t memory_used;
    double throughput_mb_s;
    int success;
};

// Thread data structure
struct thread_data {
    int thread_id;
    int iterations;
    int array_size;
    struct test_result *results;
    pthread_barrier_t *barrier;
};

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
    if (size > 0) {
        clflush_memory(addr + size - 1);
    }
    _mm_sfence();
}

// Logging functions
void log_test_start(const char *test_name, int array_size) {
    printf("=== BENCHMARK_START: %s SIZE:%d TIME:%ld ===\n", 
           test_name, array_size, time(NULL));
}

void log_test_end(const char *test_name, struct test_result *result) {
    printf("=== BENCHMARK_END: %s SUCCESS:%d ALLOC_NS:%ld INIT_NS:%ld ACCESS_NS:%ld FLUSH_NS:%ld FREE_NS:%ld ALLOC_CYCLES:%lu ACCESS_CYCLES:%lu FLUSH_CYCLES:%lu FREE_CYCLES:%lu MEMORY_MB:%.2f THROUGHPUT_MB_S:%.2f ===\n",
           test_name, result->success, result->alloc_time_ns, result->init_time_ns, 
           result->access_time_ns, result->flush_time_ns, result->free_time_ns,
           result->alloc_cycles, result->access_cycles, result->flush_cycles, result->free_cycles,
           result->memory_used / (1024.0 * 1024.0), result->throughput_mb_s);
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
    generate_random_fid(&idc->oic_fid);
    generate_random_inode_id(&idc->oic_lid);
    idc->oic_remote = rand() % 2;
}

// Enhanced test function with comprehensive metrics
struct test_result test_lustre_allocation(int array_size, int access_pattern) {
    struct test_result result = {0};
    struct timespec start, end;
    struct osd_idmap_cache *idc_array = NULL;
    
    snprintf(result.test_name, sizeof(result.test_name), "lustre_alloc_pattern_%d", access_pattern);
    result.array_size = array_size;
    result.memory_used = array_size * sizeof(struct osd_idmap_cache);
    
    log_test_start(result.test_name, array_size);
    
    // 1. Allocation phase
    clock_gettime(CLOCK_MONOTONIC, &start);
    uint64_t start_cycles = rdtsc_start();
    
    SIMPLE_ALLOC_PTR_ARRAY_LARGE(idc_array, array_size);
    
    uint64_t end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    if (!idc_array) {
        printf("ERROR: Allocation failed for size %d\n", array_size);
        result.success = 0;
        return result;
    }
    
    result.alloc_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    result.alloc_cycles = end_cycles - start_cycles;
    
    // 2. Initialization phase
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < array_size; i++) {
        init_random_idmap_cache_entry(&idc_array[i], i);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    result.init_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    
    // 3. Access phase (different patterns)
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    volatile uint64_t checksum = 0;
    switch (access_pattern) {
        case 0: // Sequential access
            for (int i = 0; i < array_size; i++) {
                checksum += idc_array[i].oic_fid.f_seq;
            }
            break;
        case 1: // Random access
            for (int i = 0; i < array_size; i++) {
                int idx = rand() % array_size;
                checksum += idc_array[idx].oic_fid.f_seq;
            }
            break;
        case 2: // Strided access
            for (int i = 0; i < array_size; i += 8) {
                checksum += idc_array[i].oic_fid.f_seq;
            }
            break;
    }
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    result.access_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    result.access_cycles = end_cycles - start_cycles;
    
    // 4. Flush phase
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    flush_memory_region(idc_array, result.memory_used);
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    result.flush_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    result.flush_cycles = end_cycles - start_cycles;
    
    // 5. Free phase
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    SIMPLE_FREE_PTR_ARRAY_LARGE(idc_array, array_size);
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    result.free_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    result.free_cycles = end_cycles - start_cycles;
    
    // Calculate throughput
    long total_time_ns = result.alloc_time_ns + result.init_time_ns + result.access_time_ns + result.flush_time_ns + result.free_time_ns;
    result.throughput_mb_s = (result.memory_used / (1024.0 * 1024.0)) / (total_time_ns / 1e9);
    
    result.success = 1;
    log_test_end(result.test_name, &result);
    
    return result;
}

// Regular malloc comparison test
struct test_result test_regular_allocation(int array_size, int access_pattern) {
    struct test_result result = {0};
    struct timespec start, end;
    struct osd_idmap_cache *idc_array = NULL;
    
    snprintf(result.test_name, sizeof(result.test_name), "regular_alloc_pattern_%d", access_pattern);
    result.array_size = array_size;
    result.memory_used = array_size * sizeof(struct osd_idmap_cache);
    
    log_test_start(result.test_name, array_size);
    
    // 1. Allocation phase
    clock_gettime(CLOCK_MONOTONIC, &start);
    uint64_t start_cycles = rdtsc_start();
    
    idc_array = malloc(array_size * sizeof(struct osd_idmap_cache));
    if (idc_array) {
        memset(idc_array, 0, result.memory_used);
    }
    
    uint64_t end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    if (!idc_array) {
        printf("ERROR: Regular allocation failed for size %d\n", array_size);
        result.success = 0;
        return result;
    }
    
    result.alloc_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    result.alloc_cycles = end_cycles - start_cycles;
    
    // 2. Initialization phase
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < array_size; i++) {
        init_random_idmap_cache_entry(&idc_array[i], i);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    result.init_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    
    // 3. Access phase (same patterns as Lustre test)
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    volatile uint64_t checksum = 0;
    switch (access_pattern) {
        case 0: // Sequential access
            for (int i = 0; i < array_size; i++) {
                checksum += idc_array[i].oic_fid.f_seq;
            }
            break;
        case 1: // Random access
            for (int i = 0; i < array_size; i++) {
                int idx = rand() % array_size;
                checksum += idc_array[idx].oic_fid.f_seq;
            }
            break;
        case 2: // Strided access
            for (int i = 0; i < array_size; i += 8) {
                checksum += idc_array[i].oic_fid.f_seq;
            }
            break;
    }
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    result.access_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    result.access_cycles = end_cycles - start_cycles;
    
    // 4. Flush phase
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    flush_memory_region(idc_array, result.memory_used);
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    result.flush_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    result.flush_cycles = end_cycles - start_cycles;
    
    // 5. Free phase
    clock_gettime(CLOCK_MONOTONIC, &start);
    start_cycles = rdtsc_start();
    
    free(idc_array);
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end);
    result.free_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    result.free_cycles = end_cycles - start_cycles;
    
    // Calculate throughput
    long total_time_ns = result.alloc_time_ns + result.init_time_ns + result.access_time_ns + result.flush_time_ns + result.free_time_ns;
    result.throughput_mb_s = (result.memory_used / (1024.0 * 1024.0)) / (total_time_ns / 1e9);
    
    result.success = 1;
    log_test_end(result.test_name, &result);
    
    return result;
}

// Multi-threaded test
void* threaded_allocation_test(void *arg) {
    struct thread_data *data = (struct thread_data*)arg;
    
    // Wait for all threads to be ready
    pthread_barrier_wait(data->barrier);
    
    for (int i = 0; i < data->iterations; i++) {
        struct test_result result = test_lustre_allocation(data->array_size, 0);
        if (data->results) {
            data->results[data->thread_id * data->iterations + i] = result;
        }
    }
    
    return NULL;
}

void run_multithreaded_test(int num_threads, int iterations, int array_size) {
    printf("=== MULTITHREADED_TEST_START: THREADS:%d ITERATIONS:%d SIZE:%d ===\n", 
           num_threads, iterations, array_size);
    
    pthread_t threads[MAX_THREADS];
    struct thread_data thread_data[MAX_THREADS];
    pthread_barrier_t barrier;
    
    struct test_result *all_results = malloc(num_threads * iterations * sizeof(struct test_result));
    
    pthread_barrier_init(&barrier, NULL, num_threads);
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // Create threads
    for (int i = 0; i < num_threads; i++) {
        thread_data[i].thread_id = i;
        thread_data[i].iterations = iterations;
        thread_data[i].array_size = array_size;
        thread_data[i].results = all_results;
        thread_data[i].barrier = &barrier;
        
        pthread_create(&threads[i], NULL, threaded_allocation_test, &thread_data[i]);
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    long total_time_ns = (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    
    // Calculate statistics
    long total_alloc_time = 0, total_access_time = 0, total_flush_time = 0, total_free_time = 0;
    int successful_tests = 0;
    
    for (int i = 0; i < num_threads * iterations; i++) {
        if (all_results[i].success) {
            total_alloc_time += all_results[i].alloc_time_ns;
            total_access_time += all_results[i].access_time_ns;
            total_flush_time += all_results[i].flush_time_ns;
            total_free_time += all_results[i].free_time_ns;
            successful_tests++;
        }
    }
    
    printf("=== MULTITHREADED_TEST_END: SUCCESS_RATE:%.2f%% TOTAL_TIME_NS:%ld AVG_ALLOC_NS:%ld AVG_ACCESS_NS:%ld AVG_FLUSH_NS:%ld AVG_FREE_NS:%ld ===\n",
           (successful_tests * 100.0) / (num_threads * iterations), total_time_ns,
           total_alloc_time / successful_tests, total_access_time / successful_tests,
           total_flush_time / successful_tests, total_free_time / successful_tests);
    
    pthread_barrier_destroy(&barrier);
    free(all_results);
}

// Stress test with varying sizes
void run_stress_test(int min_size, int max_size, int num_iterations) {
    printf("=== STRESS_TEST_START: MIN_SIZE:%d MAX_SIZE:%d ITERATIONS:%d ===\n", 
           min_size, max_size, num_iterations);
    
    for (int i = 0; i < num_iterations; i++) {
        int size = min_size + (rand() % (max_size - min_size + 1));
        int pattern = rand() % 3;
        
        struct test_result lustre_result = test_lustre_allocation(size, pattern);
        struct test_result regular_result = test_regular_allocation(size, pattern);
        
        double lustre_total = lustre_result.alloc_time_ns + lustre_result.access_time_ns + 
                             lustre_result.flush_time_ns + lustre_result.free_time_ns;
        double regular_total = regular_result.alloc_time_ns + regular_result.access_time_ns + 
                              regular_result.flush_time_ns + regular_result.free_time_ns;
        
        printf("=== STRESS_ITERATION:%d SIZE:%d PATTERN:%d LUSTRE_TOTAL_NS:%.0f REGULAR_TOTAL_NS:%.0f RATIO:%.2f ===\n",
               i, size, pattern, lustre_total, regular_total, 
               regular_total > 0 ? lustre_total / regular_total : 0.0);
    }
    
    printf("=== STRESS_TEST_END ===\n");
}

int main(int argc, char *argv[]) {
    printf("=== LUSTRE_MEMORY_BENCHMARK_START: PID:%d TIME:%ld ===\n", getpid(), time(NULL));
    
    // Seed random number generator
    srand(time(NULL));
    
    // Test different array sizes
    int test_sizes[] = {100, 1000, 10000, 100000};
    int num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    // Run basic comparison tests
    printf("\n=== BASIC_COMPARISON_TESTS ===\n");
    for (int i = 0; i < num_sizes; i++) {
        for (int pattern = 0; pattern < 3; pattern++) {
            struct test_result lustre_result = test_lustre_allocation(test_sizes[i], pattern);
            struct test_result regular_result = test_regular_allocation(test_sizes[i], pattern);
            
            printf("COMPARISON: SIZE:%d PATTERN:%d LUSTRE_ALLOC_NS:%ld REGULAR_ALLOC_NS:%ld RATIO:%.2f\n",
                   test_sizes[i], pattern, lustre_result.alloc_time_ns, regular_result.alloc_time_ns,
                   regular_result.alloc_time_ns > 0 ? (double)lustre_result.alloc_time_ns / regular_result.alloc_time_ns : 0.0);
        }
    }
    
    // Run multi-threaded test
    printf("\n=== MULTITHREADED_TESTS ===\n");
    run_multithreaded_test(4, 10, 1000);
    run_multithreaded_test(8, 5, 5000);
    
    // Run stress test
    printf("\n=== STRESS_TESTS ===\n");
    run_stress_test(10, 50000, 20);
    
    printf("=== LUSTRE_MEMORY_BENCHMARK_END: PID:%d TIME:%ld ===\n", getpid(), time(NULL));
    
    return 0;
}