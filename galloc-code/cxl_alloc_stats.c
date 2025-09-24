#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/slab.h>    // For ALIGN()
#include <linux/file.h>    // Required for filp_open/close
#include <linux/uaccess.h> // Required for file modes
#include <linux/mm.h>      // For memory mapping
#include <linux/mman.h>    // For mmap constants
#include <linux/string.h>  // For string functions
#include <linux/io.h>      // For memremap()

#include "cxl_alloc.h"

// --- Module Info ---
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lustre Developer");
MODULE_DESCRIPTION("CXL Memory Allocator Test Module - Device DAX");

// --- Module Parameters ---
static char *dax_path = "/dev/dax0.0";
module_param(dax_path, charp, 0644);
MODULE_PARM_DESC(dax_path, "Path to the DAX device (e.g., /dev/dax0.0)");

static unsigned long dax_size_mb = 131072; // default 128 GiB
module_param(dax_size_mb, ulong, 0644);
MODULE_PARM_DESC(dax_size_mb, "Size of DAX mapping in MB");

static unsigned long dax_phys = 0; // physical base address of the DAX region
module_param(dax_phys, ulong, 0644);
MODULE_PARM_DESC(dax_phys, "Physical base address of DAX region");

/* at top of cxl_alloc.c, add: */
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/atomic.h>

static atomic64_t cxl_alloc_count = ATOMIC64_INIT(0);
static atomic64_t cxl_free_count  = ATOMIC64_INIT(0);
static struct proc_dir_entry *cxl_proc_entry = NULL;

/* helper to count freelist entries */
static size_t cxl_count_freelist(void)
{
    struct cxl_free_block *node;
    size_t cnt = 0;
    unsigned long flags;

    spin_lock_irqsave(&cxl_pool.lock, flags);
    list_for_each_entry(node, &cxl_pool.freelist, link) {
        cnt++;
    }
    spin_unlock_irqrestore(&cxl_pool.lock, flags);
    return cnt;
}

/* seq_file show callback */
static int cxl_stats_show(struct seq_file *m, void *v)
{
    seq_printf(m, "cxl_pool.addr = %p\n", cxl_pool.addr);
    seq_printf(m, "cxl_pool.size = %zu (bytes) = %zu MB\n",
               cxl_pool.size, cxl_pool.size / (1024*1024));
    seq_printf(m, "freelist_entries = %zu\n", cxl_count_freelist());
    seq_printf(m, "alloc_count = %lld\n", (long long)atomic64_read(&cxl_alloc_count));
    seq_printf(m, "free_count  = %lld\n", (long long)atomic64_read(&cxl_free_count));
    return 0;
}

/* open callback for seq_file */
static int cxl_stats_open(struct inode *inode, struct file *file)
{
    return single_open(file, cxl_stats_show, NULL);
}

/* proc file ops */
static const struct file_operations cxl_stats_fops = {
    .owner   = THIS_MODULE,
    .open    = cxl_stats_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = single_release,
};

/* insert into module init after cxl_alloc_init success: */
cxl_proc_entry = proc_create("cxl_alloc_stats", 0444, NULL, &cxl_stats_fops);
if (!cxl_proc_entry) {
    pr_warn("cxl_alloc: failed to create /proc/cxl_alloc_stats\n");
}

/* in module exit, remove it: */
if (cxl_proc_entry) {
    proc_remove(cxl_proc_entry);
    cxl_proc_entry = NULL;
}


// --- Internal structures ---
#define CXL_BLOCK_MAGIC 0xDABBADF00DCAFEFEULL

struct cxl_block_header {
    u64 magic;
    size_t size;
};

struct cxl_free_block {
    struct list_head link;
};

static struct {
    void *addr;
    size_t size;
    struct list_head freelist;
    spinlock_t lock;
} cxl_pool;

// --- Allocator init ---
int cxl_alloc_init(const char *path)
{
    struct cxl_block_header *initial_block;
    struct cxl_free_block *free_node;

    pr_info("cxl_alloc: Initializing with device %s\n", path);

    spin_lock_init(&cxl_pool.lock);
    INIT_LIST_HEAD(&cxl_pool.freelist);

    if (dax_size_mb == 0) {
        pr_err("cxl_alloc: dax_size_mb cannot be 0\n");
        return -EINVAL;
    }

    cxl_pool.size = dax_size_mb << 20; // MB → bytes

    if (!dax_phys) {
        pr_err("cxl_alloc: dax_phys must be provided (physical base address of DAX region)\n");
        return -EINVAL;
    }

    cxl_pool.addr = memremap(dax_phys, cxl_pool.size, MEMREMAP_WB);
    if (!cxl_pool.addr) {
        pr_err("cxl_alloc: memremap failed for phys=%lx size=%zu\n",
               dax_phys, cxl_pool.size);
        return -ENOMEM;
    }

    if (cxl_pool.size < sizeof(struct cxl_block_header) + sizeof(struct cxl_free_block)) {
        pr_err("cxl_alloc: CXL pool is too small\n");
        return -EINVAL;
    }

    initial_block = (struct cxl_block_header *)cxl_pool.addr;
    initial_block->magic = CXL_BLOCK_MAGIC;
    initial_block->size = cxl_pool.size - sizeof(struct cxl_block_header);

    free_node = (struct cxl_free_block *)(initial_block + 1);
    list_add(&free_node->link, &cxl_pool.freelist);

    pr_info("cxl_alloc: Initialized. Pool VA: %p, Size: %zu MB\n",
            cxl_pool.addr, cxl_pool.size / (1024 * 1024));

    return 0;
}
EXPORT_SYMBOL(cxl_alloc_init); // Export for use in other modules!

// --- Allocator exit ---
void cxl_alloc_exit(void)
{
    if (cxl_pool.addr) {
        memunmap(cxl_pool.addr);
        cxl_pool.addr = NULL;
    }
    pr_info("cxl_alloc: Allocator shut down.\n");
}
EXPORT_SYMBOL(cxl_alloc_exit); // Export for use in other modules!

// --- Allocation/free ---
void *cxl_malloc(size_t size)
{
    struct cxl_block_header *hdr;
    struct cxl_free_block *free_block, *found_block = NULL;
    void *ptr = NULL;
    unsigned long flags;
    size_t aligned_size = ALIGN(size, sizeof(void *));

    spin_lock_irqsave(&cxl_pool.lock, flags);
    list_for_each_entry(free_block, &cxl_pool.freelist, link) {
        hdr = (struct cxl_block_header *)free_block - 1;
        if (hdr->magic != CXL_BLOCK_MAGIC)
            continue;
        if (hdr->size >= aligned_size) {
            found_block = free_block;
            break;
        }
    }
    if (found_block) {
        list_del(&found_block->link);
        hdr = (struct cxl_block_header *)found_block - 1;
        ptr = (void *)(hdr + 1);
        atomic64_inc(&cxl_alloc_count);
    }
    spin_unlock_irqrestore(&cxl_pool.lock, flags);

    return ptr;
}
EXPORT_SYMBOL(cxl_malloc); // Export for use in other modules!

void cxl_free(void *ptr)
{
    struct cxl_block_header *hdr;
    struct cxl_free_block *free_node;
    unsigned long flags;

    if (!ptr) return;

    hdr = (struct cxl_block_header *)ptr - 1;
    if (hdr->magic != CXL_BLOCK_MAGIC)
        return;

    free_node = (struct cxl_free_block *)ptr;
    spin_lock_irqsave(&cxl_pool.lock, flags);
    list_add(&free_node->link, &cxl_pool.freelist);
    atomic64_inc(&cxl_free_count);
    spin_unlock_irqrestore(&cxl_pool.lock, flags);
}
EXPORT_SYMBOL(cxl_free); // Export for use in other modules!

// --- Test ---
static void cxl_test_allocations(void)
{
    void *p1, *p2, *p3;
    pr_info("cxl_alloc: --- Running Test ---\n");
    p1 = cxl_malloc(100);
    pr_info("cxl_alloc: Allocated 100 bytes at %p\n", p1);
    p2 = cxl_malloc(2048);
    pr_info("cxl_alloc: Allocated 2048 bytes at %p\n", p2);
    p3 = cxl_malloc(1024 * 1024);
    pr_info("cxl_alloc: Allocated 1MB at %p\n", p3);
    cxl_free(p2);
    cxl_free(p1);
    cxl_free(p3);
    pr_info("cxl_alloc: --- Test Complete ---\n");
}

// --- Module hooks ---
static int __init cxl_module_init(void)
{
    int ret = cxl_alloc_init(dax_path);
    if (ret) return ret;
    cxl_test_allocations();
    return 0;
}

static void __exit cxl_module_exit(void)
{
    cxl_alloc_exit();
}

module_init(cxl_module_init);
module_exit(cxl_module_exit);
