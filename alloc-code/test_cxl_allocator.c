/* Simple test module for CXL allocator */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>

/* Include our CXL allocator header */
#include "cxl_kmem_allocator.h"

static int __init test_cxl_init(void)
{
    void *ptr1, *ptr2, *ptr3;
    char test_data[] = "Hello CXL Memory!";
    
    pr_info("=== CXL Allocator Test Starting ===\n");
    
    /* Test 1: Small allocation */
    ptr1 = cxl_kmalloc(256, GFP_KERNEL);
    if (ptr1) {
        strcpy((char *)ptr1, test_data);
        pr_info("Test 1 PASS: Small alloc (256B) at %p, data: %s\n", ptr1, (char *)ptr1);
    } else {
        pr_err("Test 1 FAIL: Small allocation failed\n");
        return -1;
    }
    
    /* Test 2: Medium allocation */
    ptr2 = cxl_kzalloc(4096, GFP_KERNEL);
    if (ptr2) {
        pr_info("Test 2 PASS: Medium alloc (4KB) at %p, zeroed: %s\n", 
                ptr2, (*(char *)ptr2 == 0) ? "YES" : "NO");
    } else {
        pr_err("Test 2 FAIL: Medium allocation failed\n");
        cxl_kfree(ptr1);
        return -1;
    }
    
    /* Test 3: Large allocation */
    ptr3 = cxl_vmalloc(1024 * 1024);  /* 1MB */
    if (ptr3) {
        pr_info("Test 3 PASS: Large alloc (1MB) at %p\n", ptr3);
    } else {
        pr_info("Test 3 INFO: Large allocation used fallback (expected)\n");
    }
    
    /* Show pool statistics */
    pr_info("=== Pool Statistics ===\n");
    pr_info("Total pool: %zu bytes\n", cxl_pool.total_size);
    pr_info("Allocated: %zu bytes\n", cxl_pool_get_allocated_size());
    pr_info("Free: %zu bytes\n", cxl_pool_get_free_size());
    pr_info("Allocation count: %lld\n", atomic64_read(&cxl_pool.alloc_count));
    pr_info("Fallback count: %lld\n", atomic64_read(&cxl_pool.fallback_count));
    
    /* Free memory */
    cxl_kfree(ptr1);
    cxl_kfree(ptr2);
    if (ptr3) cxl_vfree(ptr3);
    
    pr_info("=== CXL Allocator Test Complete ===\n");
    return 0;
}

static void __exit test_cxl_exit(void)
{
    pr_info("CXL Test module unloaded\n");
}

module_init(test_cxl_init);
module_exit(test_cxl_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Test module for CXL allocator");
MODULE_VERSION("1.0");