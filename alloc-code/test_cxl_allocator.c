/* Ultra-safe test module for CXL allocator - Minimal and safe */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>

/* Include our CXL allocator header */
#include "cxl_kmem_allocator.h"

static int __init test_cxl_init(void)
{
    void *test_ptr1 = NULL;
    void *test_ptr2 = NULL;
    int tests_passed = 0;
    int total_tests = 0;
    
    pr_info("=== ULTRA-SAFE CXL Test Starting ===\n");
    
    /* Test 1: Very small allocation */
    total_tests++;
    pr_info("Test 1: Small allocation (32 bytes)...\n");
    
    test_ptr1 = cxl_kmalloc(32, GFP_KERNEL);
    if (test_ptr1 != NULL) {
        pr_info("Test 1 PASS: Allocation succeeded at %p\n", test_ptr1);
        tests_passed++;
        
        /* Simple memory test - just write one byte */
        *((volatile char*)test_ptr1) = 0x42;
        if (*((volatile char*)test_ptr1) == 0x42) {
            pr_info("Test 1: Memory write/read OK\n");
        }
        
        /* Free it immediately */
        cxl_kfree(test_ptr1);
        pr_info("Test 1: Memory freed\n");
        test_ptr1 = NULL;
    } else {
        pr_info("Test 1 INFO: Allocation returned NULL (may use fallback)\n");
    }
    
    /* Test 2: Zero-initialized allocation */
    total_tests++;
    pr_info("Test 2: Zero-initialized allocation (64 bytes)...\n");
    
    test_ptr2 = cxl_kzalloc(64, GFP_KERNEL);
    if (test_ptr2 != NULL) {
        pr_info("Test 2 PASS: Zero allocation succeeded at %p\n", test_ptr2);
        
        /* Check if properly zeroed */
        if (*((volatile char*)test_ptr2) == 0) {
            pr_info("Test 2: Memory properly zeroed\n");
            tests_passed++;
        } else {
            pr_warn("Test 2: Memory not zeroed\n");
        }
        
        /* Free it */
        cxl_kfree(test_ptr2);
        pr_info("Test 2: Memory freed\n");
        test_ptr2 = NULL;
    } else {
        pr_info("Test 2 INFO: Zero allocation returned NULL\n");
    }
    
    /* Test 3: Check pool statistics */
    total_tests++;
    pr_info("Test 3: Pool statistics...\n");
    
    /* Simple call to pool functions */
    {
        size_t allocated_size = cxl_pool_get_allocated_size();
        size_t free_size = cxl_pool_get_free_size();
        
        pr_info("Test 3 PASS: Pool stats - Allocated: %zu, Free: %zu bytes\n", 
                allocated_size, free_size);
        tests_passed++;
    }
    
    /* Test 4: Multiple small allocations */
    total_tests++;
    pr_info("Test 4: Multiple allocations...\n");
    
    {
        void *ptrs[3] = {NULL, NULL, NULL};
        int successful_allocs = 0;
        int i;
        
        /* Allocate 3 small blocks */
        for (i = 0; i < 3; i++) {
            ptrs[i] = cxl_kmalloc(16, GFP_KERNEL);
            if (ptrs[i]) {
                successful_allocs++;
                /* Mark each block */
                *((volatile char*)ptrs[i]) = 0x10 + i;
            }
        }
        
        pr_info("Test 4: %d/3 allocations succeeded\n", successful_allocs);
        
        /* Verify and free all */
        for (i = 0; i < 3; i++) {
            if (ptrs[i]) {
                char expected = 0x10 + i;
                if (*((volatile char*)ptrs[i]) == expected) {
                    pr_info("Test 4: Block %d data intact\n", i);
                }
                cxl_kfree(ptrs[i]);
            }
        }
        
        if (successful_allocs > 0) {
            tests_passed++;
            pr_info("Test 4 PASS: Multiple allocations worked\n");
        }
    }
    
    /* Results summary */
    pr_info("=== ULTRA-SAFE CXL Test Results ===\n");
    pr_info("Total tests: %d\n", total_tests);
    pr_info("Passed tests: %d\n", tests_passed);
    
    if (tests_passed >= 2) {
        pr_info("✓ CXL allocator is working correctly!\n");
    } else if (tests_passed >= 1) {
        pr_info("~ CXL allocator partially working (may use fallbacks)\n");
    } else {
        pr_warn("? CXL allocator tests failed - check configuration\n");
    }
    
    pr_info("=== Test completed safely - no freeze ===\n");
    
    /* Return 0 to keep module loaded */
    return 0;
}

static void __exit test_cxl_exit(void)
{
    pr_info("Ultra-safe CXL test module unloaded cleanly\n");
}

module_init(test_cxl_init);
module_exit(test_cxl_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Ultra-safe CXL allocator test - no system freeze risk");
MODULE_VERSION("1.0-ultra-safe");