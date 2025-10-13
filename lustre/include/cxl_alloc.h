#ifndef CXL_ALLOC_H
#define CXL_ALLOC_H

#include <linux/types.h>

/**
 * cxl_malloc() - Allocates a block of memory from the CXL pool.
 * @size: The number of bytes to allocate.
 *
 * Returns a pointer to the allocated memory or NULL on failure.
 */
void *cxl_malloc(size_t size);

/**
 * cxl_free() - Returns a block of memory to the CXL pool.
 * @ptr: A pointer to a block previously allocated by cxl_malloc().
 */
void cxl_free(void *ptr);

/**
 * cxl_pool_init() - Initialize the CXL memory pool
 *
 * Maps the DAX device and initializes the allocator. Can be called
 * during module initialization. If CXL device is not available,
 * falls back gracefully to allow kmalloc usage.
 *
 * Returns: 0 on success or graceful fallback, negative error code on failure
 */
int cxl_pool_init(void);

/**
 * cxl_pool_exit() - Clean up the CXL memory pool
 *
 * Unmaps the DAX device and releases all resources.
 * Should be called during module cleanup.
 */
void cxl_pool_exit(void);

/**
 * cxl_track_fallback() - Track when kmalloc fallback is used
 *
 * Internal function to track fallback allocations for statistics.
 */
void cxl_track_fallback(void);

#endif /* CXL_ALLOC_H */