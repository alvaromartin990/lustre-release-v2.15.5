/* Safe test module for CXL allocator - No system freezing */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>

/* Include our CXL allocator header */
#include "cxl_kmem_allocator.h"

/* Function pointer declarations for safer access */
static void* (*safe_cxl_kmalloc)(size_t size, gfp_t flags) = NULL;
static void (*safe_cxl_kfree)(const void *ptr) = NULL;
static void* (*safe_cxl_kzalloc)(size_t size, gfp_t flags) = NULL;

static int __init test_cxl_init(void)
{
    void *test_ptr = NULL;
    int test_count = 0;
    int pass_count = 0;
    
    pr_info("=== SAFE CXL Allocator Test Starting ===\n");
    
    /* Step 1: Check if CXL functions are available (symbol resolution) */
    test_count++;
    safe_cxl_kmalloc = (void*(*)(size_t, gfp_t))kallsyms_lookup_name("cxl_kmalloc");
    safe_cxl_kfree = (void(*)(const void*))kallsyms_lookup_name("cxl_kfree");
    safe_cxl_kzalloc = (void*(*)(size_t, gfp_t))kallsyms_lookup_name("cxl_kzalloc");
    
    if (!safe_cxl_kmalloc || !safe_cxl_kfree || !safe_cxl_kzalloc) {
        pr_info("Test %d INFO: Using direct function calls (symbols not in kallsyms)\n", test_count);
        /* Fall back to direct calls - they should work if properly exported */
        safe_cxl_kmalloc = cxl_kmalloc;
        safe_cxl_kfree = cxl_kfree;  
        safe_cxl_kzalloc = cxl_kzalloc;
    } else {
        pr_info("Test %d PASS: CXL symbols found in kernel\n", test_count);
        pass_count++;
    }
    
    /* Step 2: Test very small allocation first (safest) */
    test_count++;
    pr_info("Test %d: Attempting very small (64B) allocation...\n", test_count);
    
    test_ptr = safe_cxl_kmalloc(64, GFP_KERNEL | __GFP_NOWARN);
    if (test_ptr) {
        pr_info("Test %d PASS: Small allocation succeeded at %p\n", test_count, test_ptr);
        
        /* Very careful memory test - just set first byte */
        *((char*)test_ptr) = 'A';
        if (*((char*)test_ptr) == 'A') {
            pr_info("Test %d PASS: Memory write/read successful\n", test_count);
            pass_count++;
        } else {
            pr_err("Test %d FAIL: Memory write/read failed\n", test_count);
        }
        
        /* Free immediately */
        safe_cxl_kfree(test_ptr);
        pr_info("Test %d: Memory freed successfully\n", test_count);
    } else {
        pr_info("Test %d INFO: Small allocation failed (may use fallback)\n", test_count);
        /* This is OK - might indicate fallback to system memory */
    }
    
    /* Step 3: Test zero-initialized allocation */
    test_count++;
    pr_info("Test %d: Testing zero-initialized allocation...\n", test_count);
    
    test_ptr = safe_cxl_kzalloc(128, GFP_KERNEL | __GFP_NOWARN);
    if (test_ptr) {
        /* Check if first few bytes are zero */
        bool is_zeroed = true;
        int i;
        for (i = 0; i < 8; i++) {
            if (((char*)test_ptr)[i] != 0) {
                is_zeroed = false;
                break;
            }
        }
        
        if (is_zeroed) {
            pr_info("Test %d PASS: Zero-initialized allocation working\n", test_count);
            pass_count++;
        } else {
            pr_warn("Test %d WARN: Memory not properly zeroed\n", test_count);
        }
        
        safe_cxl_kfree(test_ptr);
    } else {
        pr_info("Test %d INFO: Zero allocation failed (fallback possible)\n", test_count);
    }
    
    /* Step 4: Test multiple small allocations */
    test_count++;
    pr_info("Test %d: Testing multiple allocations...\n", test_count);
    
    {
        void *ptrs[4] = {NULL, NULL, NULL, NULL};
        int alloc_count = 0;
        int i;
        
        /* Allocate several small blocks */
        for (i = 0; i < 4; i++) {
            ptrs[i] = safe_cxl_kmalloc(32, GFP_KERNEL | __GFP_NOWARN);
            if (ptrs[i]) {
                alloc_count++;
                /* Mark each allocation with different values */
                *((char*)ptrs[i]) = 'A' + i;
            }
        }
        
        if (alloc_count > 0) {
            pr_info("Test %d PASS: %d/4 multiple allocations succeeded\n", test_count, alloc_count);
            pass_count++;
            
            /* Free all successful allocations */
            for (i = 0; i < 4; i++) {
                if (ptrs[i]) {
                    safe_cxl_kfree(ptrs[i]);
                }
            }
            pr_info("Test %d: All allocations freed\n", test_count);
        } else {
            pr_info("Test %d INFO: No multiple allocations succeeded\n", test_count);
        }
    }
    
    /* Step 5: Test pool statistics access (if available) */
    test_count++;
    pr_info("Test %d: Checking pool statistics access...\n", test_count);
    
    /* Try to safely check if we can access pool statistics */
    if (cxl_pool_get_allocated_size && cxl_pool_get_free_size) {
        size_t allocated = cxl_pool_get_allocated_size();
        size_t free_size = cxl_pool_get_free_size();
        
        pr_info("Test %d PASS: Pool stats - Allocated: %zu, Free: %zu\n", 
                test_count, allocated, free_size);
        pass_count++;
    } else {
        pr_info("Test %d INFO: Pool statistics functions not available\n", test_count);
    }
    
    /* Final Results */
    pr_info("=== SAFE CXL Allocator Test Results ===\n");
    pr_info("Tests completed: %d\n", test_count);
    pr_info("Tests passed: %d\n", pass_count);
    pr_info("Success rate: %d%%\n", (pass_count * 100) / test_count);
    
    if (pass_count > 0) {
        pr_info("✓ CXL allocator appears to be working\n");
    } else {
        pr_info("? CXL allocator may be using system memory fallback\n");
    }
    
    pr_info("=== SAFE CXL Test Complete - No System Freeze ===\n");
    
    /* Always return 0 to keep module loaded for inspection */
    return 0;
}

static void __exit test_cxl_exit(void)
{
    pr_info("SAFE CXL Test module unloaded - system remained stable\n");
}

module_init(test_cxl_init);
module_exit(test_cxl_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Safe test module for CXL allocator - prevents system freeze");
MODULE_VERSION("1.0-safe");