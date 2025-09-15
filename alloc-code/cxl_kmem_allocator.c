/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CXL Kernel Memory Allocator Implementation
 * 
 * Provides kernel-space CXL memory allocation compatible with Lustre's
 * existing memory allocation patterns.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/dax.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/memremap.h>
#include <linux/pfn_t.h>
#include <linux/align.h>

#include "cxl_kmem_allocator.h"

/* Module parameters */
static char *cxl_dax_device = "/dev/dax0.0";
module_param(cxl_dax_device, charp, 0644);
MODULE_PARM_DESC(cxl_dax_device, "CXL DAX device path");

static bool enable_fallback = true;
module_param(enable_fallback, bool, 0644);
MODULE_PARM_DESC(enable_fallback, "Enable fallback to system memory");

/* Global CXL memory pool */
struct cxl_memory_pool cxl_pool = {0};
EXPORT_SYMBOL(cxl_pool);

/* Static prototypes */
static struct cxl_mem_chunk *cxl_find_free_chunk(size_t size);
static struct cxl_mem_chunk *cxl_split_chunk(struct cxl_mem_chunk *chunk, size_t size);
static void cxl_merge_free_chunks(void);
static int cxl_setup_dax_mapping(const char *dax_path);

/**
 * cxl_kmem_init - Initialize the CXL memory allocator
 */
int cxl_kmem_init(void)
{
	int ret = 0;
	
	pr_info("CXL Memory Allocator: Initializing...\n");
	
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
	
	/* Setup DAX device mapping */
	ret = cxl_setup_dax_mapping(cxl_dax_device);
	if (ret) {
		pr_err("CXL Memory Allocator: Failed to setup DAX mapping: %d\n", ret);
		goto err_setup;
	}
	
	pr_info("CXL Memory Allocator: Initialized %zu bytes at %p\n", 
		cxl_pool.total_size, cxl_pool.base_addr);
	
	return 0;

err_setup:
	return ret;
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
	
	/* Unmap the CXL region */
	if (cxl_pool.base_addr) {
		iounmap(cxl_pool.base_addr);
		cxl_pool.base_addr = NULL;
	}
	
	pr_info("CXL Memory Allocator: Shutdown complete\n");
}
EXPORT_SYMBOL(cxl_kmem_exit);

/**
 * cxl_setup_dax_mapping - Setup memory mapping for CXL DAX device
 */
static int cxl_setup_dax_mapping(const char *dax_path)
{
	struct file *dax_file;
	struct inode *inode;
	struct cxl_mem_chunk *initial_chunk;
	loff_t size;
	phys_addr_t phys_addr;
	int ret = 0;
	
	/* Open the DAX device */
	dax_file = filp_open(dax_path, O_RDWR, 0);
	if (IS_ERR(dax_file)) {
		pr_err("CXL: Cannot open DAX device %s: %ld\n", 
		       dax_path, PTR_ERR(dax_file));
		return PTR_ERR(dax_file);
	}
	
	inode = file_inode(dax_file);
	size = i_size_read(inode);
	
	if (size <= 0 || size > CXL_POOL_MAX_SIZE) {
		pr_err("CXL: Invalid DAX device size: %lld\n", size);
		ret = -EINVAL;
		goto close_file;
	}
	
	/* Get physical address - for DAX devices this is straightforward */
	phys_addr = 0; /* This would need to be obtained from the DAX device */
	/* In a real implementation, you'd use dax_direct_access() or similar */
	
	/* For now, we'll use a simplified approach with ioremap */
	/* In production, you'd want to use devm_memremap() or dax_direct_access() */
	cxl_pool.phys_base = phys_addr;
	cxl_pool.total_size = size;
	
	/* Map the entire CXL region */
	cxl_pool.base_addr = ioremap_wc(phys_addr, size);
	if (!cxl_pool.base_addr) {
		pr_err("CXL: Failed to map CXL memory region\n");
		ret = -ENOMEM;
		goto close_file;
	}
	
	/* Create initial free chunk covering the entire region */
	initial_chunk = kzalloc(sizeof(*initial_chunk), GFP_KERNEL);
	if (!initial_chunk) {
		ret = -ENOMEM;
		goto unmap_region;
	}
	
	INIT_LIST_HEAD(&initial_chunk->list);
	initial_chunk->size = size;
	initial_chunk->offset = 0;
	initial_chunk->vaddr = cxl_pool.base_addr;
	initial_chunk->magic = CXL_CHUNK_MAGIC;
	atomic_set(&initial_chunk->ref_count, 0);
	
	list_add(&initial_chunk->list, &cxl_pool.free_chunks);
	
	filp_close(dax_file, NULL);
	return 0;

unmap_region:
	iounmap(cxl_pool.base_addr);
	cxl_pool.base_addr = NULL;
close_file:
	filp_close(dax_file, NULL);
	return ret;
}

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
	
	if (chunk->size <= size + sizeof(*new_chunk))
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
	cxl_memory_fence();
	
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
	/* For large allocations, use the same underlying mechanism */
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
	cxl_memory_fence();
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
MODULE_AUTHOR("Alvaro");
MODULE_DESCRIPTION("CXL Memory Allocator for Lustre");
MODULE_VERSION("1.0");