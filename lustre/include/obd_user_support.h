#ifndef _OBD_SUPPORT_USER_H
#define _OBD_SUPPORT_USER_H

/*
 * User-space replacement for Lustre's obd_support.h
 * - Redirects kernel allocators (kmalloc/vmalloc/kfree) to malloc/free
 * - Provides hooks so you can later plug in a custom CXL allocator
 *
 * Compile with GCC + glibc in user space.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------
 * Optional allocator hooks
 * By default, map to malloc/calloc/free. Replace with your own
 * functions (e.g., CXL pool allocator) later.
 * ------------------------------------------------------------------ */
#ifndef OBD_MALLOC_FN
#define OBD_MALLOC_FN(size)       malloc(size)
#endif

#ifndef OBD_CALLOC_FN
#define OBD_CALLOC_FN(n, size)    calloc((n), (size))
#endif

#ifndef OBD_FREE_FN
#define OBD_FREE_FN(ptr)          free(ptr)
#endif

/* ------------------------------------------------------------------
 * Macros for allocation/free
 * ------------------------------------------------------------------ */

/* Allocate and zero memory */
#define OBD_ALLOC(ptr, size) do {                          \
    (ptr) = OBD_MALLOC_FN(size);                           \
    if ((ptr) != NULL) memset((ptr), 0, (size));           \
} while (0)

/* Allocate an array and zero it */
#define OBD_ALLOC_PTRARRAY(ptr, n, type) do {              \
    (ptr) = (type *)OBD_CALLOC_FN((n), sizeof(type));      \
} while (0)

/* Free memory */
#define OBD_FREE(ptr, size) do {                           \
    if ((ptr) != NULL) {                                   \
        OBD_FREE_FN(ptr);                                  \
        (ptr) = NULL;                                      \
    }                                                      \
} while (0)

/* "Large" allocations — just malloc in user space */
#define OBD_ALLOC_LARGE(ptr, size) OBD_ALLOC(ptr, size)
#define OBD_FREE_LARGE(ptr, size)  OBD_FREE(ptr, size)

/* Slab allocations — in user space just use malloc/free */
#define OBD_SLAB_ALLOC(ptr, cachep, size) OBD_ALLOC(ptr, size)
#define OBD_SLAB_FREE(ptr, cachep, size)  OBD_FREE(ptr, size)

/* For alignment testing, map to malloc (can replace later) */
#define OBD_VALLOC(ptr, size) OBD_ALLOC(ptr, size)
#define OBD_VFREE(ptr, size)  OBD_FREE(ptr, size)

/* ------------------------------------------------------------------
 * Debug helpers
 * ------------------------------------------------------------------ */
#define OBD_ALLOC_DEBUG(ptr, size, where) do {             \
    (ptr) = OBD_MALLOC_FN(size);                           \
    if ((ptr) != NULL) memset((ptr), 0, (size));           \
    else fprintf(stderr, "OBD_ALLOC failed at %s\n", where); \
} while (0)

#endif /* _OBD_SUPPORT_USER_H */
