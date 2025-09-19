#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include "cxl_alloc.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alvarito");
MODULE_DESCRIPTION("Test module for CXL Allocator");

static int __init cxl_test_init2(void)
{
    void *p1, *p2, *p3;

    pr_info("cxl_alloc_test: Starting allocator test\n");

    p1 = cxl_malloc(256);
    if (p1)
        pr_info("cxl_alloc_test: Allocated 256 bytes at %p\n", p1);
    else
        pr_err("cxl_alloc_test: Failed to allocate 256 bytes\n");

    p2 = cxl_malloc(4096);
    if (p2)
        pr_info("cxl_alloc_test: Allocated 4 KB at %p\n", p2);
    else
        pr_err("cxl_alloc_test: Failed to allocate 4 KB\n");

    p3 = cxl_malloc(1024 * 1024);
    if (p3)
        pr_info("cxl_alloc_test: Allocated 1 MB at %p\n", p3);
    else
        pr_err("cxl_alloc_test: Failed to allocate 1 MB\n");

    /* Free the allocations */
    if (p1) {
        cxl_free(p1);
        pr_info("cxl_alloc_test: Freed 256 bytes\n");
    }
    if (p2) {
        cxl_free(p2);
        pr_info("cxl_alloc_test: Freed 4 KB\n");
    }
    if (p3) {
        cxl_free(p3);
        pr_info("cxl_alloc_test: Freed 1 MB\n");
    }

    pr_info("cxl_alloc_test: Allocator test complete\n");

    return 0;
}

static int __init cxl_test_init(void)
{
    void *p;
    size_t sizes[] = { 256, 512, 1024, 2048, 8192, 65536 }; // 256B, 512B, 1K, 2K, 8K, 64K
    int i;

    pr_info("cxl_alloc_test: Starting allocator test\n");

    for (i = 0; i < ARRAY_SIZE(sizes); i++) {
        p = cxl_malloc(sizes[i]);
        if (p) {
            pr_info("cxl_alloc_test: Allocated %zu bytes at %p\n", sizes[i], p);
            cxl_free(p);
            pr_info("cxl_alloc_test: Freed %zu bytes\n", sizes[i]);
        } else {
            pr_err("cxl_alloc_test: Failed to allocate %zu bytes\n", sizes[i]);
        }
    }

    pr_info("cxl_alloc_test: Allocator test complete\n");

    return 0;
}

static void __exit cxl_test_exit(void)
{
    pr_info("cxl_alloc_test: Exiting test module\n");
}

module_init(cxl_test_init);
module_exit(cxl_test_exit);
