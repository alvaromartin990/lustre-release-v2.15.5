/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CXL Kernel Memory Allocator - Simplified Version
 * 
 * This is a minimal version that uses regular kernel memory
 * for testing the allocation logic without DAX dependencies.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include "cxl_kmem_allocator.h"

/* Module parameters */
static char *cxl_dax_device = "/dev/dax0.0";
module_param(cxl_dax_device, charp, 0644);
MODULE_PARM_DESC(cxl_dax_device, "CXL DAX device path (unused in simple version)");

static bool enable_fallback = true;
module_param(enable_fallback, bool, 0644);
MODULE_PARM_DESC(enable_fallback, "Enable fallback to system memory");

static int pool_size_mb = 64;
module_param(pool_size_mb, int, 0644);
MODULE_PARM_DESC(pool_size_mb, "Test pool size in MB (default 64MB)");

/* Global CXL memory pool */
struct cxl_memory_pool cxl_pool = {0};
EXPORT_SYMBOL(cxl_pool);

/* Static prototypes */
static struct cxl_mem_chunk *cxl_find_free_chunk(size_t size);
static struct cxl_mem_chunk *cxl_split_chunk(struct cxl_mem_chunk *chunk, size_t size);
static void cxl_merge_free_chunks(void);

/**
 * cxl_kmem_init - Initialize the CXL memory allocator
 */
int cxl_kmem_init(void)
{
	struct cxl_mem_chunk *initial_chunk;
	size_t total_size = (size_t)pool_size_mb * 1024 * 1024;
	
	pr_info("CXL Memory Allocator: Initializing simple test version...\n");
	pr_info("CXL: Creating %dMB test pool using vmalloc\n", pool_size_mb);
	pr_info("CXL: DAX device %s will be used in full version\n", cxl_dax_device);
	
	/* Initialize pool structure */
	memset(&cxl_pool, 0, sizeof(cxl_pool));
	INIT_LIST_HEAD(&cxl_pool.free_chunks);
	INIT_LIST_HEAD(&cxl_pool.allocated_chunks);
	spin_lock_init(&cxl_pool.pool_lock);
	
	atomic64_set(&cxl_pool.alloc_count, 0);
	atomic64_set(&cxl_pool.free_count, 0);
	atomic64_set(&cxl_pool.alloc_bytes, 0);
	atomic64_set(&cxl_pool.fallback_count, 0);
	
	cxl_pool.fallback_enabled = enable_fallback;
	
	/* Allocate test pool using vmalloc */
	cxl_pool.base_addr = vmalloc(total_size);
	if (!cxl_pool.base_addr) {
		pr_err("CXL: Failed to allocate test pool\n");
		return -ENOMEM;
	}
	
	cxl_pool.phys_base = 0; /* Not relevant for vmalloc */
	cxl_pool.total_size = total_size;
	cxl_pool.allocated_size = 0;
	
	/* Create initial free chunk covering the entire region */
	initial_chunk = kzalloc(sizeof(*initial_chunk), GFP_KERNEL);
	if (!initial_chunk) {
		vfree(cxl_pool.base_addr);
		cxl_pool.base_addr = NULL;
		return -ENOMEM;
	}
	
	INIT_LIST_HEAD(&initial_chunk->list);
	initial_chunk->size = total_size;
	initial_chunk->offset = 0;
	initial_chunk->vaddr = cxl_pool.base_addr;
	initial_chunk->magic = CXL_CHUNK_MAGIC;
	atomic_set(&initial_chunk->ref_count, 0);
	
	list_add(&initial_chunk->list, &cxl_pool.free_chunks);
	
	pr_info("CXL Memory Allocator: Initialized %zu bytes at %p\n", 
		cxl_pool.total_size, cxl_pool.base_addr);
	
	return 0;
}
EXPORT_SYMBOL(cxl_kmem_init);

/**
 * cxl_kmem_exit - Cleanup the CXL memory allocator
 */
void cxl_kmem_exit(void)
{
	struct cxl_mem_chunk *chunk, *tmp;
	unsigned long flags;
	
	pr_info("CXL Memory Allocator: Shutting down...\n");
	
	spin_lock_irqsave(&cxl_pool.pool_lock, flags);
	
	/* Free all allocated chunks */
	list_for_each_entry_safe(chunk, tmp, &cxl_pool.allocated_chunks, list) {
		pr_warn("CXL: Memory leak detected: %p size %zu\n", 
			chunk->vaddr, chunk->size);
		list_del(&chunk->list);
		kfree(chunk);
	}
	
	/* Free all free chunks */
	list_for_each_entry_safe(chunk, tmp, &cxl_pool.free_chunks, list) {
		list_del(&chunk->list);
		kfree(chunk);
	}
	
	spin_unlock_irqrestore(&cxl_pool.pool_lock, flags);
	
	/* Free the test pool */
	if (cxl_pool.base_addr) {
		vfree(cxl_pool.base_addr);
		cxl_pool.base_addr = NULL;
	}
	
	pr_info("CXL Memory Allocator: Shutdown complete\n");
}
EXPORT_SYMBOL(cxl_kmem_exit);

/**
 * cxl_find_free_chunk - Find a suitable free chunk
 */
static struct cxl_mem_chunk *cxl_find_free_chunk(size_t size)
{
	struct cxl_mem_chunk *chunk;
	
	list_for_each_entry(chunk, &cxl_pool.free_chunks, list) {
		if (chunk->size >= size)
			return chunk;
	}
	return NULL;
}

/**
 * cxl_split_chunk - Split a chunk if it's larger than needed
 */
static struct cxl_mem_chunk *cxl_split_chunk(struct cxl_mem_chunk *chunk, size_t size)
{
	struct cxl_mem_chunk *new_chunk;
	
	if (chunk->size <= size + sizeof(*new_chunk) + 64)
		return chunk; /* Not worth splitting */
	
	new_chunk = kzalloc(sizeof(*new_chunk), GFP_ATOMIC);
	if (!new_chunk)
		return chunk; /* Can't split, return original */
	
	/* Setup the new chunk for the remaining space */
	INIT_LIST_HEAD(&new_chunk->list);
	new_chunk->size = chunk->size - size;
	new_chunk->offset = chunk->offset + size;
	new_chunk->vaddr = (char *)chunk->vaddr + size;
	new_chunk->magic = CXL_CHUNK_MAGIC;
	atomic_set(&new_chunk->ref_count, 0);
	
	/* Add the new chunk to free list */
	list_add(&new_chunk->list, &chunk->list);
	
	/* Shrink the original chunk */
	chunk->size = size;
	
	return chunk;
}

/**
 * cxl_kmalloc - CXL equivalent of kmalloc
 */
void *cxl_kmalloc(size_t size, gfp_t flags)
{
	struct cxl_mem_chunk *chunk;
	unsigned long lock_flags;
	void *ptr = NULL;
	size_t aligned_size;
	
	if (!size)
		return NULL;
	
	/* Align size to CXL requirements */
	aligned_size = ALIGN(size, CXL_ALIGNMENT);
	
	/* Check size limits */
	if (aligned_size > CXL_POOL_MAX_ALLOC_SIZE) {
		if (cxl_pool.fallback_enabled) {
			atomic64_inc(&cxl_pool.fallback_count);
			return kmalloc(size, flags);
		}
		return NULL;
	}
	
	spin_lock_irqsave(&cxl_pool.pool_lock, lock_flags);
	
	/* Find suitable free chunk */
	chunk = cxl_find_free_chunk(aligned_size);
	if (!chunk) {
		spin_unlock_irqrestore(&cxl_pool.pool_lock, lock_flags);
		
		/* Fallback to system memory if enabled */
		if (cxl_pool.fallback_enabled) {
			atomic64_inc(&cxl_pool.fallback_count);
			return kmalloc(size, flags);
		}
		return NULL;
	}
	
	/* Remove from free list */
	list_del(&chunk->list);
	
	/* Split chunk if too large */
	chunk = cxl_split_chunk(chunk, aligned_size);
	
	/* Add to allocated list */
	list_add(&chunk->list, &cxl_pool.allocated_chunks);
	
	atomic_set(&chunk->ref_count, 1);
	ptr = chunk->vaddr;
	
	/* Update statistics */
	atomic64_inc(&cxl_pool.alloc_count);
	atomic64_add(aligned_size, &cxl_pool.alloc_bytes);
	cxl_pool.allocated_size += aligned_size;
	
	spin_unlock_irqrestore(&cxl_pool.pool_lock, lock_flags);
	
	/* Clear memory if requested */
	if (flags & __GFP_ZERO)
		memset(ptr, 0, size);
	
	/* Memory fence to ensure CXL coherency */
	mb();
	
	return ptr;
}
EXPORT_SYMBOL(cxl_kmalloc);

/**
 * cxl_kzalloc - CXL equivalent of kzalloc
 */
void *cxl_kzalloc(size_t size, gfp_t flags)
{
	return cxl_kmalloc(size, flags | __GFP_ZERO);
}
EXPORT_SYMBOL(cxl_kzalloc);

/**
 * cxl_vmalloc - CXL equivalent of vmalloc
 */
void *cxl_vmalloc(size_t size)
{
	return cxl_kmalloc(size, GFP_KERNEL);
}
EXPORT_SYMBOL(cxl_vmalloc);

/**
 * cxl_vzalloc - CXL equivalent of vzalloc
 */
void *cxl_vzalloc(size_t size)
{
	return cxl_kmalloc(size, GFP_KERNEL | __GFP_ZERO);
}
EXPORT_SYMBOL(cxl_vzalloc);

/**
 * cxl_kfree - Free CXL allocated memory
 */
void cxl_kfree(const void *ptr)
{
	struct cxl_mem_chunk *chunk, *tmp;
	unsigned long flags;
	bool found = false;
	
	if (!ptr)
		return;
	
	/* Check if this is a CXL address */
	if (!is_cxl_address(ptr)) {
		/* Must be system memory, use regular kfree */
		kfree(ptr);
		return;
	}
	
	spin_lock_irqsave(&cxl_pool.pool_lock, flags);
	
	/* Find the chunk in allocated list */
	list_for_each_entry_safe(chunk, tmp, &cxl_pool.allocated_chunks, list) {
		if (chunk->vaddr == ptr) {
			if (chunk->magic != CXL_CHUNK_MAGIC) {
				pr_err("CXL: Corrupted chunk magic: %p\n", ptr);
				break;
			}
			
			/* Remove from allocated list */
			list_del(&chunk->list);
			
			/* Add to free list */
			list_add(&chunk->list, &cxl_pool.free_chunks);
			
			/* Update statistics */
			atomic64_inc(&cxl_pool.free_count);
			atomic64_sub(chunk->size, &cxl_pool.alloc_bytes);
			cxl_pool.allocated_size -= chunk->size;
			
			found = true;
			break;
		}
	}
	
	if (found) {
		/* Merge adjacent free chunks */
		cxl_merge_free_chunks();
	}
	
	spin_unlock_irqrestore(&cxl_pool.pool_lock, flags);
	
	if (!found) {
		pr_err("CXL: Attempt to free unknown pointer: %p\n", ptr);
	}
	
	/* Memory fence to ensure CXL coherency */
	mb();
}
EXPORT_SYMBOL(cxl_kfree);

/**
 * cxl_vfree - Free CXL allocated memory (same as kfree for our implementation)
 */
void cxl_vfree(const void *ptr)
{
	cxl_kfree(ptr);
}
EXPORT_SYMBOL(cxl_vfree);

/**
 * cxl_merge_free_chunks - Merge adjacent free chunks
 */
static void cxl_merge_free_chunks(void)
{
	struct cxl_mem_chunk *chunk, *next_chunk, *tmp;
	
	list_for_each_entry_safe(chunk, tmp, &cxl_pool.free_chunks, list) {
		list_for_each_entry(next_chunk, &cxl_pool.free_chunks, list) {
			if (chunk == next_chunk)
				continue;
				
			/* Check if chunks are adjacent */
			if ((char *)chunk->vaddr + chunk->size == next_chunk->vaddr) {
				/* Merge next_chunk into chunk */
				chunk->size += next_chunk->size;
				list_del(&next_chunk->list);
				kfree(next_chunk);
				break;
			}
		}
	}
}

/**
 * Pool management functions
 */
size_t cxl_pool_get_free_size(void)
{
	return cxl_pool.total_size - cxl_pool.allocated_size;
}
EXPORT_SYMBOL(cxl_pool_get_free_size);

size_t cxl_pool_get_allocated_size(void)
{
	return cxl_pool.allocated_size;
}
EXPORT_SYMBOL(cxl_pool_get_allocated_size);

/**
 * Module init/exit functions
 */
static int __init cxl_allocator_init(void)
{
	return cxl_kmem_init();
}

static void __exit cxl_allocator_exit(void)
{
	cxl_kmem_exit();
}

module_init(cxl_allocator_init);
module_exit(cxl_allocator_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CXL Development Team");
MODULE_DESCRIPTION("CXL Memory Allocator for Lustre - Simple Version");
MODULE_VERSION("1.0-simple");