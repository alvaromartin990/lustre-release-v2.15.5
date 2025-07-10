/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Test module for OBD_ALLOC_PTR_ARRAY_LARGE with osd_idmap_cache
 *
 * This kernel module tests the allocation and deallocation of
 * osd_idmap_cache arrays using OBD_ALLOC_PTR_ARRAY_LARGE macro.
 *
 * Author: Test Script Generator
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <linux/ktime.h>

/* Include necessary Lustre headers */
#include <obd_support.h>
#include <lustre_fid.h>
#include <lustre_net.h>

/* Include OSD-specific headers */
#include "lustre/osd-ldiskfs/osd_oi.h"
#include "lustre/osd-ldiskfs/osd_internal.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Test Script Generator");
MODULE_DESCRIPTION("Test module for OBD_ALLOC_PTR_ARRAY_LARGE with osd_idmap_cache");
MODULE_VERSION("1.0");

/* Test parameters */
#define TEST_MIN_ARRAY_SIZE 1
#define TEST_MAX_ARRAY_SIZE 1000
#define TEST_ITERATIONS 5

/* Global variables for testing */
static struct osd_idmap_cache *test_idc_array = NULL;
static int test_array_size = 0;

/* Function to generate random lu_fid */
static void generate_random_fid(struct lu_fid *fid)
{
    get_random_bytes(&fid->f_seq, sizeof(fid->f_seq));
    get_random_bytes(&fid->f_oid, sizeof(fid->f_oid));
    get_random_bytes(&fid->f_ver, sizeof(fid->f_ver));
    
    /* Ensure valid ranges */
    fid->f_seq = fid->f_seq % 0x1000000000ULL; /* Keep sequence reasonable */
    fid->f_oid = fid->f_oid % 0x100000; /* Keep OID reasonable */
    fid->f_ver = fid->f_ver % 100; /* Keep version small */
}

/* Function to generate random osd_inode_id */
static void generate_random_inode_id(struct osd_inode_id *id)
{
    get_random_bytes(&id->oii_ino, sizeof(id->oii_ino));
    get_random_bytes(&id->oii_gen, sizeof(id->oii_gen));
    
    /* Ensure valid ranges */
    id->oii_ino = id->oii_ino % 0x1000000; /* Keep inode number reasonable */
    id->oii_gen = id->oii_gen % 0x10000; /* Keep generation reasonable */
}

/* Function to initialize a single osd_idmap_cache entry */
static void init_random_idmap_cache_entry(struct osd_idmap_cache *idc, int index)
{
    /* Initialize with random data */
    generate_random_fid(&idc->oic_fid);
    generate_random_inode_id(&idc->oic_lid);
    
    /* Set device pointer to NULL for testing (we don't have a real device) */
    idc->oic_dev = NULL;
    
    /* Set remote flag randomly */
    idc->oic_remote = (get_random_int() % 2) ? 1 : 0;
    
    printk(KERN_INFO "test_obd_alloc: Entry %d initialized - FID: [%llu:%u:%u], "
           "Inode: %u/%u, Remote: %d\n",
           index, idc->oic_fid.f_seq, idc->oic_fid.f_oid, idc->oic_fid.f_ver,
           idc->oic_lid.oii_ino, idc->oic_lid.oii_gen, idc->oic_remote);
}

/* Test function for OBD_ALLOC_PTR_ARRAY_LARGE */
static int test_obd_alloc_idmap_cache(int array_size)
{
    struct osd_idmap_cache *idc = NULL;
    ktime_t start_time, end_time;
    s64 alloc_time_ns, free_time_ns;
    int i;
    
    printk(KERN_INFO "test_obd_alloc: Testing allocation of %d osd_idmap_cache entries\n", 
           array_size);
    
    /* Test allocation with timing */
    start_time = ktime_get();
    OBD_ALLOC_PTR_ARRAY_LARGE(idc, array_size);
    end_time = ktime_get();
    alloc_time_ns = ktime_to_ns(ktime_sub(end_time, start_time));
    
    if (idc == NULL) {
        printk(KERN_ERR "test_obd_alloc: Failed to allocate array of %d entries\n", 
               array_size);
        return -ENOMEM;
    }
    
    printk(KERN_INFO "test_obd_alloc: Successfully allocated %d entries "
           "(size: %lu bytes) in %lld ns\n", 
           array_size, array_size * sizeof(struct osd_idmap_cache), alloc_time_ns);
    
    /* Initialize entries with random data */
    for (i = 0; i < array_size; i++) {
        init_random_idmap_cache_entry(&idc[i], i);
    }
    
    /* Verify some entries */
    printk(KERN_INFO "test_obd_alloc: Sample verification:\n");
    for (i = 0; i < min(3, array_size); i++) {
        printk(KERN_INFO "  Entry %d: FID=[%llu:%u:%u], Inode=%u/%u, Remote=%d\n",
               i, idc[i].oic_fid.f_seq, idc[i].oic_fid.f_oid, idc[i].oic_fid.f_ver,
               idc[i].oic_lid.oii_ino, idc[i].oic_lid.oii_gen, idc[i].oic_remote);
    }
    
    /* Test deallocation with timing */
    start_time = ktime_get();
    OBD_FREE_PTR_ARRAY_LARGE(idc, array_size);
    end_time = ktime_get();
    free_time_ns = ktime_to_ns(ktime_sub(end_time, start_time));
    
    printk(KERN_INFO "test_obd_alloc: Successfully freed %d entries in %lld ns\n", 
           array_size, free_time_ns);
    
    return 0;
}

/* Test function for edge cases */
static int test_edge_cases(void)
{
    struct osd_idmap_cache *idc = NULL;
    int ret = 0;
    
    printk(KERN_INFO "test_obd_alloc: Testing edge cases\n");
    
    /* Test allocation of 0 entries */
    printk(KERN_INFO "test_obd_alloc: Testing allocation of 0 entries\n");
    OBD_ALLOC_PTR_ARRAY_LARGE(idc, 0);
    if (idc != NULL) {
        printk(KERN_WARNING "test_obd_alloc: Allocation of 0 entries returned non-NULL\n");
        OBD_FREE_PTR_ARRAY_LARGE(idc, 0);
    } else {
        printk(KERN_INFO "test_obd_alloc: Allocation of 0 entries correctly returned NULL\n");
    }
    
    /* Test allocation of 1 entry */
    printk(KERN_INFO "test_obd_alloc: Testing allocation of 1 entry\n");
    OBD_ALLOC_PTR_ARRAY_LARGE(idc, 1);
    if (idc == NULL) {
        printk(KERN_ERR "test_obd_alloc: Failed to allocate single entry\n");
        return -ENOMEM;
    }
    
    init_random_idmap_cache_entry(&idc[0], 0);
    printk(KERN_INFO "test_obd_alloc: Single entry test - FID=[%llu:%u:%u]\n",
           idc[0].oic_fid.f_seq, idc[0].oic_fid.f_oid, idc[0].oic_fid.f_ver);
    
    OBD_FREE_PTR_ARRAY_LARGE(idc, 1);
    printk(KERN_INFO "test_obd_alloc: Single entry test completed\n");
    
    return ret;
}

/* Test function for stress testing */
static int test_stress_allocation(void)
{
    int i, ret = 0;
    int random_size;
    
    printk(KERN_INFO "test_obd_alloc: Running stress test with %d iterations\n", 
           TEST_ITERATIONS);
    
    for (i = 0; i < TEST_ITERATIONS; i++) {
        /* Generate random array size */
        random_size = (get_random_int() % (TEST_MAX_ARRAY_SIZE - TEST_MIN_ARRAY_SIZE + 1)) 
                      + TEST_MIN_ARRAY_SIZE;
        
        printk(KERN_INFO "test_obd_alloc: Stress test iteration %d/%d (size: %d)\n",
               i + 1, TEST_ITERATIONS, random_size);
        
        ret = test_obd_alloc_idmap_cache(random_size);
        if (ret < 0) {
            printk(KERN_ERR "test_obd_alloc: Stress test failed at iteration %d\n", i + 1);
            break;
        }
    }
    
    return ret;
}

/* Module initialization function */
static int __init test_obd_alloc_init(void)
{
    int ret = 0;
    
    printk(KERN_INFO "test_obd_alloc: Loading test module for OBD_ALLOC_PTR_ARRAY_LARGE\n");
    printk(KERN_INFO "test_obd_alloc: sizeof(struct osd_idmap_cache) = %lu bytes\n",
           sizeof(struct osd_idmap_cache));
    
    /* Test basic functionality */
    ret = test_obd_alloc_idmap_cache(10);
    if (ret < 0) {
        printk(KERN_ERR "test_obd_alloc: Basic test failed\n");
        return ret;
    }
    
    /* Test edge cases */
    ret = test_edge_cases();
    if (ret < 0) {
        printk(KERN_ERR "test_obd_alloc: Edge case test failed\n");
        return ret;
    }
    
    /* Test stress scenarios */
    ret = test_stress_allocation();
    if (ret < 0) {
        printk(KERN_ERR "test_obd_alloc: Stress test failed\n");
        return ret;
    }
    
    printk(KERN_INFO "test_obd_alloc: All tests completed successfully\n");
    return 0;
}

/* Module cleanup function */
static void __exit test_obd_alloc_exit(void)
{
    /* Clean up any remaining allocations */
    if (test_idc_array != NULL) {
        OBD_FREE_PTR_ARRAY_LARGE(test_idc_array, test_array_size);
        test_idc_array = NULL;
        test_array_size = 0;
    }
    
    printk(KERN_INFO "test_obd_alloc: Test module unloaded\n");
}

module_init(test_obd_alloc_init);
module_exit(test_obd_alloc_exit);
