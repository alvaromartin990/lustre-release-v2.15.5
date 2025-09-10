#ifndef _LUSTRE_CXL_ALLOC_H
#define _LUSTRE_CXL_ALLOC_H

#include <linux/types.h>
#include <linux/gfp.h>

/* Public API for Lustre to request NUMA-node-aware allocations.
 *
 * cxl_kmalloc() / cxl_kzalloc() try to allocate on the requested node via
 * kmalloc_node() or kmem_cache_alloc_node(). If node == NUMA_NO_NODE or
 * allocation fails, they fall back to generic kmalloc()/vmalloc() paths.
 *
 * cxl_node_id: if >= 0, it can be used as a global default node by Lustre.
 */

void *cxl_kmalloc(size_t size, gfp_t flags, int node);
void *cxl_kzalloc(size_t size, gfp_t flags, int node);
void *cxl_kmalloc_large(size_t size, gfp_t flags, int node);
void cxl_kfree(const void *ptr);
int cxl_init_allocator(void);
void cxl_exit_allocator(void);

/* Helper to create per-object slab cache pinned to node */
struct kmem_cache *cxl_kmem_cache_create(const char *name, size_t size,
					unsigned int align, gfp_t flags,
					int node);
void cxl_kmem_cache_destroy(struct kmem_cache *cachep);
void *cxl_kmem_cache_alloc(struct kmem_cache *cachep, gfp_t flags, int node);
void cxl_kmem_cache_free(struct kmem_cache *cachep, void *obj);

#endif /* _LUSTRE_CXL_ALLOC_H */
