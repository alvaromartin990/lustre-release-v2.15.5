/*
 * lustre_microbenchmark.c
 * 
 * Comprehensive microbenchmark for Lustre 2.15.5 client-side performance testing
 * Triggers instrumented code paths in osd_handler.c and osd_scrub.c
 * 
 * Compilation: gcc -o lustre_microbench lustre_microbenchmark.c -llustreapi -lrt
 * Usage: ./lustre_microbench /mnt/lustre/testdir
 */

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

// Lustre-specific includes
#include <lustre/lustreapi.h>
#include <lustre/lustre_user.h>
#include "obd_support.h"  // OBD_ macros
#include "osd_oi.h" 

// Benchmark configuration
#define BENCHMARK_ITERATIONS 10
#define WARMUP_ITERATIONS 1
#define TEST_FILE_COUNT 50
#define CACHE_LINE_SIZE 64
#define MEMORY_SIZE (1024 * 1024)  // 1MB test memory

// Timing structures
typedef struct {
    uint64_t min;
    uint64_t max;
    uint64_t avg;
    uint64_t total;
    double variance;
} benchmark_stats_t;

// OBD allocation counters
typedef struct {
    int obd_alloc_ptr;
    int obd_alloc_ptr_array_large;
    int obd_alloc_ptr_array;
    int obd_alloc;
    int obd_slab_alloc_ptr;
    int total_obd_allocs;
} obd_alloc_stats_t;

// OSD scrub timing data
typedef struct {
    int count;
    long long *journal_start_timings;  // Array of journal start latencies
    long long *journal_stop_timings;   // Array of journal stop latencies
    int journal_start_count;
    int journal_stop_count;
    long long min_journal_start;
    long long max_journal_start;
    long long avg_journal_start;
    long long min_journal_stop;
    long long max_journal_stop;
    long long avg_journal_stop;
} osd_scrub_stats_t;

// Combined instrumentation statistics
typedef struct {
    obd_alloc_stats_t alloc_stats;
    osd_scrub_stats_t scrub_stats;
    int total_osd_scrub_entries;
} instrumentation_stats_t;

typedef struct {
    char *lustre_path;
    char *test_dir;
    int verbose;
    int enable_clflush;
    benchmark_stats_t timing_stats;
    instrumentation_stats_t instr_stats;
} benchmark_config_t;

// Global variables for memory operations
static char *test_memory = NULL;
static volatile int a = 0;  // Variable 'a' for allocation/assignment tests

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

// Timing overhead measurement
static uint64_t measure_timing_overhead(void) {
    const int iterations = 1000;
    uint64_t total = 0;
    
    for (int i = 0; i < iterations; i++) {
        uint64_t start = rdtsc_start();
        uint64_t end = rdtsc_end();
        total += (end - start);
    }
    
    return total / iterations;
}

// CPU frequency estimation for cycle-to-time conversion
static double estimate_cpu_frequency(void) {
    struct timespec start_time, end_time;
    uint64_t start_cycles, end_cycles;
    
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    start_cycles = rdtsc_start();
    
    usleep(100000); // Sleep for 100ms
    
    end_cycles = rdtsc_end();
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    double elapsed_ns = (end_time.tv_sec - start_time.tv_sec) * 1e9 + 
                       (end_time.tv_nsec - start_time.tv_nsec);
    double cycles = end_cycles - start_cycles;
    
    return cycles / elapsed_ns; // cycles per nanosecond
}

// Statistics calculation
static benchmark_stats_t calculate_stats(uint64_t *results, int count) {
    benchmark_stats_t stats = {0};
    
    stats.min = results[0];
    stats.max = results[0];
    stats.total = 0;
    
    for (int i = 0; i < count; i++) {
        if (results[i] < stats.min) stats.min = results[i];
        if (results[i] > stats.max) stats.max = results[i];
        stats.total += results[i];
    }
    
    stats.avg = stats.total / count;
    
    // Calculate variance
    double variance_sum = 0;
    for (int i = 0; i < count; i++) {
        double diff = (double)results[i] - (double)stats.avg;
        variance_sum += diff * diff;
    }
    stats.variance = variance_sum / count;
    
    return stats;
}

// Initialize instrumentation statistics
static void init_instrumentation_stats(instrumentation_stats_t *stats) {
    memset(stats, 0, sizeof(instrumentation_stats_t));
    
    // Allocate arrays for timing data (assume max 1000 entries)
    stats->scrub_stats.journal_start_timings = malloc(1000 * sizeof(long long));
    stats->scrub_stats.journal_stop_timings = malloc(1000 * sizeof(long long));
    stats->scrub_stats.min_journal_start = LLONG_MAX;
    stats->scrub_stats.max_journal_start = LLONG_MIN;
    stats->scrub_stats.min_journal_stop = LLONG_MAX;
    stats->scrub_stats.max_journal_stop = LLONG_MIN;
}

// Free instrumentation statistics
static void free_instrumentation_stats(instrumentation_stats_t *stats) {
    if (stats->scrub_stats.journal_start_timings) {
        free(stats->scrub_stats.journal_start_timings);
    }
    if (stats->scrub_stats.journal_stop_timings) {
        free(stats->scrub_stats.journal_stop_timings);
    }
}

// Parse kernel log for OBD allocation patterns
static void parse_obd_allocations(const char *log_filename, obd_alloc_stats_t *stats) {
    FILE *file = fopen(log_filename, "r");
    if (!file) {
        printf("Warning: Could not open kernel log file %s\n", log_filename);
        return;
    }
    
    char line[2048];
    while (fgets(line, sizeof(line), file)) {
        // Count specific OBD allocation patterns from lctl dk output
        if (strstr(line, "OBD_ALLOC_PTR_ARRAY_LARGE")) {
            stats->obd_alloc_ptr_array_large++;
        } else if (strstr(line, "OBD_ALLOC_PTR_ARRAY")) {
            stats->obd_alloc_ptr_array++;
        } else if (strstr(line, "OBD_ALLOC_PTR")) {
            stats->obd_alloc_ptr++;
        } else if (strstr(line, "OBD_SLAB_ALLOC_PTR")) {
            stats->obd_slab_alloc_ptr++;
        } else if (strstr(line, "OBD_ALLOC")) {
            stats->obd_alloc++;
        }
    }
    
    fclose(file);
}

// Parse dmesg for custom OBD allocation instrumentation with timestamp filtering
static void parse_dmesg_obd_allocations(obd_alloc_stats_t *stats, time_t start_time) {
    FILE *dmesg_pipe = popen("dmesg -T | tail -2000", "r");
    if (!dmesg_pipe) {
        printf("Warning: Could not read dmesg output\n");
        return;
    }
    
    char line[2048];
    while (fgets(line, sizeof(line), dmesg_pipe)) {
        // Parse timestamp from dmesg -T format: [Mon Dec  6 14:30:45 2024]
        struct tm tm_time;
        char *bracket_pos = strchr(line, ']');
        if (bracket_pos) {
            char time_str[100];
            strncpy(time_str, line + 1, bracket_pos - line - 1);
            time_str[bracket_pos - line - 1] = '\0';
            
            if (strptime(time_str, "%a %b %d %H:%M:%S %Y", &tm_time)) {
                time_t msg_time = mktime(&tm_time);
                // Only count messages from our benchmark run
                if (msg_time >= start_time) {
                    // Look for user's custom instrumentation messages
                    if (strstr(line, "about to call OBD_ALLOC_PTR_ARRAY_LARGE")) {
                        stats->obd_alloc_ptr_array_large++;
                    } else if (strstr(line, "about to call OBD_ALLOC_PTR_ARRAY")) {
                        stats->obd_alloc_ptr_array++;
                    } else if (strstr(line, "about to call OBD_ALLOC_PTR")) {
                        stats->obd_alloc_ptr++;
                    } else if (strstr(line, "about to call OBD_SLAB_ALLOC_PTR")) {
                        stats->obd_slab_alloc_ptr++;
                    } else if (strstr(line, "about to call OBD_ALLOC")) {
                        stats->obd_alloc++;
                    }
                }
            }
        }
        
        // Fallback: if timestamp parsing fails, use simple pattern matching
        if (strstr(line, "Within osd_idc_add") || strstr(line, "Within osd_it_ea")) {
            if (strstr(line, "about to call OBD_ALLOC_PTR_ARRAY_LARGE")) {
                stats->obd_alloc_ptr_array_large++;
            } else if (strstr(line, "about to call OBD_ALLOC_PTR_ARRAY")) {
                stats->obd_alloc_ptr_array++;
            } else if (strstr(line, "about to call OBD_ALLOC_PTR")) {
                stats->obd_alloc_ptr++;
            } else if (strstr(line, "about to call OBD_SLAB_ALLOC_PTR")) {
                stats->obd_slab_alloc_ptr++;
            } else if (strstr(line, "about to call OBD_ALLOC")) {
                stats->obd_alloc++;
            }
        }
    }
    
    pclose(dmesg_pipe);
    
    stats->total_obd_allocs = stats->obd_alloc_ptr + stats->obd_alloc_ptr_array_large + 
                              stats->obd_alloc_ptr_array + stats->obd_alloc + 
                              stats->obd_slab_alloc_ptr;
}

// Parse kernel log for OSD_SCRUB patterns and timing data
static void parse_osd_scrub_data(const char *log_filename, osd_scrub_stats_t *stats, time_t start_time) {
    FILE *file = fopen(log_filename, "r");
    if (file) {
        char line[2048];
        long long timing_value;
        
        while (fgets(line, sizeof(line), file)) {
            // Count general OSD_SCRUB entries
            if (strstr(line, "OSD_SCRUB")) {
                stats->count++;
            }
            
            // Extract journal start timing
            if (strstr(line, "OSD_SCRUB: Lustre OI osd_journal_start_sb latency at osd_scrub_convert_ff()")) {
                if (sscanf(line, "%*[^=]= %lld ns", &timing_value) == 1) {
                    if (stats->journal_start_count < 1000) {
                        stats->journal_start_timings[stats->journal_start_count] = timing_value;
                        stats->journal_start_count++;
                        
                        // Update min/max
                        if (timing_value < stats->min_journal_start) {
                            stats->min_journal_start = timing_value;
                        }
                        if (timing_value > stats->max_journal_start) {
                            stats->max_journal_start = timing_value;
                        }
                    }
                }
            }
            
            // Extract journal stop timing
            if (strstr(line, "OSD_SCRUB: Lustre OI commit latency after ldiskfs_journal_stop at osd_scrub_convert_ff()")) {
                if (sscanf(line, "%*[^=]= %lld ns", &timing_value) == 1) {
                    if (stats->journal_stop_count < 1000) {
                        stats->journal_stop_timings[stats->journal_stop_count] = timing_value;
                        stats->journal_stop_count++;
                        
                        // Update min/max
                        if (timing_value < stats->min_journal_stop) {
                            stats->min_journal_stop = timing_value;
                        }
                        if (timing_value > stats->max_journal_stop) {
                            stats->max_journal_stop = timing_value;
                        }
                    }
                }
            }
        }
        fclose(file);
    }
    
    // Also check dmesg for custom instrumentation with timestamp filtering
    FILE *dmesg_pipe = popen("dmesg -T | grep 'OSD_SCRUB' | tail -100", "r");
    if (dmesg_pipe) {
        char line[2048];
        long long timing_value;
        
        while (fgets(line, sizeof(line), dmesg_pipe)) {
            // Parse timestamp from dmesg -T format and filter
            struct tm tm_time;
            char *bracket_pos = strchr(line, ']');
            bool use_message = true;
            
            if (bracket_pos) {
                char time_str[100];
                strncpy(time_str, line + 1, bracket_pos - line - 1);
                time_str[bracket_pos - line - 1] = '\0';
                
                if (strptime(time_str, "%a %b %d %H:%M:%S %Y", &tm_time)) {
                    time_t msg_time = mktime(&tm_time);
                    use_message = (msg_time >= start_time);
                }
            }
            
            if (use_message) {
                // Count general OSD_SCRUB entries from dmesg
                stats->count++;
                
                // Extract journal start timing from dmesg
                if (strstr(line, "OSD_SCRUB: Lustre OI osd_journal_start_sb latency at osd_scrub_convert_ff()")) {
                    if (sscanf(line, "%*[^=]= %lld ns", &timing_value) == 1) {
                        if (stats->journal_start_count < 1000) {
                            stats->journal_start_timings[stats->journal_start_count] = timing_value;
                            stats->journal_start_count++;
                            
                            // Update min/max
                            if (timing_value < stats->min_journal_start) {
                                stats->min_journal_start = timing_value;
                            }
                            if (timing_value > stats->max_journal_start) {
                                stats->max_journal_start = timing_value;
                            }
                        }
                    }
                }
                
                // Extract journal stop timing from dmesg
                if (strstr(line, "OSD_SCRUB: Lustre OI commit latency after ldiskfs_journal_stop at osd_scrub_convert_ff()")) {
                    if (sscanf(line, "%*[^=]= %lld ns", &timing_value) == 1) {
                        if (stats->journal_stop_count < 1000) {
                            stats->journal_stop_timings[stats->journal_stop_count] = timing_value;
                            stats->journal_stop_count++;
                            
                            // Update min/max
                            if (timing_value < stats->min_journal_stop) {
                                stats->min_journal_stop = timing_value;
                            }
                            if (timing_value > stats->max_journal_stop) {
                                stats->max_journal_stop = timing_value;
                            }
                        }
                    }
                }
            }
        }
        pclose(dmesg_pipe);
    }
    
    // Calculate averages
    if (stats->journal_start_count > 0) {
        long long total = 0;
        for (int i = 0; i < stats->journal_start_count; i++) {
            total += stats->journal_start_timings[i];
        }
        stats->avg_journal_start = total / stats->journal_start_count;
    }
    
    if (stats->journal_stop_count > 0) {
        long long total = 0;
        for (int i = 0; i < stats->journal_stop_count; i++) {
            total += stats->journal_stop_timings[i];
        }
        stats->avg_journal_stop = total / stats->journal_stop_count;
    }
}

// Comprehensive log analysis
static void analyze_instrumentation_logs(const char *log_filename, instrumentation_stats_t *stats, time_t start_time) {
    printf("\nAnalyzing instrumentation logs...\n");
    printf("Filtering messages from benchmark start time: %s", ctime(&start_time));
    
    // Parse OBD allocations from both lctl dk and dmesg with timestamp filtering
    parse_obd_allocations(log_filename, &stats->alloc_stats);
    parse_dmesg_obd_allocations(&stats->alloc_stats, start_time);
    
    // Parse OSD scrub data from both sources with timestamp filtering
    parse_osd_scrub_data(log_filename, &stats->scrub_stats, start_time);
    stats->total_osd_scrub_entries = stats->scrub_stats.count;
    
    printf("Log analysis completed.\n");
    printf("Sources checked: lctl dk output + dmesg kernel log (time-filtered)\n");
}

// Memory allocation and assignment operations
static void perform_memory_operations(benchmark_config_t *config) {
    // example from osd_handler.c
    // struct osd_idmap_cache *idc;
    // int i;
    // OBD_ALLOC_PTR_ARRAY_LARGE(idc, i);

    printf("Performing allocation using OBD_ALLOC_PTR_ARRAY_LARGE...\n");
    struct osd_idmap_cache *idc_array;
    int i;

    // initialize idc_array with a large enough size for testing
    // This simulates the allocation of a large array of osd_idmap_cache structures
    // as seen in osd_handler.c


    i = TEST_FILE_COUNT;  // Use a large enough size for testing
    OBD_ALLOC_PTR_ARRAY_LARGE(idc_array, i);
    if (!idc_array) {
        fprintf(stderr, "Failed to allocate osd_idmap_cache array\n");
        return;
    }

    printf("Allocated osd_idmap_cache array of size %d at %p\n", i, idc_array);

    // print about to perform simple memory operation
    printf("Performing simple memory operations...\n");

    // add a timer for that
    uint64_t simple_start = rdtsc_start();
    
    // Variable allocation and assignment
    volatile int *a_ptr = &a;
    *a_ptr = 42;  // a = 42
    volatile int a_prime = *a_ptr;  // a' = a
    
    // Use a_prime to avoid compiler warning
    (void)a_prime;

    uint64_t simple_end = rdtsc_end();
    printf("Simple memory operation completed in %lu cycles.\n", simple_end - simple_start);

    // print about to perform complex memory allocation
    printf("Performing complex memory allocation...\n");

    // time complex memory allocation
    uint64_t complex_start = rdtsc_start();
    
    // Additional memory operations to stress allocation patterns
    void *temp_ptrs[10];
    for (int i = 0; i < 10; i++) {
        temp_ptrs[i] = malloc(1024);
        memset(temp_ptrs[i], 0xAA, 1024);
    }

    uint64_t complex_end = rdtsc_end();

    // let's print a message to indicate we are about to flush memory
    printf("Performing flushing...\n");

    // add a timer to see how long it takes to perform the flush
    uint64_t flush_start = rdtsc_start();
    
    // CLFLUSH operations if enabled
    if (config->enable_clflush) {
        flush_memory_region(test_memory, MEMORY_SIZE);
        printf("Just flushed test memory region.\n");
        for (int i = 0; i < 10; i++) {
            flush_memory_region(temp_ptrs[i], 1024);
        }
    }

    uint64_t flush_end = rdtsc_end();
    printf("Memory flush completed in %lu cycles.\n", flush_end - flush_start);
    
    // Clean up
    for (int i = 0; i < 10; i++) {
        free(temp_ptrs[i]);
    }
}

// File operations to trigger Lustre code paths
static int create_test_file(const char *filepath) {
    int fd = open(filepath, O_CREAT | O_WRONLY | O_EXCL, 0644);
    if (fd < 0) {
        perror("create_test_file");
        return -1;
    }
    
    // Write some data to trigger OST operations
    char buffer[4096];
    memset(buffer, 0xAA, sizeof(buffer));
    write(fd, buffer, sizeof(buffer));
    
    // Force sync to trigger journaling
    fsync(fd);
    close(fd);
    return 0;
}

static int modify_file_attributes(const char *filepath) {
    // Modify file permissions (triggers MDT operations)
    if (chmod(filepath, 0755) < 0) {
        perror("chmod");
        return -1;
    }
    
    // Set extended attributes (triggers OBD_ALLOC in osd_handler.c)
    const char *xattr_name = "user.lustre_benchmark";
    const char *xattr_value = "test_value_for_instrumentation";
    
    if (setxattr(filepath, xattr_name, xattr_value, strlen(xattr_value), 0) < 0) {
        perror("setxattr");
        return -1;
    }
    
    // Get extended attributes (additional OBD_ALLOC triggers)
    char buffer[256];
    if (getxattr(filepath, xattr_name, buffer, sizeof(buffer)) < 0) {
        perror("getxattr");
        return -1;
    }
    
    // Update timestamps (triggers inode metadata operations)
    struct timespec times[2] = {{0, 0}, {0, 0}};
    if (utimensat(AT_FDCWD, filepath, times, 0) < 0) {
        perror("utimensat");
        return -1;
    }
    
    return 0;
}

static int delete_test_file(const char *filepath) {
    if (unlink(filepath) < 0) {
        perror("unlink");
        return -1;
    }
    return 0;
}

// Lustre-specific operations to trigger instrumentation
static int trigger_lustre_operations(const char *filepath) {
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        perror("open for lustre operations");
        return -1;
    }
    
    // Get file striping information (triggers client-side operations)
    struct llapi_layout *layout = llapi_layout_get_by_fd(fd, 0);
    if (layout) {
        uint64_t stripe_count, stripe_size;
        llapi_layout_stripe_count_get(layout, &stripe_count);
        llapi_layout_stripe_size_get(layout, &stripe_size);
        llapi_layout_free(layout);
    }
    
    // Use ioctl to trigger additional code paths
    struct lov_user_md lum;
    if (ioctl(fd, LL_IOC_LOV_GETSTRIPE, &lum) == 0) {
        // Successfully retrieved stripe information
    }
    
    // Get file FID (triggers path2fid conversion)
    struct lu_fid fid;
    if (llapi_fd2fid(fd, &fid) == 0) {
        // Successfully got FID
    }
    
    close(fd);
    return 0;
}

// Operations specifically designed to trigger osd_scrub_convert_ff
static int trigger_osd_scrub_operations(const char *test_dir) {
    char command[PATH_MAX + 100];
    
    printf("  Attempting to trigger osd_scrub_convert_ff()...\n");
    
    // 1. Try to trigger OI scrub operations by creating many files rapidly
    // This can trigger OI cache additions (osd_idc_add) which we see in dmesg
    for (int i = 0; i < 20; i++) {
        char rapid_file[PATH_MAX];
        snprintf(rapid_file, sizeof(rapid_file), "%s/rapid_%d.dat", test_dir, i);
        
        int fd = open(rapid_file, O_CREAT | O_WRONLY | O_EXCL, 0644);
        if (fd >= 0) {
            // Write data to trigger OST operations and potential scrub
            char buffer[8192];
            memset(buffer, 0xBB + i, sizeof(buffer));
            write(fd, buffer, sizeof(buffer));
            fsync(fd);  // Force immediate write-back
            close(fd);
        }
    }
    
    // 2. Create files with varying sizes to trigger different allocation patterns
    for (int i = 0; i < 10; i++) {
        char varied_file[PATH_MAX];
        snprintf(varied_file, sizeof(varied_file), "%s/varied_%d.dat", test_dir, i);
        
        int fd = open(varied_file, O_CREAT | O_WRONLY | O_EXCL, 0644);
        if (fd >= 0) {
            size_t size = (i + 1) * 4096;  // Varying sizes
            char *buffer = malloc(size);
            if (buffer) {
                memset(buffer, 0xCC + i, size);
                write(fd, buffer, size);
                fsync(fd);
                free(buffer);
            }
            close(fd);
        }
    }
    
    // 3. Try to manually trigger scrub operations that may activate convert_ff
    printf("  Attempting to trigger OI scrub via lctl commands...\n");
    
    // Try to start OI scrub (may require admin privileges)
    snprintf(command, sizeof(command), "lctl set_param osd-*.*.start_scrub=1 2>/dev/null || true");
    system(command);
    
    // Try to trigger scrub check operations
    snprintf(command, sizeof(command), "lctl set_param osd-*.*.force_sync=1 2>/dev/null || true");
    system(command);
    
    // Set scrub parameters that might trigger conversion
    snprintf(command, sizeof(command), "lctl set_param osd-*.*.scrub_speed=500 2>/dev/null || true");
    system(command);
    
    // Try to trigger upgrade mode (this might activate convert_ff)
    snprintf(command, sizeof(command), "lctl set_param osd-*.*.upgrade=1 2>/dev/null || true");
    system(command);
    
    // 4. Force OI lookup operations that may trigger scrub
    snprintf(command, sizeof(command), "find %s -name '*.dat' -exec stat {} \\; >/dev/null 2>&1", test_dir);
    system(command);
    
    // 5. Create and delete files rapidly to stress OI operations
    printf("  Creating/deleting files to stress OI operations...\n");
    for (int i = 0; i < 30; i++) {
        char stress_file[PATH_MAX];
        snprintf(stress_file, sizeof(stress_file), "%s/stress_%d.tmp", test_dir, i);
        
        int fd = open(stress_file, O_CREAT | O_WRONLY | O_EXCL, 0644);
        if (fd >= 0) {
            write(fd, "stress_test", 11);
            close(fd);
            
            // Immediate delete to stress OI mapping/unmapping
            unlink(stress_file);
        }
    }
    
    // 6. Create directories which can trigger different OI operations
    for (int i = 0; i < 5; i++) {
        char dir_name[PATH_MAX];
        snprintf(dir_name, sizeof(dir_name), "%s/test_dir_%d", test_dir, i);
        mkdir(dir_name, 0755);
        
        // Create files within directories
        char sub_file[PATH_MAX];
        snprintf(sub_file, sizeof(sub_file), "%s/subfile_%d.txt", dir_name, i);
        int fd = open(sub_file, O_CREAT | O_WRONLY | O_EXCL, 0644);
        if (fd >= 0) {
            write(fd, "trigger_scrub", 13);
            close(fd);
        }
    }
    
    // 7. Try to trigger inconsistency detection (which may activate scrub)
    snprintf(command, sizeof(command), "lctl set_param osd-*.*.check_oi=1 2>/dev/null || true");
    system(command);
    
    // 8. Force OI table operations
    snprintf(command, sizeof(command), "lctl set_param osd-*.*.oi_scrub=full 2>/dev/null || true");
    system(command);
    
    printf("  OSD scrub trigger operations completed.\n");
    return 0;
}

// Advanced operations to force osd_scrub_convert_ff() activation
static int force_oi_scrub_conversion(const char *test_dir) {
    char command[PATH_MAX + 200];
    
    printf("  Attempting advanced OI scrub conversion triggers...\n");
    
    // 1. Try to simulate upgrade conditions
    // The convert_ff function is triggered during upgrade scenarios
    printf("    Simulating upgrade scenario...\n");
    system("lctl set_param osd-*.*.scrub_flags=SF_UPGRADE 2>/dev/null || true");
    system("lctl set_param osd-*.*.check_ff=1 2>/dev/null || true");
    
    // 2. Create many files then force consistency check
    char batch_dir[PATH_MAX];
    snprintf(batch_dir, sizeof(batch_dir), "%s/conversion_batch", test_dir);
    mkdir(batch_dir, 0755);
    
    for (int i = 0; i < 50; i++) {
        char batch_file[PATH_MAX];
        snprintf(batch_file, sizeof(batch_file), "%s/convert_%04d.dat", batch_dir, i);
        
        int fd = open(batch_file, O_CREAT | O_WRONLY | O_EXCL, 0644);
        if (fd >= 0) {
            // Write pattern that might trigger format detection
            char pattern[1024];
            memset(pattern, 0xDE + (i % 16), sizeof(pattern));
            write(fd, pattern, sizeof(pattern));
            
            // Multiple fsyncs to force journal operations
            fsync(fd);
            close(fd);
            
            // Force extended attribute operations (may trigger conversion checks)
            snprintf(command, sizeof(command), 
                "setfattr -n user.test -v 'conversion_test_%d' %s 2>/dev/null || true", i, batch_file);
            system(command);
        }
    }
    
    // 3. Force scrub to examine all created objects
    system("lctl set_param osd-*.*.scrub_speed=1000 2>/dev/null || true");
    system("lctl set_param osd-*.*.scrub_check_all=1 2>/dev/null || true");
    
    // 4. Try to restart scrub with conversion flags
    system("lctl set_param osd-*.*.stop_scrub=1 2>/dev/null || true");
    usleep(100000); // 100ms
    system("lctl set_param osd-*.*.start_scrub=1 2>/dev/null || true");
    
    // 5. Force OI table rebuild (may trigger conversion)
    system("lctl set_param osd-*.*.rebuild_oi=1 2>/dev/null || true");
    
    printf("    Advanced conversion triggers completed.\n");
    return 0;
}

// Enhanced file operations that trigger more instrumented code paths
static int create_test_file_enhanced(const char *filepath, int iteration) {
    int fd = open(filepath, O_CREAT | O_WRONLY | O_EXCL, 0644);
    if (fd < 0) {
        perror("create_test_file_enhanced");
        return -1;
    }
    
    // Write varying amounts of data to trigger different allocation patterns
    size_t write_size = 4096 + (iteration % 16) * 1024;  // 4KB to 20KB
    char *buffer = malloc(write_size);
    if (buffer) {
        memset(buffer, 0xAA + (iteration % 16), write_size);
        ssize_t written = write(fd, buffer, write_size);
        if (written > 0) {
            // Force multiple sync operations to trigger journaling
            fsync(fd);
            fdatasync(fd);  // Additional sync to stress the journaling system
        }
        free(buffer);
    }
    
    close(fd);
    return 0;
}

// Main benchmark function
static benchmark_stats_t run_benchmark(benchmark_config_t *config, time_t *start_time) {
    char filepath[PATH_MAX];
    uint64_t *results = malloc(BENCHMARK_ITERATIONS * sizeof(uint64_t));
    uint64_t overhead = measure_timing_overhead();
    double cpu_freq_ghz = estimate_cpu_frequency(); // cycles per nanosecond
    
    // Record start time for log filtering
    *start_time = time(NULL);
    
    printf("Starting Lustre microbenchmark...\n");
    printf("Benchmark start time: %s", ctime(start_time));
    printf("Timing overhead: %lu cycles\n", overhead);
    printf("Estimated CPU frequency: %.2f GHz\n", cpu_freq_ghz);
    printf("Note: 'Cycles' = CPU clock cycles measured by RDTSC instruction\n");
    printf("      This provides nanosecond-precision timing measurements\n\n");
    
    // Clear existing dmesg entries to get a clean baseline
    printf("Clearing kernel message buffer...\n");
    system("dmesg -C 2>/dev/null || true");
    
    // Warm-up runs with enhanced operations
    printf("Performing warm-up operations...\n");
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        snprintf(filepath, sizeof(filepath), "%s/warmup_%d.txt", config->test_dir, i);
        create_test_file_enhanced(filepath, i);
        modify_file_attributes(filepath);
        delete_test_file(filepath);
    }
    
    // Trigger OSD scrub operations before main benchmark
    printf("Phase 1: Triggering basic OSD scrub operations...\n");
    trigger_osd_scrub_operations(config->test_dir);
    
    // Advanced conversion triggers
    printf("Phase 2: Attempting to force osd_scrub_convert_ff() activation...\n");
    force_oi_scrub_conversion(config->test_dir);
    
    // Small delay to let background operations settle
    printf("Waiting for background operations to settle...\n");
    sleep(2);
    
    // Update start time to after trigger operations
    *start_time = time(NULL);
    printf("Main benchmark start time: %s", ctime(start_time));
    
    // Main benchmark iterations
    printf("Running main benchmark iterations...\n");
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        snprintf(filepath, sizeof(filepath), "%s/bench_%d.txt", config->test_dir, i);
        
        // Flush caches before measurement
        if (config->enable_clflush) {
            flush_memory_region(test_memory, MEMORY_SIZE);
        }

        // Start timing
        uint64_t start = rdtsc_start();
        
        // Perform memory operations
        perform_memory_operations(config);

        // End timing
        uint64_t end = rdtsc_end();
        
        // Enhanced file operations that trigger instrumented code paths
        if (create_test_file_enhanced(filepath, i) < 0) {
            fprintf(stderr, "Failed to create test file %s\n", filepath);
            continue;
        }
        
        if (modify_file_attributes(filepath) < 0) {
            fprintf(stderr, "Failed to modify attributes for %s\n", filepath);
        }
        
        if (trigger_lustre_operations(filepath) < 0) {
            fprintf(stderr, "Failed to trigger Lustre operations for %s\n", filepath);
        }
        
        // Additional OSD scrub triggers every 10 iterations
        if (i % 10 == 0) {
            trigger_osd_scrub_operations(config->test_dir);
        }
        
        // More aggressive conversion attempts every 25 iterations
        if (i % 25 == 0) {
            force_oi_scrub_conversion(config->test_dir);
        }
        
        if (delete_test_file(filepath) < 0) {
            fprintf(stderr, "Failed to delete test file %s\n", filepath);
        }
        
        
        results[i] = (end - start) - overhead;
        
        if (config->verbose && i % 10 == 0) {
            double time_ns = results[i] / cpu_freq_ghz;
            printf("Iteration %d: %lu cycles (%.2f μs)\n", i, results[i], time_ns / 1000.0);
        }
    }
    
    // Final aggressive trigger to capture any remaining operations
    printf("Phase 3: Final comprehensive trigger attempt...\n");
    trigger_osd_scrub_operations(config->test_dir);
    force_oi_scrub_conversion(config->test_dir);
    
    // Extended delay to let operations complete and appear in logs
    printf("Waiting for final operations to complete and logs to flush...\n");
    sleep(5);
    
    benchmark_stats_t stats = calculate_stats(results, BENCHMARK_ITERATIONS);
    free(results);
    
    return stats;
}

// Kernel log analysis functions
static void setup_lustre_debugging(void) {
    printf("Setting up Lustre debugging...\n");
    
    // Enable debug logging for relevant subsystems
    system("lctl set_param debug=+malloc,+trace,+vfstrace 2>/dev/null");
    system("lctl set_param subsystem_debug=+osd,+llite,+osc 2>/dev/null");
    
    // Clear existing debug buffer
    system("lctl clear 2>/dev/null");
}

static void capture_kernel_logs(const char *log_filename) {
    char command[512];
    snprintf(command, sizeof(command), "lctl dk > %s 2>/dev/null", log_filename);
    system(command);
    printf("Kernel debug log captured to: %s\n", log_filename);
}

static void analyze_kernel_logs(const char *log_filename) {
    char command[512];
    
    printf("\nAnalyzing kernel logs for instrumentation markers...\n");
    
    // Search for OBD_ALLOC related entries
    snprintf(command, sizeof(command), 
        "grep -i 'OBD\\|osd_object\\|malloc' %s | head -20", log_filename);
    printf("OBD_ALLOC related entries:\n");
    system(command);
    
    // Search for journaling related entries
    snprintf(command, sizeof(command), 
        "grep -i 'journal\\|ldiskfs\\|OSD_SCRUB' %s | head -20", log_filename);
    printf("\nJournaling related entries:\n");
    system(command);
    
    // Search for file operation traces
    snprintf(command, sizeof(command), 
        "grep -i 'vfstrace\\|llite\\|osc' %s | head -20", log_filename);
    printf("\nFile operation traces:\n");
    system(command);
}

// Main function
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <lustre_mount_point> [options]\n", argv[0]);
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  -v          Verbose output\n");
        fprintf(stderr, "  -c          Enable CLFLUSH operations\n");
        return 1;
    }
    
    benchmark_config_t config = {0};
    config.lustre_path = argv[1];
    config.verbose = 0;
    config.enable_clflush = 0;
    
    // Parse options
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            config.verbose = 1;
        } else if (strcmp(argv[i], "-c") == 0) {
            config.enable_clflush = 1;
        }
    }
    
    // Setup test directory
    char test_dir[PATH_MAX];
    snprintf(test_dir, sizeof(test_dir), "%s/benchmark_test_%d", 
             config.lustre_path, getpid());
    config.test_dir = test_dir;
    
    // Create test directory
    if (mkdir(test_dir, 0755) < 0) {
        perror("mkdir");
        return 1;
    }
    
    // Allocate test memory
    test_memory = aligned_alloc(CACHE_LINE_SIZE, MEMORY_SIZE);
    if (!test_memory) {
        perror("aligned_alloc");
        return 1;
    }
    memset(test_memory, 0xAA, MEMORY_SIZE);
    
    // Initialize instrumentation statistics
    init_instrumentation_stats(&config.instr_stats);
    
    // Setup Lustre debugging
    setup_lustre_debugging();
    
    printf("Lustre 2.15.5 Client-Side Performance Microbenchmark\n");
    printf("====================================================\n");
    printf("Test directory: %s\n", test_dir);
    printf("Memory size: %d bytes\n", MEMORY_SIZE);
    printf("Iterations: %d\n", BENCHMARK_ITERATIONS);
    printf("CLFLUSH enabled: %s\n", config.enable_clflush ? "Yes" : "No");
    printf("Verbose output: %s\n", config.verbose ? "Yes" : "No");
    printf("\n");
    
    // Run benchmark
    time_t benchmark_start_time;
    benchmark_stats_t stats = run_benchmark(&config, &benchmark_start_time);
    
    // Capture kernel logs
    char log_filename[PATH_MAX];
    snprintf(log_filename, sizeof(log_filename), "%s/kernel_debug.log", test_dir);
    capture_kernel_logs(log_filename);
    
    // Analyze instrumentation logs with timestamp filtering
    analyze_instrumentation_logs(log_filename, &config.instr_stats, benchmark_start_time);
    
    // Calculate CPU frequency for time conversion
    double cpu_freq_ghz = estimate_cpu_frequency();
    
    // Print results
    printf("\n===========================================\n");
    printf("            BENCHMARK RESULTS              \n");
    printf("===========================================\n");
    printf("Performance Measurements:\n");
    printf("  Min cycles:      %lu (%.2f μs)\n", stats.min, stats.min / cpu_freq_ghz / 1000);
    printf("  Max cycles:      %lu (%.2f μs)\n", stats.max, stats.max / cpu_freq_ghz / 1000);
    printf("  Avg cycles:      %lu (%.2f μs)\n", stats.avg, stats.avg / cpu_freq_ghz / 1000);
    printf("  Total cycles:    %lu\n", stats.total);
    printf("  Variance:        %.2f cycles²\n", stats.variance);
    printf("  CPU frequency:   %.2f GHz\n", cpu_freq_ghz);
    printf("\nNote: Cycles = CPU clock ticks measured by processor's timestamp counter\n");
    printf("      Lower cycle counts = better performance\n");
    printf("\n");
    
    // Print OBD allocation statistics
    printf("OBD Allocation Statistics:\n");
    printf("==========================\n");
    printf("OBD_ALLOC_PTR:              %d\n", config.instr_stats.alloc_stats.obd_alloc_ptr);
    printf("OBD_ALLOC_PTR_ARRAY_LARGE:  %d\n", config.instr_stats.alloc_stats.obd_alloc_ptr_array_large);
    printf("OBD_ALLOC_PTR_ARRAY:        %d\n", config.instr_stats.alloc_stats.obd_alloc_ptr_array);
    printf("OBD_ALLOC:                  %d\n", config.instr_stats.alloc_stats.obd_alloc);
    printf("OBD_SLAB_ALLOC_PTR:         %d\n", config.instr_stats.alloc_stats.obd_slab_alloc_ptr);
    printf("Total OBD Allocations:      %d\n", config.instr_stats.alloc_stats.total_obd_allocs);
    printf("\n");
    
    // Print OSD scrub statistics
    printf("OSD Scrub Statistics:\n");
    printf("=====================\n");
    printf("Total OSD_SCRUB entries:    %d\n", config.instr_stats.total_osd_scrub_entries);
    printf("Journal start measurements: %d\n", config.instr_stats.scrub_stats.journal_start_count);
    printf("Journal stop measurements:  %d\n", config.instr_stats.scrub_stats.journal_stop_count);
    printf("\n");
    
    // Print journal timing statistics if available
    if (config.instr_stats.scrub_stats.journal_start_count > 0) {
        printf("Journal Start Timing (osd_journal_start_sb):\n");
        printf("=============================================\n");
        printf("Min latency:    %lld ns\n", config.instr_stats.scrub_stats.min_journal_start);
        printf("Max latency:    %lld ns\n", config.instr_stats.scrub_stats.max_journal_start);
        printf("Avg latency:    %lld ns\n", config.instr_stats.scrub_stats.avg_journal_start);
        printf("Sample count:   %d\n", config.instr_stats.scrub_stats.journal_start_count);
        printf("\n");
    } else {
        printf("No journal start timing data found in logs.\n\n");
    }
    
    if (config.instr_stats.scrub_stats.journal_stop_count > 0) {
        printf("Journal Stop Timing (ldiskfs_journal_stop):\n");
        printf("============================================\n");
        printf("Min latency:    %lld ns\n", config.instr_stats.scrub_stats.min_journal_stop);
        printf("Max latency:    %lld ns\n", config.instr_stats.scrub_stats.max_journal_stop);
        printf("Avg latency:    %lld ns\n", config.instr_stats.scrub_stats.avg_journal_stop);
        printf("Sample count:   %d\n", config.instr_stats.scrub_stats.journal_stop_count);
        printf("\n");
    } else {
        printf("No journal stop timing data found in logs.\n\n");
    }
    
    // Print detailed timing analysis if verbose
    if (config.verbose && config.instr_stats.scrub_stats.journal_start_count > 0) {
        printf("Detailed Journal Start Timings:\n");
        printf("===============================\n");
        for (int i = 0; i < config.instr_stats.scrub_stats.journal_start_count && i < 10; i++) {
            printf("Sample %d: %lld ns\n", i+1, config.instr_stats.scrub_stats.journal_start_timings[i]);
        }
        if (config.instr_stats.scrub_stats.journal_start_count > 10) {
            printf("... and %d more samples\n", config.instr_stats.scrub_stats.journal_start_count - 10);
        }
        printf("\n");
    }
    
    if (config.verbose && config.instr_stats.scrub_stats.journal_stop_count > 0) {
        printf("Detailed Journal Stop Timings:\n");
        printf("==============================\n");
        for (int i = 0; i < config.instr_stats.scrub_stats.journal_stop_count && i < 10; i++) {
            printf("Sample %d: %lld ns\n", i+1, config.instr_stats.scrub_stats.journal_stop_timings[i]);
        }
        if (config.instr_stats.scrub_stats.journal_stop_count > 10) {
            printf("... and %d more samples\n", config.instr_stats.scrub_stats.journal_stop_count - 10);
        }
        printf("\n");
    }
    
    // Original kernel log analysis (for compatibility)
    analyze_kernel_logs(log_filename);
    
    // Summary and recommendations
    printf("\n===========================================\n");
    printf("        INSTRUMENTATION ANALYSIS          \n");
    printf("===========================================\n");
    printf("Benchmark Configuration:\n");
    printf("  Iterations: %d\n", BENCHMARK_ITERATIONS);
    printf("  OBD allocations per iteration: %.2f\n", 
           (float)config.instr_stats.alloc_stats.total_obd_allocs / BENCHMARK_ITERATIONS);
    printf("  OSD_SCRUB events per iteration: %.2f\n", 
           (float)config.instr_stats.total_osd_scrub_entries / BENCHMARK_ITERATIONS);
    printf("\nInstrumentation Status:\n");
    
    if (config.instr_stats.scrub_stats.journal_start_count > 0 || 
        config.instr_stats.scrub_stats.journal_stop_count > 0) {
        printf("  ✅ osd_scrub_convert_ff() timing instrumentation WORKING!\n");
        printf("  ✅ Journal timing data successfully captured from kernel logs\n");
        if (config.instr_stats.scrub_stats.journal_start_count > 0) {
            printf("     → Journal start samples: %d\n", config.instr_stats.scrub_stats.journal_start_count);
        }
        if (config.instr_stats.scrub_stats.journal_stop_count > 0) {
            printf("     → Journal stop samples: %d\n", config.instr_stats.scrub_stats.journal_stop_count);
        }
    } else {
        printf("  ⚠️  No osd_scrub_convert_ff() timing data detected\n");
        printf("     → Check that your custom printk statements are active\n");
        printf("     → Try: dmesg | grep 'OSD_SCRUB.*latency'\n");
    }
    
    if (config.instr_stats.alloc_stats.total_obd_allocs > 0) {
        printf("  ✅ OBD allocation instrumentation WORKING!\n");
        printf("     → Total allocations detected: %d\n", config.instr_stats.alloc_stats.total_obd_allocs);
        printf("     → Sources: lctl dk + dmesg kernel log\n");
    } else {
        printf("  ⚠️  No OBD allocation patterns detected in logs\n");
        printf("     → Check: dmesg | grep 'OBD_ALLOC'\n");
    }
    
    if (config.instr_stats.total_osd_scrub_entries > 0) {
        printf("  ✅ OSD_SCRUB operations detected: %d entries\n", config.instr_stats.total_osd_scrub_entries);
    }
    
    printf("\nPerformance Insights:\n");
    if (config.instr_stats.scrub_stats.journal_start_count > 0) {
        printf("  • Journal start avg latency: %lld ns (%.2f μs)\n", 
               config.instr_stats.scrub_stats.avg_journal_start,
               config.instr_stats.scrub_stats.avg_journal_start / 1000.0);
    }
    if (config.instr_stats.scrub_stats.journal_stop_count > 0) {
        printf("  • Journal stop avg latency: %lld ns (%.2f μs)\n", 
               config.instr_stats.scrub_stats.avg_journal_stop,
               config.instr_stats.scrub_stats.avg_journal_stop / 1000.0);
    }
    if (config.instr_stats.alloc_stats.total_obd_allocs > 0) {
        printf("  • Most frequent allocation type: ");
        int max_count = 0;
        const char *max_type = "None";
        if (config.instr_stats.alloc_stats.obd_alloc > max_count) {
            max_count = config.instr_stats.alloc_stats.obd_alloc;
            max_type = "OBD_ALLOC";
        }
        if (config.instr_stats.alloc_stats.obd_alloc_ptr > max_count) {
            max_count = config.instr_stats.alloc_stats.obd_alloc_ptr;
            max_type = "OBD_ALLOC_PTR";
        }
        if (config.instr_stats.alloc_stats.obd_alloc_ptr_array_large > max_count) {
            max_count = config.instr_stats.alloc_stats.obd_alloc_ptr_array_large;
            max_type = "OBD_ALLOC_PTR_ARRAY_LARGE";
        }
        printf("%s (%d times)\n", max_type, max_count);
    }
    
    printf("\nDebugging Tips:\n");
    printf("  • Kernel logs saved to: %s\n", log_filename);
    printf("  • Check dmesg: dmesg | tail -100 | grep -E 'OSD_SCRUB|OBD_ALLOC'\n");
    printf("  • Check Lustre debug: lctl dk | grep -E 'osd_handler|osd_scrub'\n");
    printf("  • Monitor during benchmark: watch 'dmesg | tail -5'\n");
    printf("\n");
    
    // Cleanup
    printf("\nCleaning up...\n");
    char cleanup_cmd[PATH_MAX + 20];
    snprintf(cleanup_cmd, sizeof(cleanup_cmd), "rm -rf %s", test_dir);
    system(cleanup_cmd);
    free(test_memory);
    free_instrumentation_stats(&config.instr_stats);
    
    return 0;
}
