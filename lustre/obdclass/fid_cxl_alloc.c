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
#include <linux/vmalloc.h> // For vzalloc/vfree
#include <obd_support.h>   // For OBD_ALLOC/OBD_FREE
#include <fid_cxl_alloc.h> // Header file for CXL FID allocation

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

// static struct file *cxl_filp = NULL;
static bool cxl_inited = false;
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
    struct cxl_alloc_header *header;
    void *mapped_addr = NULL;
    struct file *dax_file = NULL;
    bool dax_exists = false;

    if (cxl_inited)
        return 0;

    pr_info("[CXL_FID]: Initializing CXL allocator\n");
    if (cxl_addr) {
        pr_info("[CXL_FID]: CXL allocator already initialized\n");
        return 0; /* Already initialized */
    }

    /*
     * FIRST: Check if the DAX device actually exists.
     * We must do this BEFORE calling memremap(), because memremap() will
     * succeed even if the physical address has no actual memory behind it,
     * and we'll crash when we try to access it.
     */
    dax_file = filp_open(CXL_DEV_PATH, O_RDWR, 0);
    if (!IS_ERR(dax_file)) {
        pr_info("[CXL_FID]: DAX device %s exists\n", CXL_DEV_PATH);
        filp_close(dax_file, NULL);
        dax_exists = true;
    } else {
        /*
         * CRITICAL FIX: Do NOT use vzalloc fallback for CXL allocator.
         *
         * In a multi-MDT/OST Lustre setup (e.g., MDSCOUNT=2), all servers
         * run in the same kernel. If we use vzalloc as a "shared" pool:
         * - All MDTs/OSTs allocate from the same vzalloc pool
         * - When one MDT shuts down and frees its allocations, it corrupts
         *   the pool that other MDTs are still using
         * - This causes kernel panics during shutdown or operation
         *
         * For true CXL hardware, sharing is correct because:
         * - CXL memory is physically shared across nodes
         * - The allocator is designed for cross-node coordination
         *
         * Without real CXL, we must return failure and let callers use
         * standard kernel allocators (OBD_ALLOC) instead.
         */
        pr_info("[CXL_FID]: DAX device %s not found (err=%ld)\n",
                CXL_DEV_PATH, PTR_ERR(dax_file));
        pr_info("[CXL_FID]: CXL allocator disabled - callers should use OBD_ALLOC\n");
        return -ENODEV;
    }

    /*
     * Map physical CXL memory.
     * dax_exists is guaranteed true at this point (we returned -ENODEV above if not).
     */
    mapped_addr = memremap(CXL_DEV_PHYS_ADDR, CXL_POOL_SIZE, MEMREMAP_WB);

    if (!mapped_addr) {
        pr_err("[CXL_FID]: memremap failed for CXL device\n");
        return -EIO;
    }

    pr_info("[CXL_FID]: Successfully mapped CXL device at %p (phys: 0x%llx)\n",
            mapped_addr, (unsigned long long)CXL_DEV_PHYS_ADDR);
    cxl_addr = mapped_addr;

    /* Check if header is already initialized using magic number */
    header = (struct cxl_alloc_header *)cxl_addr;

    /*
     * Validate header using magic number and version
     */
    if (header->magic != CXL_ALLOC_MAGIC ||
        header->version != CXL_ALLOC_VERSION) {
        pr_info("[CXL_FID]: Initializing new CXL header (magic: 0x%llx, version: %u)\n",
                CXL_ALLOC_MAGIC, CXL_ALLOC_VERSION);
        memset(header, 0, sizeof(*header));
        header->magic = CXL_ALLOC_MAGIC;
        header->version = CXL_ALLOC_VERSION;
        header->pool_size = CXL_POOL_SIZE;
        header->next_free_offset = sizeof(struct cxl_alloc_header);
        header->free_list_head = 0;
        header->lock = 0;
        header->fld_cache_root = 0;
        header->seq_ctrl_root = 0;
        flush_region_and_sfence(header, sizeof(*header));
    } else {
        pr_info("[CXL_FID]: Found existing valid CXL header. Next free: %zu\n",
                header->next_free_offset);
    }

    cxl_inited = true;
    return 0;
}
/*
 * This function is called when the module is unloaded.
 * It releases the CXL allocator resources.
 *
 * Note: With vzalloc fallback removed, cxl_addr is always from memremap
 * (true CXL), so we always use memunmap.
 */
void fid_cxl_fini(void)
{
    pr_info("[CXL_FID]: Finalizing CXL allocator\n");
    if (cxl_addr) {
        memunmap(cxl_addr);
        cxl_addr = NULL;
        cxl_inited = false;
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
            /* Validate offset before dereferencing */
            if (!fid_cxl_offset_is_valid(curr_offset)) {
                pr_err("[CXL_FID]: Invalid free list offset %zu\n", curr_offset);
                break;
            }

            // The while loop is used to traverse the free list
            block = (struct cxl_free_block *)fid_cxl_offset_to_ptr(curr_offset);
            if (!block) {
                pr_err("[CXL_FID]: Failed to convert offset %zu to pointer\n", curr_offset);
                break;
            }

            invalidate_region(block, sizeof(*block)); // so we can read the data structure

            if (block->size >= size) {
                /* Found a block - validate next offset too */
                if (block->next_offset != 0 &&
                    !fid_cxl_offset_is_valid(block->next_offset)) {
                    pr_err("[CXL_FID]: Corrupted free list next %zu\n",
                           block->next_offset);
                    /* Still use this block but break chain */
                    block->next_offset = 0;
                }

                if (prev) {
                    prev->next_offset = block->next_offset;
                    flush_region_and_sfence(prev, sizeof(*prev)); // so we can write the data structure
                } else {
                    header->free_list_head = block->next_offset;
                }

                ptr = (void *)block;
                break;
            }

            /* Validate next offset before continuing */
            if (block->next_offset != 0 &&
                !fid_cxl_offset_is_valid(block->next_offset)) {
                pr_err("[CXL_FID]: Invalid next offset %zu in free list\n",
                       block->next_offset);
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

    /* Validate current free list head if it's not empty */
    if (header->free_list_head != 0 &&
        !fid_cxl_offset_is_valid(header->free_list_head)) {
        pr_err("[CXL_FID]: Corrupted free list head %zu, resetting free list\n",
               header->free_list_head);
        header->free_list_head = 0;
        flush_region_and_sfence(header, sizeof(*header));
    }

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
        pr_warn("[CXL_FID]: CXL not initialized, fld_cache_root unavailable\n");
        return NULL;
    }
    
    header = (struct cxl_alloc_header *)cxl_addr; // cast the cxl_addr to the header
    invalidate_region(header, sizeof(*header)); // invalidate the header

    if (header->fld_cache_root == 0)
        return NULL;

    /* Validate offset before conversion */
    if (!fid_cxl_offset_is_valid(header->fld_cache_root)) {
        pr_err("[CXL_FID]: Invalid FLD cache offset %zu\n", header->fld_cache_root);
        return NULL;
    }

    /* Safe pointer arithmetic with validated offset */
    return fid_cxl_offset_to_ptr(header->fld_cache_root); // return the pointer to the FLD cache
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
        pr_warn("[CXL_FID]: CXL not initialized, seq_ctrl_root unavailable\n");
        return NULL;
    }
    
    header = (struct cxl_alloc_header *)cxl_addr;
    invalidate_region(header, sizeof(*header)); // invalidate the header

    if (header->seq_ctrl_root == 0) {
        pr_info("[CXL_FID]: Sequence Controller root not yet set\n");
        return NULL;
    }

    /* Validate offset before conversion */
    if (!fid_cxl_offset_is_valid(header->seq_ctrl_root)) {
        pr_err("[CXL_FID]: Invalid Sequence Controller offset %zu\n", header->seq_ctrl_root);
        return NULL;
    }

    /* Safe pointer arithmetic with validated offset */
    return fid_cxl_offset_to_ptr(header->seq_ctrl_root); // return the pointer to the Sequence Controller
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

/*
 * Offset validation and conversion helpers
 *
 * These functions provide safe conversion between CXL offsets and kernel pointers
 */

/**
 * fid_cxl_offset_is_valid - Check if a CXL offset is valid
 * @offset: The offset to validate
 *
 * Returns true if the offset points to valid CXL memory (within pool bounds
 * and past the header), false otherwise.
 */
bool fid_cxl_offset_is_valid(size_t offset)
{
    struct cxl_alloc_header *header;

    if (!cxl_addr || !cxl_inited)
        return false;

    if (offset == 0)
        return false;

    header = (struct cxl_alloc_header *)cxl_addr;

    /* Offset must be within pool and past header */
    return (offset >= sizeof(struct cxl_alloc_header) &&
            offset < header->pool_size);
}

/**
 * fid_cxl_offset_to_ptr - Convert CXL offset to kernel pointer with validation
 * @offset: The offset to convert
 *
 * Returns a valid kernel pointer if the offset is within bounds, NULL otherwise.
 */
void *fid_cxl_offset_to_ptr(size_t offset)
{
    if (!fid_cxl_offset_is_valid(offset)) {
        pr_err("[CXL_FID]: Invalid offset %zu\n", offset);
        return NULL;
    }

    return cxl_addr + offset;
}

/**
 * fid_cxl_ptr_to_offset - Convert kernel pointer to CXL offset
 * @ptr: The pointer to convert
 *
 * Returns the offset if the pointer is in the CXL range, 0 otherwise.
 */
size_t fid_cxl_ptr_to_offset(void *ptr)
{
    struct cxl_alloc_header *header;
    size_t offset;

    if (!cxl_addr || !ptr)
        return 0;

    /* Check if ptr is in CXL range */
    header = (struct cxl_alloc_header *)cxl_addr;
    offset = ptr - cxl_addr;

    if (offset >= header->pool_size) {
        pr_err("[CXL_FID]: Pointer %p outside CXL range\n", ptr);
        return 0;
    }

    return offset;
}

/**
 * fid_cxl_ptr_is_cxl - Check if pointer is in CXL memory range
 * @ptr: The pointer to check
 *
 * Returns true if the pointer points to CXL memory, false otherwise.
 */
bool fid_cxl_ptr_is_cxl(const void *ptr)
{
    struct cxl_alloc_header *header;
    const char *p = (const char *)ptr;

    if (!cxl_addr || !ptr)
        return false;

    header = (struct cxl_alloc_header *)cxl_addr;

    return (p >= (const char *)cxl_addr &&
            p < (const char *)(cxl_addr + header->pool_size));
}

/**
 * fid_cxl_is_shared_memory - Check if CXL pool is true shared memory
 *
 * With vzalloc fallback removed, this now simply checks if CXL is initialized.
 * When fid_cxl_init() succeeds, we have true CXL shared memory.
 * When it fails (no DAX device), callers should use OBD_ALLOC instead.
 *
 * Returns true if CXL is initialized (always true shared memory now),
 * false if CXL is not available.
 */
bool fid_cxl_is_shared_memory(void)
{
    return cxl_addr && cxl_inited;
}

/**
 * fid_cxl_alloc_hybrid - Allocate memory, trying CXL first, OBD fallback
 * @size: Size in bytes to allocate
 *
 * This function attempts CXL allocation first. If CXL is not available
 * or the allocation fails, it falls back to OBD_ALLOC (kernel memory).
 *
 * Returns pointer to allocated memory, or NULL on failure.
 * Caller must use fid_cxl_free_hybrid() to free.
 */
void *fid_cxl_alloc_hybrid(size_t size)
{
    void *ptr = NULL;

    /* Only try CXL if it's initialized */
    if (cxl_inited && cxl_addr) {
        ptr = fid_cxl_alloc(size);
        if (ptr) {
            pr_info("[CXL_FID]: Hybrid alloc: CXL allocation succeeded, %zu bytes at %p\n", size, ptr);
            return ptr;
        }
    }

    /* Fallback to OBD_ALLOC - note: OBD_ALLOC is a macro that assigns to ptr */
    pr_info("[CXL_FID]: Hybrid alloc: Using OBD_ALLOC fallback for %zu bytes\n", size);
    OBD_ALLOC(ptr, size);
    if (ptr) {
        pr_info("[CXL_FID]: Hybrid alloc: OBD_ALLOC succeeded, %zu bytes at %p\n", size, ptr);
    } else {
        pr_err("[CXL_FID]: Hybrid alloc: OBD_ALLOC failed for %zu bytes\n", size);
    }
    return ptr;
}

/**
 * fid_cxl_free_hybrid - Free memory allocated by fid_cxl_alloc_hybrid
 * @ptr: Pointer to free
 * @size: Size that was allocated
 *
 * Automatically detects whether memory is CXL or kernel and frees accordingly.
 */
void fid_cxl_free_hybrid(void *ptr, size_t size)
{
    if (!ptr)
        return;

    if (fid_cxl_ptr_is_cxl(ptr)) {
        fid_cxl_free(ptr, size);
    } else {
        OBD_FREE(ptr, size);
    }
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
EXPORT_SYMBOL(fid_cxl_offset_is_valid);
EXPORT_SYMBOL(fid_cxl_offset_to_ptr);
EXPORT_SYMBOL(fid_cxl_ptr_to_offset);
EXPORT_SYMBOL(fid_cxl_ptr_is_cxl);
EXPORT_SYMBOL(fid_cxl_is_shared_memory);
EXPORT_SYMBOL(fid_cxl_alloc_hybrid);
EXPORT_SYMBOL(fid_cxl_free_hybrid);