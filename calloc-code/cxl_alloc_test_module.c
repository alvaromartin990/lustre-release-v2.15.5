#include <linux/module.h>
#include <linux/kernel.h>
#include "cxl_alloc.h"

static char *dax_name = "dax0.0";
module_param(dax_name, charp, 0444);

static int __init test_init(void)
{
    int ret;
    pr_info("cxl_test: init, trying dev %s\n", dax_name);

    /* If dev lookup fails you can pass phys and size module params (see cxl_alloc.c) */
    ret = cxl_alloc_init(dax_name, 0 /* fallback phys */, 0 /* fallback size */);
    if (ret) {
        pr_err("cxl_test: cxl_alloc_init failed: %d\n", ret);
        return ret;
    }

    /* quick sanity allocation */
    void *p = cxl_malloc(4096);
    pr_info("cxl_test: allocated %p\n", p);

    return 0;
}

static void __exit test_exit(void)
{
    cxl_alloc_exit();
    pr_info("cxl_test: exit\n");
}

module_init(test_init);
module_exit(test_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alvarito");
MODULE_DESCRIPTION("Test module for CXL allocator");
