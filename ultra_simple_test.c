/*
 * Ultra-simplified OBD allocation test module
 * This version removes ALL potentially problematic functions
 * and uses only the most basic kernel APIs
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Test Script");
MODULE_DESCRIPTION("Ultra-simplified OBD allocation test");
MODULE_VERSION("1.0");

/* Simplified structures */
struct ultra_simple_cache {
    __u64 seq;
    __u32 oid;
    __u32 ino;
    __u32 gen;
    __u16 remote;
};

/* Simplified allocation macros - NO is_vmalloc_addr() */
#define KMALLOC_MAX_SIZE (1024 * 1024)  /* 1MB threshold */

#define ULTRA_ALLOC_LARGE(ptr, size)                           \
do {                                                           \
    if ((size) > KMALLOC_MAX_SIZE) {                           \
        (ptr) = vmalloc(size);                                 \
        if (ptr) {                                             \
            memset(ptr, 0, size);                              \
            printk(KERN_INFO "ultra_test: allocated %zu bytes via vmalloc\n", (size_t)(size)); \
        }                                                      \
    } else {                                                   \
        (ptr) = kmalloc(size, GFP_KERNEL);                     \
        if (ptr) {                                             \
            memset(ptr, 0, size);                              \
            printk(KERN_INFO "ultra_test: allocated %zu bytes via kmalloc\n", (size_t)(size)); \
        }                                                      \
    }                                                          \
} while (0)

#define ULTRA_FREE_LARGE(ptr, size)                           \
do {                                                           \
    if (ptr) {                                                 \
        if ((size) > KMALLOC_MAX_SIZE) {                       \
            vfree(ptr);                                        \
            printk(KERN_INFO "ultra_test: freed via vfree\n"); \
        } else {                                               \
            kfree(ptr);                                        \
            printk(KERN_INFO "ultra_test: freed via kfree\n"); \
        }                                                      \
        ptr = NULL;                                            \
    }                                                          \
} while (0)

#define ULTRA_ALLOC_PTR_ARRAY_LARGE(ptr, n)                   \
    ULTRA_ALLOC_LARGE(ptr, (n) * sizeof(*(ptr)))

#define ULTRA_FREE_PTR_ARRAY_LARGE(ptr, n)                    \
    ULTRA_FREE_LARGE(ptr, (n) * sizeof(*(ptr)))

/* Test parameters */
#define TEST_SIZES_COUNT 4
static int test_sizes[TEST_SIZES_COUNT] = {1, 10, 100, 1000};

/* Simple counter for generating test data */
static unsigned int test_counter = 0;

/* Generate simple test data */
static void init_ultra_cache_entry(struct ultra_simple_cache *cache, int index)
{
    test_counter++;
    cache->seq = test_counter;
    cache->oid = test_counter * 2;
    cache->ino = test_counter * 1000;
    cache->gen = test_counter % 10000;
    cache->remote = (index % 2) ? 1 : 0;
}

/* Test allocation function */
static int test_ultra_allocation(int array_size)
{
    struct ultra_simple_cache *cache_array = NULL;
    size_t total_size;
    int i;
    
    total_size = array_size * sizeof(struct ultra_simple_cache);
    
    printk(KERN_INFO "ultra_test: Testing allocation of %d entries (%zu bytes)\n",
           array_size, total_size);
    
    /* Test allocation */
    ULTRA_ALLOC_PTR_ARRAY_LARGE(cache_array, array_size);
    
    if (cache_array == NULL) {
        printk(KERN_ERR "ultra_test: Failed to allocate %d entries\n", array_size);
        return -ENOMEM;
    }
    
    printk(KERN_INFO "ultra_test: Allocation successful\n");
    
    /* Initialize entries with test data */
    for (i = 0; i < array_size; i++) {
        init_ultra_cache_entry(&cache_array[i], i);
    }
    
    /* Show first few entries */
    for (i = 0; i < min(3, array_size); i++) {
        printk(KERN_INFO "ultra_test: Entry %d - seq:%llu oid:%u ino:%u gen:%u remote:%u\n",
               i, cache_array[i].seq, cache_array[i].oid, cache_array[i].ino, 
               cache_array[i].gen, cache_array[i].remote);
    }
    
    /* Test deallocation */
    ULTRA_FREE_PTR_ARRAY_LARGE(cache_array, array_size);
    
    printk(KERN_INFO "ultra_test: Deallocation successful\n");
    
    return 0;
}

/* Module initialization */
static int __init ultra_test_init(void)
{
    int i, ret = 0;
    
    printk(KERN_INFO "ultra_test: Loading ultra-simplified OBD allocation test module\n");
    printk(KERN_INFO "ultra_test: sizeof(struct ultra_simple_cache) = %zu bytes\n",
           sizeof(struct ultra_simple_cache));
    printk(KERN_INFO "ultra_test: KMALLOC_MAX_SIZE = %d bytes\n", KMALLOC_MAX_SIZE);
    
    /* Test different allocation sizes */
    for (i = 0; i < TEST_SIZES_COUNT; i++) {
        printk(KERN_INFO "ultra_test: Test %d/%d (size: %d)\n", 
               i + 1, TEST_SIZES_COUNT, test_sizes[i]);
        
        ret = test_ultra_allocation(test_sizes[i]);
        if (ret < 0) {
            printk(KERN_ERR "ultra_test: Test failed at size %d\n", test_sizes[i]);
            break;
        }
    }
    
    if (ret == 0) {
        printk(KERN_INFO "ultra_test: All tests completed successfully!\n");
    } else {
        printk(KERN_ERR "ultra_test: Tests failed with error %d\n", ret);
    }
    
    return 0;
}

/* Module cleanup */
static void __exit ultra_test_exit(void)
{
    printk(KERN_INFO "ultra_test: Ultra-simplified test module unloaded\n");
}

module_init(ultra_test_init);
module_exit(ultra_test_exit);
