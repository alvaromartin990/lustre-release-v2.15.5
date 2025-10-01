#ifndef CXL_ALLOC_H
#define CXL_ALLOC_H

#include <linux/types.h>

/**
 * cxl_alloc_init() - Maps the DAX device and initializes the allocator.
 * @dax_path: The device path, e.g., "/dev/dax0.0".
 *
 * Returns 0 on success, or a negative error code on failure.
 */
// int cxl_alloc_init(const char *dax_path);

/**
 * cxl_alloc_exit() - Unmaps the DAX device and cleans up resources.
 */
// void cxl_alloc_exit(void);

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

// Add these to cxl_alloc.h:
int cxl_pool_init(void);
void cxl_pool_exit(void);
void cxl_track_fallback(void);

#endif /* CXL_ALLOC_H */