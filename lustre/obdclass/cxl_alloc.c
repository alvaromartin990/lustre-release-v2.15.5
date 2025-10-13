#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/slab.h>    // For ALIGN()
#include <linux/file.h>    // Required for filp_open/close
#include <linux/uaccess.h> // Required for file modes
#include <linux/mm.h>      // For memory mapping
#include <linux/mman.h>    // For mmap constants
#include <linux/string.h>  // For string functions
#include <linux/io.h>      // For memremap()
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <asm/barrier.h>   // For memory barriers

#include "cxl_alloc.h"

// --- Cache Line Flushing and Memory Barriers (from cacheline.h) ---
#define CXL_CACHE_LINE_SIZE 64

/**
 * cxl_flush_region() - Flush cache lines in a memory region
 * @ptr: Pointer to the start of the region
 * @size: Size of the region in bytes
 *
 * Flushes all cache lines in the specified region using clflushopt.
 * This ensures that data written to CXL memory is pushed out of the
 * CPU cache and becomes visible to other hosts.
 */
static inline void cxl_flush_region(void *ptr, size_t size)
{
	char *p = (char *)ptr;
	size_t i;

	for (i = 0; i < size; i += CXL_CACHE_LINE_SIZE) {
		/* Use clflushopt to flush cache line to memory */
		asm volatile("clflushopt %0" : "+m" (*(volatile char *)(p + i)));
	}
}

/**
 * cxl_flush_and_sfence() - Flush region and issue store fence
 * @ptr: Pointer to the start of the region
 * @size: Size of the region in bytes
 *
 * Flushes cache lines and then issues an sfence to ensure all stores
 * are globally visible before proceeding.
 */
static inline void cxl_flush_and_sfence(void *ptr, size_t size)
{
	cxl_flush_region(ptr, size);
	/* Store fence to ensure flush completes before subsequent operations */
	asm volatile("sfence" ::: "memory");
}

/**
 * cxl_flush_and_mfence() - Flush region and issue full memory fence
 * @ptr: Pointer to the start of the region
 * @size: Size of the region in bytes
 *
 * Flushes cache lines and then issues an mfence for full memory ordering.
 */
static inline void cxl_flush_and_mfence(void *ptr, size_t size)
{
	cxl_flush_region(ptr, size);
	/* Full memory fence for complete ordering */
	asm volatile("mfence" ::: "memory");
}

/**
 * cxl_invalidate_region() - Invalidate cache lines for reading
 * @ptr: Pointer to the start of the region
 * @size: Size of the region in bytes
 *
 * Evicts cache lines to ensure subsequent reads see fresh data from CXL memory.
 * Used by readers to see updates from other hosts.
 */
static inline void cxl_invalidate_region(void *ptr, size_t size)
{
	char *p = (char *)ptr;
	size_t i;

	for (i = 0; i < size; i += CXL_CACHE_LINE_SIZE) {
		asm volatile("clflushopt %0" : "+m" (*(volatile char *)(p + i)));
	}
	/* Load fence to ensure subsequent reads are fresh */
	asm volatile("lfence" ::: "memory");
}


// --- Module Parameters ---
static char *dax_path = "/dev/dax0.0";
module_param(dax_path, charp, 0644);
MODULE_PARM_DESC(dax_path, "Path to the DAX device (e.g., /dev/dax0.0)");

static unsigned long dax_size_mb = 131072; // default 128 GiB
module_param(dax_size_mb, ulong, 0644);
MODULE_PARM_DESC(dax_size_mb, "Size of DAX mapping in MB");

static unsigned long dax_phys = 0; // physical base address of the DAX region
module_param(dax_phys, ulong, 0644);
MODULE_PARM_DESC(dax_phys, "Physical base address of DAX region");

// --- Internal structures ---
#define CXL_BLOCK_MAGIC 0xDABBADF00DCAFEFEULL

/**
 * struct cxl_block_header - Header for each allocated/free block
 * @magic: Magic number to identify valid blocks
 * @size: Size of the block in bytes (excluding header)
 */
struct cxl_block_header {
    u64 magic;
    size_t size;
};

/**
 * struct cxl_free_block - Node for free block linked list
 * @link: List head for linking free blocks
 */
struct cxl_free_block {
    struct list_head link;
};

/*
* struct cxl_pool - Represents the CXL memory pool
* @addr: Base virtual address of the CXL memory region
* @size: Size of the CXL memory region in bytes
* @freelist: Linked list of free blocks
* @lock: Spinlock to protect access to the freelist     
* @alloc_count: Total number of allocations made
* @free_count: Total number of frees made
* @bytes_allocated: Current total bytes allocated
* @fallback_count: Number of times allocation fell back to kmalloc
* @flush_count: Number of cache flush operations performed
*/
static struct {
    void *addr;
    size_t size;
    struct list_head freelist;
    spinlock_t lock;
    atomic64_t alloc_count; // Tracks cxl_allocs
    atomic64_t free_count; // Tracks cxl_frees
    atomic64_t bytes_allocated;
    atomic64_t fallback_count;  // Tracks kmalloc fallbacks
    atomic64_t flush_count;  // Track number of flush operations
} cxl_pool;

// --- sysfs interface --- this will helps userspace monitor the allocator
static struct kobject *cxl_kobj;

// Now, we need to provide the user with stats
static ssize_t stats_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	ssize_t len = 0;
	
	len += sprintf(buf + len, "CXL Allocator Statistics\n");
	len += sprintf(buf + len, "========================\n");
	len += sprintf(buf + len, "Pool address:     %p\n", cxl_pool.addr);
	len += sprintf(buf + len, "Pool size:        %zu MB\n", 
		       cxl_pool.size / (1024 * 1024));
	len += sprintf(buf + len, "Total allocs:     %lld\n", 
		       (long long)atomic64_read(&cxl_pool.alloc_count));
	len += sprintf(buf + len, "Total frees:      %lld\n", 
		       (long long)atomic64_read(&cxl_pool.free_count));
	len += sprintf(buf + len, "Current usage:    %lld bytes\n", 
		       (long long)atomic64_read(&cxl_pool.bytes_allocated));
	len += sprintf(buf + len, "Fallback count:   %lld\n", 
		       (long long)atomic64_read(&cxl_pool.fallback_count));
	len += sprintf(buf + len, "Cache flushes:    %lld\n", 
		       (long long)atomic64_read(&cxl_pool.flush_count));
	len += sprintf(buf + len, "Status:           %s\n",
		       cxl_pool.addr ? "ACTIVE (using CXL)" : "INACTIVE (using kmalloc)");
	
	return len;
}

static struct kobj_attribute stats_attribute = __ATTR_RO(stats);

// The following 3 functions manage the sysfs interface
static int cxl_sysfs_init(void)
{
    int ret;
    
    // Create /sys/kernel/cxl_allocator/
    cxl_kobj = kobject_create_and_add("cxl_allocator", kernel_kobj);
    if (!cxl_kobj) {
        pr_err("cxl_alloc: Failed to create sysfs kobject\n");
        return -ENOMEM;
    }
    
    // Create at /sys/kernel/cxl_allocator/stats ~ cat /sys/kernel/cxl_allocator/stats
    ret = sysfs_create_file(cxl_kobj, &stats_attribute.attr);
    if (ret) {
        pr_err("cxl_alloc: Failed to create sysfs file\n");
        kobject_put(cxl_kobj);
        return ret;
    }
    
    pr_info("cxl_alloc: sysfs interface created at /sys/kernel/cxl_allocator/stats\n");
    return 0;
}

static void cxl_sysfs_exit(void)
{
    if (cxl_kobj) {
        sysfs_remove_file(cxl_kobj, &stats_attribute.attr);
        kobject_put(cxl_kobj);
    }
}

// --- Track fallback ---
void cxl_track_fallback(void)
{
    atomic64_inc(&cxl_pool.fallback_count);
}
EXPORT_SYMBOL(cxl_track_fallback);

// --- Allocation/free ---
void *cxl_malloc(size_t size)
{
    pr_info("cxl_malloc: Requesting allocation of size %zu\n", size);
    
    struct cxl_block_header *hdr;
    struct cxl_free_block *free_block, *found_block = NULL;
    void *ptr = NULL;
    unsigned long flags;
    size_t aligned_size = ALIGN(size, sizeof(void *)); // Align to pointer size

    // Return NULL if pool is not initialized
    if (!cxl_pool.addr) {
        pr_warn("cxl_malloc: Pool not initialized\n");
        return NULL;
    }

    spin_lock_irqsave(&cxl_pool.lock, flags); // Protect freelist from concurrent access
    
    /* Search for a suitable free block:
    * It iterates the kernel linked list looking for the first block available that fits the request.
    * To do so: a valid magic and a size at least as large as the aligned request.
    */
    list_for_each_entry(free_block, &cxl_pool.freelist, link) {
        hdr = (struct cxl_block_header *)free_block - 1; // Compute header pointer address of the candidate block

        /* Invalidate header cache to see updates from other hosts */
		cxl_invalidate_region(hdr, sizeof(struct cxl_block_header));
        
        if (hdr->magic != CXL_BLOCK_MAGIC)
            continue;
        if (hdr->size >= aligned_size) {
            found_block = free_block;
            break; // Found a suitable block
        }
    }

    // If found, remove from freelist and return
    if (found_block) {
        list_del(&found_block->link);
        hdr = (struct cxl_block_header *)found_block - 1;
        ptr = (void *)(hdr + 1); // Pointer to user memory

        /* Update statistics */
		atomic64_inc(&cxl_pool.alloc_count);
		atomic64_add(aligned_size, &cxl_pool.bytes_allocated);

        /*
		 * CRITICAL: Mark block as allocated by updating header
		 * and flush to ensure visibility across hosts (cacheline.h)
		 */
		hdr->magic = CXL_BLOCK_MAGIC;  // Reconfirm magic - they may have changed
		cxl_flush_and_sfence(hdr, sizeof(struct cxl_block_header));
		atomic64_inc(&cxl_pool.flush_count);
    }
    // Release lock and return
    spin_unlock_irqrestore(&cxl_pool.lock, flags); 

    pr_info("cxl_malloc: Allocated %zu bytes at %p\n", size, ptr);

    return ptr;
}
EXPORT_SYMBOL(cxl_malloc);

void cxl_free(void *ptr)
{
    pr_info("cxl_free: Requesting free of memory at %p\n", ptr);
    // The goal of this function is to return a previously allocated block 
    // to the CXL-backed pool and update local accounting and cross-host visibility.

    struct cxl_block_header *hdr;
    struct cxl_free_block *free_node;
    unsigned long flags;

    if (!ptr) return; // Ignore NULL frees

    // Do nothing if pool is not initialized
    if (!cxl_pool.addr) {
        return;
    } // otherwise, if we try, it will cause a fault

    // Check if pointer is within CXL pool range
    if (ptr < cxl_pool.addr || 
        ptr >= (cxl_pool.addr + cxl_pool.size)) {
        return;  // Not a CXL allocation, don't try to free it
    }

    hdr = (struct cxl_block_header *)ptr - 1; // computes the block header 

    /* Invalidate to see current state */
	cxl_invalidate_region(hdr, sizeof(struct cxl_block_header)); // to observe any remote updates

    if (hdr->magic != CXL_BLOCK_MAGIC)
        return; 
    free_node = (struct cxl_free_block *)ptr;
    
    spin_lock_irqsave(&cxl_pool.lock, flags);
	list_add(&free_node->link, &cxl_pool.freelist);
	spin_unlock_irqrestore(&cxl_pool.lock, flags);

    /*
	 * CRITICAL: Flush the free block header and freelist changes
	 * to make the freed memory visible to other hosts
	 */
	cxl_flush_and_sfence(free_node, sizeof(struct cxl_free_block));
	cxl_flush_and_sfence(hdr, sizeof(struct cxl_block_header));
	atomic64_add(2, &cxl_pool.flush_count);

	/* Update statistics */
	atomic64_inc(&cxl_pool.free_count);
	atomic64_sub(hdr->size, &cxl_pool.bytes_allocated);
}
EXPORT_SYMBOL(cxl_free);

// --- Allocator Initialization ---
int cxl_pool_init(void)
{
    struct cxl_block_header *initial_block;
    struct cxl_free_block *free_node;
    
    pr_info("cxl_alloc: Attempting to initialize CXL pool\n");

    spin_lock_init(&cxl_pool.lock);
    INIT_LIST_HEAD(&cxl_pool.freelist);
    
    // check whether or not we have a dax device at dax_path (kernel-level code for open)
    pr_warn("cxl_alloc: Checking for DAX device at %s\n", dax_path);
    
    // This is the kernel equivalent of open()
    struct file *filp = filp_open(dax_path, O_RDWR | O_CLOEXEC, 0);
    if (IS_ERR(filp)) {
        pr_warn("cxl_alloc: Could not open DAX device at %s, using fallback\n", dax_path);
        cxl_pool.addr = NULL;
        cxl_pool.size = 0;
        return 0;  // Not an error, just no CXL
    }
    filp_close(filp, NULL);
    pr_info("cxl_alloc: DAX device %s found\n", dax_path);
    
    unsigned long cxl_phys_addr = 0x1000000000;
    size_t cxl_size = 137438953472; // 128 GiB

    if (cxl_phys_addr == 0) {
        pr_warn("cxl_alloc: No CXL physical address specified, using fallback\n");
        cxl_pool.addr = NULL;
        cxl_pool.size = 0;
        return 0;  // Not an error, just no CXL
    }

    atomic64_set(&cxl_pool.alloc_count, 0);
    atomic64_set(&cxl_pool.free_count, 0);
    atomic64_set(&cxl_pool.bytes_allocated, 0);
    atomic64_set(&cxl_pool.fallback_count, 0);
    atomic64_set(&cxl_pool.flush_count, 0);

    // According to cacheline.h, map CXL memory using MEMREMAP_WB for cacheable access
    cxl_pool.size = cxl_size;
    cxl_pool.addr = memremap(cxl_phys_addr, cxl_pool.size, MEMREMAP_WB);
	if (!cxl_pool.addr) {
		pr_err("cxl_alloc: memremap failed for phys=0x%lx size=%zu\n", 
		       cxl_phys_addr, cxl_pool.size);
		cxl_pool.size = 0;
		return 0;
	}

    if (cxl_pool.size < sizeof(struct cxl_block_header) + sizeof(struct cxl_free_block)) {
        pr_err("cxl_alloc: CXL pool too small\n");
        memunmap(cxl_pool.addr);
        cxl_pool.addr = NULL;
        cxl_pool.size = 0;
        return 0;
    }

    /* Initialize the free list with one big block */
	initial_block = (struct cxl_block_header *)cxl_pool.addr;
	initial_block->magic = CXL_BLOCK_MAGIC;
	initial_block->size = cxl_pool.size - sizeof(struct cxl_block_header);

	/* CRITICAL: Flush the header to CXL memory for cross-host visibility */
	cxl_flush_and_mfence(initial_block, sizeof(struct cxl_block_header));
	atomic64_inc(&cxl_pool.flush_count);

    free_node = (struct cxl_free_block *)(initial_block + 1);
    list_add(&free_node->link, &cxl_pool.freelist);

    pr_info("cxl_alloc: CXL pool initialized. VA=%p, Size=%zu MB (Phys=0x%lx)\n",
            cxl_pool.addr, cxl_pool.size / (1024 * 1024), cxl_phys_addr);

    cxl_sysfs_init();

    return 0;
}

void cxl_pool_exit(void)
{
    cxl_sysfs_exit();

    if (cxl_pool.addr) {
        memunmap(cxl_pool.addr);
    }

    pr_info("cxl_alloc: CXL pool cleanup complete\n");
}