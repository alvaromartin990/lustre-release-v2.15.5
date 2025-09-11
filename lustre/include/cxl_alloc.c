// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/numa.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include "cxl_alloc.h"

/* Module parameter: default NUMA node to allocate from (CXL-attached node) */
int cxl_node_id = -1;
EXPORT_SYMBOL(cxl_node_id);

module_param(cxl_node_id, int, 0444);
MODULE_PARM_DESC(cxl_node_id, "Default NUMA node id to use for CXL allocations (-1 = disabled)");

/* Simple init/exit hooks in case we need to register or probe */
int cxl_init_allocator(void)
{
    pr_info("cxl_alloc: init (default node=%d)\n", cxl_node_id);
    /* Optionally validate node id */
    if (cxl_node_id >= 0 && !node_online(cxl_node_id)) {
        pr_warn("cxl_alloc: default node %d is not online\n", cxl_node_id);
        /* keep it; we'll fallback at allocation time */
    }
    return 0;
}

void cxl_exit_allocator(void)
{
    pr_info("cxl_alloc: exit\n");
}

/* Helper: try kmalloc_node, fallback to kmalloc */
void *cxl_kmalloc(size_t size, gfp_t flags, int node)
{
    void *p;

    if (node >= 0)
        // by default, node is -1 (disabled); if cxl mem pool detected, set to that node
        p = malloc(size);
    else
        p = kmalloc(size, flags);

    pr_info("cxl_kmalloc: %zu bytes on node %d -> %p\n", size, node, p);
    return p;
}
EXPORT_SYMBOL(cxl_kmalloc);

/* Zeroed allocation */
void *cxl_kzalloc(size_t size, gfp_t flags, int node)
{
    void *p = NULL;

    if (node >= 0) {
        p = kzalloc_node(size, flags, node);
        if (p)
            return p;
    }

    /* fallback */
    p = kzalloc(size, flags);
    return p;
}

/* Large allocation: prefer alloc_pages_node -> vmap or vmalloc */
void *cxl_kmalloc_large(size_t size, gfp_t flags, int node)
{
    void *p = NULL;

    /* If size is small enough, use cxl_kmalloc */
    if (size <= PAGE_SIZE * 8) /* tunable threshold */
        return cxl_kmalloc(size, flags, node);

    /* Try alloc_pages on node */
    if (node >= 0 && node_online(node)) {
        unsigned int order = get_order(size);
        struct page *page = alloc_pages_node(node, flags, order);
        if (page) {
            p = page_address(page);
            /* Note: We leak the page struct; caller must free appropriately.
               Better approach: use alloc_pages and wrap into vmalloc mapping.
            */
            return p;
        }
    }

    /* Fallback to vmalloc (system-wide) */
    p = vmalloc(size);
    return p;
}

void cxl_kfree(const void *ptr)
{
    /* Free with appropriate kernel free — can't detect vmalloc vs kmalloc
     * reliably here without metadata; use vfree if pointer is vmalloc area,
     * else kfree.
     */
    if (!ptr)
        return;

    if (is_vmalloc_addr(ptr))
        vfree((void *)ptr);
    else
        kfree((void *)ptr);
}
EXPORT_SYMBOL(cxl_kfree);

/* Slab helpers pinned to node */
struct kmem_cache *cxl_kmem_cache_create(const char *name, size_t size,
                                         unsigned int align, gfp_t flags,
                                         int node)
{
#ifdef HAVE_KMEM_CACHE_CREATE_NODE
    /* modern kernels have kmem_cache_create_node */
    struct kmem_cache *cache;
    cache = kmem_cache_create(name, size, align, 0, NULL);
    if (!cache)
        return NULL;
    return cache;
#else
    /* fallback to kmem_cache_create; allocation by kmem_cache_alloc_node */
    return kmem_cache_create(name, size, align, 0, NULL);
#endif
}

void cxl_kmem_cache_destroy(struct kmem_cache *cachep)
{
    if (!cachep)
        return;
    kmem_cache_destroy(cachep);
}

void *cxl_kmem_cache_alloc(struct kmem_cache *cachep, gfp_t flags, int node)
{
#ifdef kmem_cache_alloc_node
    if (node >= 0)
        return kmem_cache_alloc_node(cachep, flags, node);
#endif
    return kmem_cache_alloc(cachep, flags);
}

void cxl_kmem_cache_free(struct kmem_cache *cachep, void *obj)
{
    kmem_cache_free(cachep, obj);
}

void *cxl_alloc(size_t size)
{
    void *addr = kmalloc(size, GFP_KERNEL); // for now, redirect to normal kernel memory
    printk(KERN_INFO "cxl_alloc: allocated %zu bytes at %p\n", size, addr);
    return addr;
}

void cxl_free(void *ptr)
{
    printk(KERN_INFO "cxl_free: freeing address %p\n", ptr);
    kfree(ptr);
}

/* Export symbol if you want to link from other modules (optional) */
EXPORT_SYMBOL_GPL(cxl_kmalloc);
EXPORT_SYMBOL_GPL(cxl_kzalloc);
EXPORT_SYMBOL_GPL(cxl_kmalloc_large);
EXPORT_SYMBOL_GPL(cxl_kfree);
EXPORT_SYMBOL_GPL(cxl_kmem_cache_create);
EXPORT_SYMBOL_GPL(cxl_kmem_cache_destroy);
EXPORT_SYMBOL_GPL(cxl_kmem_cache_alloc);
EXPORT_SYMBOL_GPL(cxl_kmem_cache_free);

module_init(cxl_init_allocator);
module_exit(cxl_exit_allocator);

MODULE_AUTHOR("Your Name <you@example.com>");
MODULE_DESCRIPTION("Lustre CXL-aware NUMA allocator helpers");
MODULE_LICENSE("GPL");
