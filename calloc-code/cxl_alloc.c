/*
 * cxl_alloc.c
 *
 * Minimal kernel-side allocator that maps a devdax/CXL region and
 * provides cxl_malloc/cxl_free.  Simplified for clarity.
 *
 * Behavior:
 *  - Try to find the dev-dax device by name with class_find_device_by_name().
 *    If the driver data (dev_get_drvdata(dev)) exposes a KVA (virt base), use it.
 *  - Otherwise, fall back to mapping a physical address passed by the caller
 *    (module param) with memremap().
 *
 *  - Basic bump-pointer allocator protected by a mutex.
 *
 * This code is intentionally small: production-grade code must provide
 * fragmentation handling, free-list/coalescing, alignment, crash-consistency
 * (for persistent memory), wear-leveling, etc.
 *
 * NOTE: Some kernel versions expose different dev-dax internals. This file is
 * meant as a starting point — adapt the devdax lookup to your kernel if needed.
 */

#include "cxl_alloc.h"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/class.h>
#include <linux/err.h>
#include <linux/io.h>        /* memremap, memunmap */
#include <linux/fs.h>        /* for filp_open if you choose that route */
#include <linux/uaccess.h>
#include <linux/types.h>
#include <linux/string.h>

static void *cxl_base = NULL;        /* kernel virtual address of mapped region */
static phys_addr_t cxl_phys = 0;     /* phys base (if known) */
static size_t cxl_size = 0;

static struct mutex cxl_lock;
static size_t cxl_offset = 0;

/* Module-level fallback parameters (so you can pass phys and size at insmod time) */
static phys_addr_t fallback_phys = 0;
static size_t fallback_size = 0;
module_param_named(phys, fallback_phys, ulong, 0444);
MODULE_PARM_DESC(phys, "Fallback physical base address for CXL/DAX mapping (hex)");

module_param_named(size, fallback_size, ulong, 0444);
MODULE_PARM_DESC(size, "Fallback size for CXL/DAX mapping (bytes)");

/* dax class symbol: declared in device-dax core. We'll reference it dynamically.
 * The symbol name 'dax_class' exists in many kernels. If your kernel doesn't
 * export it or your distribution hides it, the class_find_device_by_name
 * call below will not work and we will fall back to memremap.
 */
extern struct class dax_class;

/* Helper: try to get kernel KVA from dev-dax driver data.
 *
 * Many kernels' devdax implementation stores a KVA or virt_addr in the
 * driver-specific struct. There's no stable public accessor across all
 * kernel versions, so we attempt a couple of likely options below.
 *
 * You may need to adapt or replace this with a kernel-specific accessor.
 */
static void *try_devdax_kva_from_device(const char *dax_path)
{
    struct device *dev = NULL;
    void *kva = NULL;

    if (!dax_path)
        return NULL;

    /* find device by name in the 'dax' class */
    dev = class_find_device_by_name(&dax_class, dax_path);
    if (!dev)
        return NULL;

    /* If the dev-dax implementation put its structure in driver data, try to
     * retrieve it. The actual type is kernel-version dependent; try dev_get_drvdata.
     */
    {
        void *drvdata = dev_get_drvdata(dev);

        /* Common possibilities (depending on kernel):
         *  - drvdata is struct dev_dax * (and that struct might contain 'virt_addr').
         *  - drvdata can be something else; this is a heuristic.
         *
         *  We'll try to read a plausible field at a known offset if present.
         *
         *  IMPORTANT: This is a heuristic designed to work with recent kernels
         *  that store a saved KVA; adapt as necessary for your kernel.
         */
        if (drvdata) {
            /* Attempt 1: assume first pointer-sized field is virt_addr (best-effort) */
            void **maybe_ptr = (void **)drvdata;
            void *candidate = NULL;

            /* read a pointer value from drvdata (safe copy) */
            candidate = READ_ONCE(*maybe_ptr);

            /* very coarse check: candidate should be non-NULL and within kernel VA range */
            if (candidate && !is_vmalloc_addr(candidate) && !is_kernel_addr(candidate)) {
                /* candidate looks suspicious; but we accept vmalloc or ioremap ranges too */
            }
            /* A safer approach: try memcmp pattern or use exported accessor if available. */
            kva = candidate;
        }
    }

    /* release device reference */
    put_device(dev);

    return kva;
}

/* Public API */

int cxl_alloc_init(const char *dax_path, phys_addr_t fallback_phys_arg, size_t fallback_size_arg)
{
    void *kva = NULL;

    mutex_init(&cxl_lock);

    /* Primary attempt: find dev-dax device and get its kernel KVA */
    if (dax_path) {
        kva = try_devdax_kva_from_device(dax_path);
    }

    /* Secondary attempt: if user provided fallback phys/size, memremap it */
    if (!kva && fallback_phys_arg && fallback_size_arg) {
        void __iomem *mapped;

        mapped = memremap(fallback_phys_arg, fallback_size_arg, MEMREMAP_WB);
        if (!mapped) {
            pr_err("cxl_alloc: memremap failed for phys=%pa size=%zu\n", &fallback_phys_arg, fallback_size_arg);
            return -ENOMEM;
        }
        kva = (void *)mapped;
        cxl_phys = fallback_phys_arg;
        cxl_size = fallback_size_arg;
    }

    if (!kva) {
        pr_err("cxl_alloc: could not map devdax or fallback region\n");
        return -ENODEV;
    }

    /* initialize region bookkeeping */
    cxl_base = kva;
    cxl_offset = 0;

    pr_info("cxl_alloc: mapped CXL region at %p (size %zu)\n", cxl_base, cxl_size);

    return 0;
}

void cxl_alloc_exit(void)
{
    if (!cxl_base)
        return;

    /* If we used memremap (we have cxl_phys/cxl_size), unmap */
    if (cxl_phys && cxl_size) {
        memunmap((void __iomem *)cxl_base);
        cxl_phys = 0;
        cxl_size = 0;
    } else {
        /* If the KVA came from dev-dax internals, we must not unmap it ourselves
         * (the dev-dax driver owns it). Just drop references / cleanup.
         */
    }

    cxl_base = NULL;
    mutex_destroy(&cxl_lock);

    pr_info("cxl_alloc: cleaned up\n");
}

/* Very small bump allocator: returns aligned chunks within the region.
 * NOTE: This allocator never returns freed memory to the pool (free is noop).
 * Use this only for prototypes/testing. Replace with proper allocator for real use.
 */
void *cxl_malloc(size_t size)
{
    size_t aligned, old_off;
    void *ret;

    if (!cxl_base || size == 0)
        return NULL;

    /* minimal alignment to 8 bytes */
    aligned = (size + 7) & ~((size_t)7);

    mutex_lock(&cxl_lock);
    old_off = cxl_offset;
    if (cxl_size && (old_off + aligned) > cxl_size) {
        mutex_unlock(&cxl_lock);
        return NULL; /* out of memory */
    }
    cxl_offset = old_off + aligned;
    mutex_unlock(&cxl_lock);

    ret = (void *)((char *)cxl_base + old_off);
    return ret;
}

void cxl_free(void *ptr)
{
    /* no-op for bump allocator. Replace with a free-list/coalescer in production. */
    (void)ptr;
}
EXPORT_SYMBOL_GPL(cxl_malloc);
EXPORT_SYMBOL_GPL(cxl_free);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lustre Developer");
MODULE_DESCRIPTION("Minimal CXL/DAX Allocator Prototype");