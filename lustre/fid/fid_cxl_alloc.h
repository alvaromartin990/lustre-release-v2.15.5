#ifndef FID_CXL_ALLOC_H
#define FID_CXL_ALLOC_H

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include "cacheline.h"

/*
 * CXL FID Allocator
 * 
 * Manages a shared memory region on a CXL device for FID allocation.
 * Replaces standard OBD_ALLOC/OBD_FREE for cl_seq and dt_find_or_create for seq_store.
 */

/* 
 * Allocator metadata stored at the beginning of the CXL region.
 */
struct cxl_alloc_header {
    size_t next_free_offset; /* Offset to the next virgin space */
    size_t free_list_head;   /* Offset to the first free block (or 0 if empty) */
    size_t pool_size;        /* Total size of the pool */
    /* Simple lock for the allocator metadata (0 = unlocked, 1 = locked) */
    volatile int lock; 
    
    /* Root of the FLD cache (offset to struct fld_cache in CXL, or 0 if not init) */
    size_t fld_cache_root;

    /* Root of the Sequence Controller state (offset to struct lu_seq_range in CXL) */
    size_t seq_ctrl_root;
};

/* 
 * Node in the free list. 
 * Stored within the free memory block itself.
 */
struct cxl_free_block {
    size_t next_offset; /* Offset to the next free block */
    size_t size;        /* Size of this block */
};

/* Initialize the CXL allocator. Maps the device. */
int fid_cxl_init(void);

/* Finalize the CXL allocator. Unmaps the device. */
void fid_cxl_fini(void);

// cxl_lock
void cxl_lock(struct cxl_alloc_header *header);

// cxl_unlock
void cxl_unlock(struct cxl_alloc_header *header);

// cxl base
void *fid_cxl_base(void);

/* Allocate memory from the CXL pool. 
 * Replaces OBD_ALLOC_PTR. 
 * Returns pointer to allocated memory or NULL on failure.
 */
void *fid_cxl_alloc(size_t size);

/* Free memory to the CXL pool.
 * Replaces OBD_FREE_PTR.
 */
void fid_cxl_free(void *ptr, size_t size);

/* 
 * Get the base address of the CXL mapping. 
 * Useful for offset calculations if needed.
 */
void *fid_cxl_base(void);

/*
 * Atomic FLD Update Helper
 * 
 * Atomically updates a pointer to a new value in CXL.
 * Used for updating fld_cache_entry->fce_data.
 * 
 * @ptr_addr: Address of the pointer to update (e.g. &entry->fce_data)
 * @new_data: Pointer to the new data (already in CXL and flushed)
 * 
 * Returns 0 on success.
 */
int fid_cxl_atomic_update(void * volatile *ptr_addr, void *new_data);

/*
 * Helper to get/set the FLD cache root in the CXL header.
 */
void *fid_cxl_get_fld_cache(void);
void fid_cxl_set_fld_cache(void *cache);

/*
 * Helper to get/set the Sequence Controller root in the CXL header.
 */
void *fid_cxl_get_seq_ctrl(void);
void fid_cxl_set_seq_ctrl(void *ctrl);

#endif /* FID_CXL_ALLOC_H */
