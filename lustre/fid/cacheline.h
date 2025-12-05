#pragma once

#include <linux/types.h>

// memory_fence provides a full memory barrier.
static inline void memory_mfence(void) {
    // Use inline assembly for mfence.
    __asm__ __volatile__("mfence" ::: "memory");
}

// memory_sfence provides a Store-memory barrier.
static inline void memory_sfence(void) {
    // Use inline assembly for mfence.
    __asm__ __volatile__("sfence" ::: "memory");
}

// memory_lfence provides a load-memory barrier.
static inline void memory_lfence(void) {
    // Use inline assembly for mfence.
    __asm__ __volatile__("lfence" ::: "memory");
}

// flush_region_internal performs the core flush loop using inline assembly.
static inline void flush_region_internal(void *ptr, size_t size) {
    char *p = (char *)ptr;
    const size_t cache_line_size = 64;
    for (size_t i = 0; i < size; i += cache_line_size) {
        // Direct inline assembly bypasses the need for -mclflushopt.
        // The 'm' constraint means 'memory operand'.
        // 'volatile' prevents compiler reordering.
        __asm__ __volatile__("clflushopt %0" : "+m" (*(volatile char *)(p + i)));
    }
}

// flush_region_and_sfence flushes a memory region and then issues a store fence.
static inline void flush_region_and_sfence(void *ptr, size_t size) {
    flush_region_internal(ptr, size);
	memory_sfence();
}

// invalidate_region evicts a region of memory from the CPU cache.
// This is used by a READER to ensure it sees the latest data written by a
// writer on a non-cache-coherent system.
static inline void invalidate_region(void *ptr, size_t size) {
    char *p = (char *)ptr;
    const size_t cache_line_size = 64;
    for (size_t i = 0; i < size; i += cache_line_size) {
        // Use clflushopt to evict the cache line.
        __asm__ __volatile__("clflushopt %0" : "+m" (*(volatile char *)(p + i)));
    }
    // A load fence after invalidating ensures that subsequent reads will see the fresh data from memory, not an older value that was in the pipeline.
    __asm__ __volatile__("lfence" ::: "memory");
}

// flush_region_and_sfence flushes a memory region and then issues a store fence.
static inline void flush_region_and_mfence(void *ptr, size_t size) {
    flush_region_internal(ptr, size);
	memory_mfence();
}
