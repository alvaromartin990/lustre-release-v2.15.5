/*
 * Simple test module for OBD_ALLOC_PTR_ARRAY_LARGE with osd_idmap_cache
 *
 * This is a simplified version that focuses on testing the allocation
 * mechanism without complex Lustre dependencies.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <linux/ktime.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Test Script");
MODULE_DESCRIPTION("Simple test for OBD_ALLOC_PTR_ARRAY_LARGE");
MODULE_VERSION("1.0");

/* Simplified definitions based on Lustre structures */
struct simple_fid {
    __u64 f_seq;
    __u32 f_oid;
    __u32 f_ver;
} __attribute__((packed));

struct simple_inode_id {
    __u32 oii_ino;
    __u32 oii_gen;
};

struct simple_idmap_cache {
    struct simple_fid       oic_fid;
    struct simple_inode_id  oic_lid;
    void                   *oic_dev;    /* Would be struct osd_device * */
    __u16                   oic_remote:1;
};

/* Simplified OBD allocation macros for testing */
#define KMALLOC_MAX_SIZE (1024 * 1024)  /* 1MB threshold */

#define SIMPLE_ALLOC_GFP(ptr, size, gfp_mask)                      \
do {                                                                \
    (ptr) = kmalloc(size, gfp_mask);                               \
    if (ptr)                                                       \
        memset(ptr, 0, size);                                      \
} while (0)

#define SIMPLE_VMALLOC(ptr, size)                                  \
do {                                                               \
    (ptr) = vmalloc(size);                                         \
    if (ptr)                                                       \
        memset(ptr, 0, size);                                      \
} while (0)

#define SIMPLE_ALLOC_LARGE(ptr, size)                              \
do {                                                               \
    if ((size) > KMALLOC_MAX_SIZE)                                 \
        ptr = NULL;                                                \
    else                                                           \
        SIMPLE_ALLOC_GFP(ptr, size, GFP_KERNEL | __GFP_NOWARN);   \
    if (ptr == NULL)                                               \
        SIMPLE_VMALLOC(ptr, size);                                 \
} while (0)

#define SIMPLE_ALLOC_PTR_ARRAY_LARGE(ptr, n)                      \
    SIMPLE_ALLOC_LARGE(ptr, (n) * sizeof(*(ptr)))

#define SIMPLE_FREE_LARGE(ptr, size)                              \
do {                                                               \
    if (ptr) {                                                     \
        if (is_vmalloc_addr(ptr))                                  \
            vfree(ptr);                                            \
        else                                                       \
            kfree(ptr);                                            \
        ptr = NULL;                                                \
    }                                                              \
} while (0)

#define SIMPLE_FREE_PTR_ARRAY_LARGE(ptr, n)                       \
    SIMPLE_FREE_LARGE(ptr, (n) * sizeof(*(ptr)))

/* Test parameters */
#define TEST_ITERATIONS 5
#define TEST_MIN_SIZE 1
#define TEST_MAX_SIZE 1000

/* Generate random data for testing */
static void generate_random_fid(struct simple_fid *fid)
{
    get_random_bytes(fid, sizeof(*fid));
    fid->f_seq = fid->f_seq % 0x1000000000ULL;
    fid->f_oid = fid->f_oid % 0x100000;
    fid->f_ver = fid->f_ver % 100;
}

static void generate_random_inode_id(struct simple_inode_id *id)
{
    get_random_bytes(id, sizeof(*id));
    id->oii_ino = id->oii_ino % 0x1000000;
    id->oii_gen = id->oii_gen % 0x10000;
}

static void init_random_cache_entry(struct simple_idmap_cache *cache, int index)
{
    generate_random_fid(&cache->oic_fid);
    generate_random_inode_id(&cache->oic_lid);
    cache->oic_dev = NULL;
    cache->oic_remote = (get_random_int() % 2) ? 1 : 0;
    
    if (index < 3) {  /* Print first few entries */
        printk(KERN_INFO "simple_test: Entry %d - FID: [%llu:%u:%u], "
               "Inode: %u/%u, Remote: %d\n",
               index, cache->oic_fid.f_seq, cache->oic_fid.f_oid, 
               cache->oic_fid.f_ver, cache->oic_lid.oii_ino, 
               cache->oic_lid.oii_gen, cache->oic_remote);
    }
}

/* Test allocation function */
static int test_allocation(int array_size)
{
    struct simple_idmap_cache *cache_array = NULL;
    ktime_t start_time, end_time;
    s64 alloc_time_ns, free_time_ns;
    size_t total_size;
    int i;
    
    total_size = array_size * sizeof(struct simple_idmap_cache);
    
    printk(KERN_INFO "simple_test: Testing allocation of %d entries (%zu bytes)\n",
           array_size, total_size);
    
    /* Test allocation with timing */
    start_time = ktime_get();
    SIMPLE_ALLOC_PTR_ARRAY_LARGE(cache_array, array_size);
    end_time = ktime_get();
    alloc_time_ns = ktime_to_ns(ktime_sub(end_time, start_time));
    
    if (cache_array == NULL) {
        printk(KERN_ERR "simple_test: Failed to allocate %d entries\n", array_size);
        return -ENOMEM;
    }
    
    printk(KERN_INFO "simple_test: Allocation successful in %lld ns\n", alloc_time_ns);
    printk(KERN_INFO "simple_test: Memory allocated via %s\n", 
           is_vmalloc_addr(cache_array) ? "vmalloc" : "kmalloc");
    
    /* Initialize entries with random data */
    for (i = 0; i < array_size; i++) {
        init_random_cache_entry(&cache_array[i], i);
    }
    
    /* Test deallocation with timing */
    start_time = ktime_get();
    SIMPLE_FREE_PTR_ARRAY_LARGE(cache_array, array_size);
    end_time = ktime_get();
    free_time_ns = ktime_to_ns(ktime_sub(end_time, start_time));
    
    printk(KERN_INFO "simple_test: Deallocation successful in %lld ns\n", free_time_ns);
    
    return 0;
}

/* Test edge cases */
static int test_edge_cases(void)
{
    struct simple_idmap_cache *cache = NULL;
    
    printk(KERN_INFO "simple_test: Testing edge cases\n");
    
    /* Test small allocation */
    SIMPLE_ALLOC_PTR_ARRAY_LARGE(cache, 1);
    if (cache == NULL) {
        printk(KERN_ERR "simple_test: Failed to allocate single entry\n");
        return -ENOMEM;
    }
    
    init_random_cache_entry(&cache[0], 0);
    SIMPLE_FREE_PTR_ARRAY_LARGE(cache, 1);
    
    /* Test large allocation (should use vmalloc) */
    int large_size = KMALLOC_MAX_SIZE / sizeof(struct simple_idmap_cache) + 100;
    SIMPLE_ALLOC_PTR_ARRAY_LARGE(cache, large_size);
    
    if (cache == NULL) {
        printk(KERN_WARNING "simple_test: Large allocation failed - this may be expected\n");
    } else {
        printk(KERN_INFO "simple_test: Large allocation (%d entries) successful via %s\n",
               large_size, is_vmalloc_addr(cache) ? "vmalloc" : "kmalloc");
        SIMPLE_FREE_PTR_ARRAY_LARGE(cache, large_size);
    }
    
    return 0;
}

/* Stress test */
static int stress_test(void)
{
    int i, ret = 0;
    int test_size;
    
    printk(KERN_INFO "simple_test: Running stress test with %d iterations\n", TEST_ITERATIONS);
    
    for (i = 0; i < TEST_ITERATIONS; i++) {
        test_size = (get_random_int() % (TEST_MAX_SIZE - TEST_MIN_SIZE + 1)) + TEST_MIN_SIZE;
        
        printk(KERN_INFO "simple_test: Stress test %d/%d (size: %d)\n", 
               i + 1, TEST_ITERATIONS, test_size);
        
        ret = test_allocation(test_size);
        if (ret < 0) {
            printk(KERN_ERR "simple_test: Stress test failed at iteration %d\n", i + 1);
            break;
        }
    }
    
    return ret;
}

/* Module initialization */
static int __init simple_test_init(void)
{
    int ret = 0;
    
    printk(KERN_INFO "simple_test: Loading simple OBD allocation test module\n");
    printk(KERN_INFO "simple_test: sizeof(struct simple_idmap_cache) = %zu bytes\n",
           sizeof(struct simple_idmap_cache));
    printk(KERN_INFO "simple_test: KMALLOC_MAX_SIZE = %d bytes\n", KMALLOC_MAX_SIZE);
    
    /* Basic functionality test */
    ret = test_allocation(10);
    if (ret < 0) {
        printk(KERN_ERR "simple_test: Basic test failed\n");
        return ret;
    }
    
    /* Edge cases */
    ret = test_edge_cases();
    if (ret < 0) {
        printk(KERN_ERR "simple_test: Edge case test failed\n");
        return ret;
    }
    
    /* Stress test */
    ret = stress_test();
    if (ret < 0) {
        printk(KERN_ERR "simple_test: Stress test failed\n");
        return ret;
    }
    
    printk(KERN_INFO "simple_test: All tests completed successfully!\n");
    return 0;
}

/* Module cleanup */
static void __exit simple_test_exit(void)
{
    printk(KERN_INFO "simple_test: Test module unloaded\n");
}

module_init(simple_test_init);
module_exit(simple_test_exit);
