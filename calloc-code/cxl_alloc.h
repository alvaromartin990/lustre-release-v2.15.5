/* cxl_alloc.h
 *
 * Simple CXL/DAX-backed allocator API for prototypes.
 *
 * NOTE: This is a small prototype API — suitable for experimenting with
 * mapping a dev-dax/CXL region into the kernel and allocating from it.
 *
 * See accompanying cxl_alloc.c for implementation and usage notes.
 */

#ifndef _CXL_ALLOC_H
#define _CXL_ALLOC_H

#include <linux/types.h>

int  cxl_alloc_init(const char *dax_path, phys_addr_t fallback_phys, size_t fallback_size);
void cxl_alloc_exit(void);

void *cxl_malloc(size_t size);
void  cxl_free(void *ptr);

#endif /* _CXL_ALLOC_H */
