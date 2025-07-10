/*
 * Simplified OBD allocation test module
 * This version removes potentially problematic functions
 * and focuses on the core allocation testing
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <linux/ktime.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Test Script");
MODULE_DESCRIPTION("Simplified OBD allocation test");
MODULE_VERSION("1.0");

/* Simplified structures without complex dependencies */
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
    void                   *oic_dev;
    __u16                   oic_remote:1;
};

/* Simplified allocation macros */
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
#define TEST_SIZES_COUNT 5
static int test_sizes[TEST_SIZES_COUNT] = {1, 10, 100, 1000, 10000};

/* Simple counter for generating test data */
static unsigned int test_counter = 0;

/* Generate simple test data without random functions */
static void generate_simple_fid(struct simple_fid *fid)
{
    test_counter++;
    fid->f_seq = test_counter;
    fid->f_oid = test_counter * 2;
    fid->f_ver = test_counter % 100;
}

static void generate_simple_inode_id(struct simple_inode_id *id)
{
    test_counter++;
    id->oii_ino = test_counter * 1000;
    id->oii_gen = test_counter % 10000;
}

static void init_simple_cache_entry(struct simple_idmap_cache *cache, int index)
{
    generate_simple_fid(&cache->oic_fid);
    generate_simple_inode_id(&cache->oic_lid);
    cache->oic_dev = NULL;
    cache->oic_remote = (index % 2) ? 1 : 0;
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
    
    /* Initialize entries with test data */
    for (i = 0; i < array_size; i++) {
        init_simple_cache_entry(&cache_array[i], i);
    }
    
    /* Show first few entries */
    for (i = 0; i < min(3, array_size); i++) {
        printk(KERN_INFO "simple_test: Entry %d - FID: [%llu:%u:%u], "
               "Inode: %u/%u, Remote: %d\n",
               i, cache_array[i].oic_fid.f_seq, cache_array[i].oic_fid.f_oid, 
               cache_array[i].oic_fid.f_ver, cache_array[i].oic_lid.oii_ino, 
               cache_array[i].oic_lid.oii_gen, cache_array[i].oic_remote);
    }
    
    /* Test deallocation with timing */
    start_time = ktime_get();
    SIMPLE_FREE_PTR_ARRAY_LARGE(cache_array, array_size);
    end_time = ktime_get();
    free_time_ns = ktime_to_ns(ktime_sub(end_time, start_time));
    
    printk(KERN_INFO "simple_test: Deallocation successful in %lld ns\n", free_time_ns);
    
    return 0;
}

/* Module initialization */
static int __init simple_test_init(void)
{
    int i, ret = 0;
    
    printk(KERN_INFO "simple_test: Loading simplified OBD allocation test module\n");
    printk(KERN_INFO "simple_test: sizeof(struct simple_idmap_cache) = %zu bytes\n",
           sizeof(struct simple_idmap_cache));
    printk(KERN_INFO "simple_test: KMALLOC_MAX_SIZE = %d bytes\n", KMALLOC_MAX_SIZE);
    
    /* Test different allocation sizes */
    for (i = 0; i < TEST_SIZES_COUNT; i++) {
        printk(KERN_INFO "simple_test: Test %d/%d (size: %d)\n", 
               i + 1, TEST_SIZES_COUNT, test_sizes[i]);
        
        ret = test_allocation(test_sizes[i]);
        if (ret < 0) {
            printk(KERN_ERR "simple_test: Test failed at size %d\n", test_sizes[i]);
            break;
        }
    }
    
    if (ret == 0) {
        printk(KERN_INFO "simple_test: All tests completed successfully!\n");
    } else {
        printk(KERN_ERR "simple_test: Tests failed with error %d\n", ret);
    }
    
    return 0;
}

/* Module cleanup */
static void __exit simple_test_exit(void)
{
    printk(KERN_INFO "simple_test: Simplified test module unloaded\n");
}

module_init(simple_test_init);
module_exit(simple_test_exit);
