#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/delay.h> /* For udelay */
#include <linux/fcntl.h>   // For open() flags
#include <linux/io.h>      // For memremap
#include "fid_cxl_alloc.h" // Header file for CXL FID allocation

#define CXL_DEV_PATH "/dev/dax0.0"
#define CXL_POOL_SIZE (2 * 1024 * 1024) /* 2MB as per example */

// declare cxl dev phys addr
// sudo cat /sys/bus/dax/devices/dax0.0/mapping0/start 
// 0x8080000000
// sudo cat /sys/bus/dax/devices/dax0.0/mapping0/end 
// 0xa07fffffff
// sudo cat /sys/bus/dax/devices/dax0.0/mapping0/page_offset 
// 0x0
#define CXL_DEV_PHYS_ADDR 0x8080000000

static struct file *cxl_filp = NULL;
static void *cxl_addr = NULL;

/* 
 * Spinlock for local concurrency control.
 * For cross-node, we rely on the 'lock' field in cxl_alloc_header and atomic ops/fencing.
 */
// static DEFINE_SPINLOCK(local_lock);

/* 
 * Initialize CXL allocator.
 * Opens the DAX device and maps it.
 */
int fid_cxl_init(void)
{
    int rc = 0;
    unsigned long addr;
    struct cxl_alloc_header *header;

    pr_info("[CXL_FID]: Initializing CXL allocator\n");
    if (cxl_addr) {
        pr_info("[CXL_FID]: CXL allocator already initialized\n");
        return 0; /* Already initialized */
    }
    /* Open the CXL device with read and write permissions */
    // we can't use open because we're in kernel space
    cxl_filp = filp_open(CXL_DEV_PATH, O_RDWR, 0);
    if (IS_ERR(cxl_filp)) {
        rc = PTR_ERR(cxl_filp);
        pr_err("[CXL_FID]: Failed to open CXL device %s: %d\n", CXL_DEV_PATH, rc);
        return rc;
    }
    /* 
     * Map the device using mmap: the problem is that we can't use mmap in kernel space
     * https://stackoverflow.com/questions/24112438/how-to-share-memory-between-user-space-and-kernel-using-mmap-and-the-data-is-not
     * https://stackoverflow.com/questions/48460742/how-to-mmap-a-file-inside-the-linux-kernel
     * so we need to use the phys addr: memremap
     */
    addr = (unsigned long)memremap(CXL_DEV_PHYS_ADDR, CXL_POOL_SIZE, MEMREMAP_WB); // we need to pass the necessary flags to memremap so all processes can access it
    // https://www.youtube.com/watch?v=m7E9piHcfr4
    if (IS_ERR_VALUE(addr)) {
        rc = (int)addr;
        pr_err("[CXL_FID]: Failed to memremap CXL device: %d\n", rc);
        filp_close(cxl_filp, NULL);
        cxl_filp = NULL;
        return rc;
    }
    cxl_addr = (void *)addr;
    /* Initialize header if it looks empty (simple check) */
    // Why? If this data structure is empty, we need to initialize it
    header = (struct cxl_alloc_header *)cxl_addr;
    
    /* Invalidate to read fresh data */
    invalidate_region(header, sizeof(*header)); // so we can read the data structure
    
    if (header->pool_size == 0) {
        /* Assume we are initializing */
        // set all params
        header->pool_size = CXL_POOL_SIZE;
        header->next_free_offset = sizeof(struct cxl_alloc_header);
        header->free_list_head = 0;
        header->lock = 0;
        header->fld_cache_root = 0;
        header->seq_ctrl_root = 0;
        
        flush_region_and_sfence(header, sizeof(*header)); // so we can write the data structure
        pr_info("[CXL_FID]: CXL allocator initialized at %p\n", cxl_addr);
    } else {
        pr_info("[CXL_FID]: CXL allocator attached at %p\n", cxl_addr);
    }
    return 0;
}
/*
 * This function is called when the module is unloaded.
 * It releases the CXL allocator resources.
 */
void fid_cxl_fini(void)
{
    pr_info("[CXL_FID]: Finalizing CXL allocator\n");
    // Given that memremap is used, we need to use munmap to release the memory
    if (cxl_addr) {
        memunmap(cxl_addr);
        cxl_addr = NULL;
    }
    // Given that filp_open is used, we need to use filp_close to release the file descriptor
    if (cxl_filp) {
        filp_close(cxl_filp, NULL);
        cxl_filp = NULL;
    }
}
/* This function acquires the shared lock 
* Why are locks critical? 
* We have to make sure that different nodes don't access the same memory at the same time
*/
void cxl_lock(struct cxl_alloc_header *header)
{
    int i = 0;
    while (1) {
        /* Simple test-and-set */
        if (__sync_bool_compare_and_swap(&header->lock, 0, 1)) {
            break;
        }
        udelay(1);
        if (i++ > 10) {
            pr_warn("[CXL_FID]: CXL lock contention\n");
            i = 0;
        }
    }
    /* Acquire barrier */
    memory_lfence(); // so we can read the data structure
}
/* This function releases the shared lock */
void cxl_unlock(struct cxl_alloc_header *header)
{
    /* Release barrier */
    memory_sfence();
    header->lock = 0;
}
/*
 * This function allocates a block of memory from the CXL allocator.
 * It returns a pointer to the allocated memory or NULL if allocation fails.
 */
void *fid_cxl_alloc(size_t size)
{
    void *ptr = NULL;
    struct cxl_alloc_header *header;

    pr_info("[CXL_FID]: Allocating %zu bytes\n", size);
    
    if (!cxl_addr) {
        if (fid_cxl_init() != 0)
            return NULL;
    }

    header = (struct cxl_alloc_header *)cxl_addr;
    cxl_lock(header); // lock!
    
    /* Invalidate header to ensure we have latest state */
    invalidate_region(header, sizeof(*header));
    
    /* 1. Check free list */
    // Why? We need to check if there is a free block in the free list
    // if there is, we can use it to allocate memory
    if (header->free_list_head != 0) {
        size_t curr_offset = header->free_list_head;
        struct cxl_free_block *block;
        struct cxl_free_block *prev = NULL;
        
        while (curr_offset != 0) {
            // The while loop is used to traverse the free list
            block = (struct cxl_free_block *)(cxl_addr + curr_offset);
            invalidate_region(block, sizeof(*block)); // so we can read the data structure
            if (block->size >= size) {
                /* Found a block */
                if (prev) {
                    prev->next_offset = block->next_offset;
                    flush_region_and_sfence(prev, sizeof(*prev)); // so we can write the data structure
                } else {
                    header->free_list_head = block->next_offset;
                }
                
                ptr = (void *)block;
                break;
            }
            
            prev = block;
            curr_offset = block->next_offset;
        }
    }
    /* 2. If no free block found, bump allocate */
    // Why? If there is no free block, we need to bump allocate
    if (!ptr) {
        if (header->next_free_offset + size <= header->pool_size) {
            ptr = cxl_addr + header->next_free_offset;
            header->next_free_offset += size;
        } else {
            pr_err("[CXL_FID]: CXL pool exhausted\n");
        }
    }
    /* Update header if we allocated */
    if (ptr) {
        flush_region_and_sfence(header, sizeof(*header)); // so we can write the data structure
    }
    cxl_unlock(header); // release the lock
    return ptr;
}

void fid_cxl_free(void *ptr, size_t size)
{
    struct cxl_alloc_header *header;
    struct cxl_free_block *block;
    size_t offset;

    pr_info("[CXL_FID]: Freeing %zu bytes at %p\n", size, ptr);
    if (!cxl_addr || !ptr) {
        pr_err("[CXL_FID]: Attempt to free pointer outside CXL pool\n");
        return;
    }
    
    header = (struct cxl_alloc_header *)cxl_addr;
    offset = (void *)ptr - cxl_addr;
    if (offset >= header->pool_size) {
        pr_err("[CXL_FID]: Attempt to free pointer outside CXL pool\n");
        return;
    }

    cxl_lock(header); // lock!
    
    /* Invalidate header */
    invalidate_region(header, sizeof(*header));
    
    /* Create free block node */
    // Why? We need to create a free block node to add it to the free list
    block = (struct cxl_free_block *)ptr;
    block->size = size;
    block->next_offset = header->free_list_head;
    
    /* Flush the block content */
    flush_region_and_sfence(block, sizeof(*block));
    
    /* Update head */
    header->free_list_head = offset;
    
    /* Flush header */
    flush_region_and_sfence(header, sizeof(*header));
    
    cxl_unlock(header); // release the lock
    pr_info("[CXL_FID]: Freed %zu bytes at %p\n", size, ptr);
}

/*
 * Get the base address of the CXL mapping. 
 * Handy for offset calculations if needed.
 */
void *fid_cxl_base(void)
{
    return cxl_addr;
}

/*
 * This function is used to atomically update a pointer to a new value in CXL.
 * It is used for updating fld_cache_entry->fce_data.
 * The reason why we need this is because we need to update the pointer to the new data in CXL.
 * We need to do this atomically (with the lock)
 */
int fid_cxl_atomic_update(void * volatile *ptr_addr, void *new_data)
{
    /* 
     * Don't assume new_data is already flushed by the caller.
     * We perform an atomic exchange of the pointer.
     */    
    void *old_val;
    
    /* Ensure previous writes are visible */
    memory_sfence();
    
    /* Atomic exchange */
    // xchg is needed to safely update the fce_data pointer
    old_val = xchg(ptr_addr, new_data);
    
    /* Ensure the pointer update is visible */
    flush_region_and_sfence((void *)ptr_addr, sizeof(void *));
    
    return 0;
}

/*
 * This function is used to get the FLD cache root from the CXL allocator.
 * It is used for updating fld_cache_entry->fce_data.
 * The reason why we need this is because we need to update the pointer to the new data in CXL.
 * We need to do this atomically (with the lock)
 */
void *fid_cxl_get_fld_cache(void)
{
    struct cxl_alloc_header *header; // pointer to the header

    pr_info("[CXL_FID]: Getting FLD cache root\n");

    if (!cxl_addr) {
        pr_err("[CXL_FID]: Attempt to get FLD cache root from null CXL address\n");
        return NULL;
    }
    
    header = (struct cxl_alloc_header *)cxl_addr; // cast the cxl_addr to the header
    invalidate_region(header, sizeof(*header)); // invalidate the header
    
    if (header->fld_cache_root == 0)
        return NULL;
        
    return cxl_addr + header->fld_cache_root; // return the pointer to the FLD cache
}

/*
 * This function is used to set the FLD cache root in the CXL allocator.
 * It is used for updating fld_cache_entry->fce_data.
 * The reason why we need this is because we need to update the pointer to the new data in CXL.
 * We need to do this atomically (with the lock)
 */
void fid_cxl_set_fld_cache(void *cache)
{
    struct cxl_alloc_header *header; // pointer to the header
    size_t offset; // offset of the cache

    pr_info("[CXL_FID]: Setting FLD cache root to %p\n", cache);
    
    if (!cxl_addr || !cache) return; // if the cxl_addr or cache is null, return
    
    header = (struct cxl_alloc_header *)cxl_addr; // cast the cxl_addr to the header
    
    // calculate the offset of the cache: We need to calculate the offset of the cache to store it in the header
    offset = cache - cxl_addr; 
    
    cxl_lock(header); // lock the header
    header->fld_cache_root = offset; // set the FLD cache root
    flush_region_and_sfence(header, sizeof(*header)); // flush the header
    cxl_unlock(header); // unlock the header
}

/*
 * Helper to get/set the Sequence Controller root in the CXL header.
 * It is used for updating seq_ctrl_root->fce_data.
 * The reason why we need this is because we need to update the pointer to the new data in CXL.
 * We need to do this atomically (with the lock)
 */
void *fid_cxl_get_seq_ctrl(void)
{
    struct cxl_alloc_header *header;

    pr_info("[CXL_FID]: Getting Sequence Controller root\n");

    if (!cxl_addr) {
        pr_err("[CXL_FID]: Attempt to get Sequence Controller root from null CXL address\n");
        return NULL;
    }
    
    header = (struct cxl_alloc_header *)cxl_addr;
    invalidate_region(header, sizeof(*header)); // invalidate the header
    
    if (header->seq_ctrl_root == 0) {
        pr_err("[CXL_FID]: Sequence Controller root is null\n");
        return NULL;
    }
        
    return cxl_addr + header->seq_ctrl_root; // return the pointer to the Sequence Controller
}

/*
 * This function sets the Sequence Controller root in the CXL header.
 * It is used for updating seq_ctrl_root->fce_data.
 * The reason why we need this is because we need to update the pointer to the new data in CXL.
 * We need to do this atomically (with the lock)
 */
void fid_cxl_set_seq_ctrl(void *ctrl)
{
    struct cxl_alloc_header *header;
    size_t offset;

    pr_info("[CXL_FID]: Setting Sequence Controller root to %p\n", ctrl);
    
    if (!cxl_addr || !ctrl) {
        pr_err("[CXL_FID]: Attempt to set Sequence Controller root from null CXL address\n");
        return;
    }
    
    header = (struct cxl_alloc_header *)cxl_addr;
    offset = ctrl - cxl_addr; // calculate the offset of the Sequence Controller
    
    cxl_lock(header); // lock the header
    header->seq_ctrl_root = offset; // set the Sequence Controller root
    flush_region_and_sfence(header, sizeof(*header)); // flush the header
    cxl_unlock(header); // unlock the header
}

EXPORT_SYMBOL(fid_cxl_init);
EXPORT_SYMBOL(fid_cxl_fini);
EXPORT_SYMBOL(cxl_lock);
EXPORT_SYMBOL(cxl_unlock);
EXPORT_SYMBOL(fid_cxl_alloc);
EXPORT_SYMBOL(fid_cxl_free);
EXPORT_SYMBOL(fid_cxl_base);
EXPORT_SYMBOL(fid_cxl_atomic_update);
EXPORT_SYMBOL(fid_cxl_get_fld_cache);
EXPORT_SYMBOL(fid_cxl_set_fld_cache);
EXPORT_SYMBOL(fid_cxl_get_seq_ctrl);
EXPORT_SYMBOL(fid_cxl_set_seq_ctrl);