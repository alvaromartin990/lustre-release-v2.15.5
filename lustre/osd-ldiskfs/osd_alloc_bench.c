#include <linux/module.h>
#include <linux/init.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include "obd_support.h"    /* for OBD_ALLOC_PTR / OBD_FREE_PTR */

#define NITER 100

struct foo { int x, y; };

static int __init alloc_bench_init(void)
{
    int i;
    u64 sum_obd = 0, sum_k = 0;
    for (i = 0; i < NITER; i++) {
        ktime_t t0, t1;
        struct foo *o;

        // ————— OBD_ALLOC path —————
        t0 = ktime_get();
        OBD_ALLOC_PTR(o);
        if (!o) { pr_err("OBD_ALLOC failed\n"); break; }
        o->x = i; o->y = i*2;
        OBD_FREE_PTR(o);
        t1 = ktime_get();
        sum_obd += ktime_to_ns(ktime_sub(t1, t0));

        // ———— kmalloc path ——————
        t0 = ktime_get();
        o = kmalloc(sizeof(*o), GFP_KERNEL);
        if (!o) { pr_err("kmalloc failed\n"); break; }
        o->x = i; o->y = i*2;
        kfree(o);
        t1 = ktime_get();
        sum_k += ktime_to_ns(ktime_sub(t1, t0));
    }

    pr_info("alloc_bench: avg_obd = %llu ns, avg_kmalloc = %llu ns\n",
            sum_obd / NITER, sum_k / NITER);
    return -EINVAL;  // fail load so module unloads immediately
}

module_init(alloc_bench_init);
MODULE_LICENSE("GPL");
