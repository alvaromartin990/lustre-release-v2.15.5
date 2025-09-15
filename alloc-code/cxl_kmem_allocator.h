/* SPDX-License-Identifier: GPL-2.0 */
/*
 * CXL Kernel Memory Allocator for Lustre
 * 
 * This header defines the interface for a kernel-space CXL memory allocator
 * that can be used as a drop-in replacement for kmalloc/vmalloc in Lustre.
 */

#ifndef _CXL_KMEM_ALLOCATOR_H
#define _CXL_KMEM_ALLOCATOR_H

#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/spinlock.h>
#include <linux/list.h>

/* CXL Memory Pool Configuration */
#define CXL_POOL_MAX_SIZE        (128ULL << 30)  /* 128GB as per your env */
#define CXL_POOL_MIN_ALLOC_SIZE  64              /* Minimum allocation unit */
#define CXL_POOL_MAX_ALLOC_SIZE  (64 << 20)     /* 64MB max single allocation */
#define CXL_ALIGNMENT            64              /* CXL alignment requirement */

/* Memory chunk header for tracking allocations */
struct cxl_mem_chunk {
	struct list_head list;
	size_t size;
	unsigned long offset;
	void *vaddr;
	atomic_t ref_count;
	unsigned long magic;
#define CXL_CHUNK_MAGIC 0xCEEEFACE
};

/* CXL Memory Pool Structure */
struct cxl_memory_pool {
	void __iomem *base_addr;        /* ioremap'd CXL memory base */
	phys_addr_t phys_base;          /* Physical address of CXL region */
	size_t total_size;              /* Total pool size */
	size_t allocated_size;          /* Currently allocated bytes */
	
	/* Free list management */
	struct list_head free_chunks;   /* List of free chunks */
	struct list_head allocated_chunks; /* List of allocated chunks */
	spinlock_t pool_lock;           /* Protects pool operations */
	
	/* Statistics */
	atomic64_t alloc_count;
	atomic64_t free_count;
	atomic64_t alloc_bytes;
	
	/* Fallback to system memory if CXL fails */
	bool fallback_enabled;
	atomic64_t fallback_count;
};

/* Public API Functions */
int cxl_kmem_init(void);
void cxl_kmem_exit(void);

void *cxl_kmalloc(size_t size, gfp_t flags);
void *cxl_kzalloc(size_t size, gfp_t flags);
void *cxl_vmalloc(size_t size);
void *cxl_vzalloc(size_t size);
void cxl_kfree(const void *ptr);
void cxl_vfree(const void *ptr);

/* Pool management functions */
int cxl_pool_add_region(phys_addr_t phys_addr, size_t size);
void cxl_pool_remove_region(phys_addr_t phys_addr);
size_t cxl_pool_get_free_size(void);
size_t cxl_pool_get_allocated_size(void);

/* Memory fencing for CXL (required for cache coherency) */
static inline void cxl_memory_fence(void)
{
	mb();  /* Full memory barrier */
	asm volatile("sfence" ::: "memory"); /* Store fence for x86 */
}

/* Utility macros */
#define cxl_ptr_to_chunk(ptr) \
	container_of((struct list_head *)((char *)(ptr) - sizeof(struct cxl_mem_chunk)), \
		     struct cxl_mem_chunk, list)

#define is_cxl_address(ptr) \
	(((unsigned long)(ptr) >= (unsigned long)cxl_pool.base_addr) && \
	 ((unsigned long)(ptr) < (unsigned long)cxl_pool.base_addr + cxl_pool.total_size))

/* Debug and statistics */
#ifdef CONFIG_CXL_ALLOCATOR_DEBUG
void cxl_dump_pool_stats(void);
void cxl_dump_allocated_chunks(void);
#else
static inline void cxl_dump_pool_stats(void) {}
static inline void cxl_dump_allocated_chunks(void) {}
#endif

extern struct cxl_memory_pool cxl_pool;

#endif /* _CXL_KMEM_ALLOCATOR_H */