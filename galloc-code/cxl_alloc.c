#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/dax.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/spinlock.h>
#include <linux/slab.h> // For ALIGN()

#include "cxl_alloc.h"

// --- Module Info ---
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alvaro");
MODULE_DESCRIPTION("CXL Memory Allocator Test Module");

// --- Module Parameter ---
static char *dax_path = "/dev/dax0.0";
module_param(dax_path, charp, 0644);
MODULE_PARM_DESC(dax_path, "Path to the DAX device (e.g., /dev/dax0.0)");


#define CXL_BLOCK_MAGIC 0xDEADBEEFCXL00BAD

/* Represents the header for every memory block (allocated or free) */
struct cxl_block_header {
	u64 magic;
	size_t size; // Size of the data area, excluding this header
};

/* Placed in a free block's data area to link it into the freelist */
struct cxl_free_block {
	struct list_head link;
};

/* Global state for our allocator */
static struct {
	struct dax_device *dax_dev;
	struct block_device *bdev;
	void *addr;          // Start virtual address of the mapped CXL memory
	size_t size;         // Total size of the mapped region
	struct list_head freelist;
	spinlock_t lock;
} cxl_pool;

int cxl_alloc_init(const char *path)
{
	long mapping_size;
	struct cxl_block_header *initial_block;
	struct cxl_free_block *free_node;

	pr_info("cxl_alloc: Initializing with device %s\n", path);

	spin_lock_init(&cxl_pool.lock);
	INIT_LIST_HEAD(&cxl_pool.freelist);

	// 1. Get the block device from the path (kernel "open")
	cxl_pool.bdev = lookup_bdev(path);
	if (IS_ERR(cxl_pool.bdev)) {
		pr_err("cxl_alloc: Failed to look up bdev for %s\n", path);
		return PTR_ERR(cxl_pool.bdev);
	}
    pr_info("cxl_alloc: Opened block device %s\n", path);

	// 2. Get the DAX device
	cxl_pool.dax_dev = dax_get_by_host(cxl_pool.bdev->bd_disk->disk_name);
	if (!cxl_pool.dax_dev) {
		pr_err("cxl_alloc: Failed to get DAX device\n");
		bdput(cxl_pool.bdev);
		return -ENXIO;
	}
    pr_info("cxl_alloc: Got DAX device for %s\n", path);

	// 3. Map the device memory into kernel virtual address space (kernel "mmap")
	cxl_pool.addr = dax_direct_access(cxl_pool.dax_dev, 0, -1UL, DAX_ACCESS);
	if (IS_ERR(cxl_pool.addr)) {
		pr_err("cxl_alloc: Failed to map DAX device\n");
		dax_put(cxl_pool.dax_dev);
		bdput(cxl_pool.bdev);
		return PTR_ERR(cxl_pool.addr);
	}
    pr_info("cxl_alloc: Mapped DAX device at %p\n", cxl_pool.addr);

	mapping_size = dax_direct_access(cxl_pool.dax_dev, 0, 0, DAX_SIZE);
	if (mapping_size < 0) {
		pr_err("cxl_alloc: Failed to get DAX mapping size\n");
		dax_put(cxl_pool.dax_dev);
		bdput(cxl_pool.bdev);
		return mapping_size;
	}
	cxl_pool.size = mapping_size;
    pr_info("cxl_alloc: DAX device size: %ld MB\n", mapping_size / (1024 * 1024));

	// 4. Initialize the freelist with one giant block
	if (cxl_pool.size < sizeof(struct cxl_block_header) + sizeof(struct cxl_free_block)) {
		pr_err("cxl_alloc: CXL pool is too small for initial block\n");
		cxl_alloc_exit();
		return -EINVAL;
	}

	initial_block = (struct cxl_block_header *)cxl_pool.addr;
	initial_block->magic = CXL_BLOCK_MAGIC;
	initial_block->size = cxl_pool.size - sizeof(struct cxl_block_header);

	free_node = (struct cxl_free_block *)(initial_block + 1);
	list_add(&free_node->link, &cxl_pool.freelist);

	pr_info("cxl_alloc: Initialized successfully. Pool VA: %p, Size: %zu MB\n",
		cxl_pool.addr, cxl_pool.size / (1024 * 1024));

	return 0;
}

void cxl_alloc_exit(void)
{
	if (cxl_pool.dax_dev) {
		dax_put(cxl_pool.dax_dev);
		cxl_pool.dax_dev = NULL;
	}
	if (cxl_pool.bdev) {
		bdput(cxl_pool.bdev);
		cxl_pool.bdev = NULL;
	}
	pr_info("cxl_alloc: Allocator shut down.\n");
}

void *cxl_malloc(size_t size)
{
	struct cxl_block_header *hdr;
	struct cxl_free_block *free_block, *found_block = NULL;
	void *ptr = NULL;
	unsigned long flags;
	size_t aligned_size = ALIGN(size, sizeof(void *));

	spin_lock_irqsave(&cxl_pool.lock, flags);

	// First-fit: Find the first free block that is large enough
	list_for_each_entry(free_block, &cxl_pool.freelist, link) {
		// Get the header, which is located immediately before the free_block struct
		hdr = (struct cxl_block_header *)free_block - 1;

		if (hdr->magic != CXL_BLOCK_MAGIC) {
			pr_crit_once("cxl_alloc: Corrupted block header in freelist!\n");
			continue;
		}

		if (hdr->size >= aligned_size) {
			found_block = free_block;
			break;
		}
	}

	if (found_block) {
		list_del(&found_block->link);
		// Get header again, outside the loop
		hdr = (struct cxl_block_header *)found_block - 1;
		ptr = (void *)(hdr + 1); // User data starts after the header
	}

	spin_unlock_irqrestore(&cxl_pool.lock, flags);

	if (!ptr) {
		pr_warn_ratelimited("cxl_alloc: Failed to allocate %zu bytes\n", size);
		return NULL;
	}

	return ptr;
}

void cxl_free(void *ptr)
{
	struct cxl_block_header *hdr;
	struct cxl_free_block *free_node;
	unsigned long flags;

	if (!ptr)
		return;

	// Get the header from the user-provided pointer
	hdr = (struct cxl_block_header *)ptr - 1;

	if (hdr->magic != CXL_BLOCK_MAGIC) {
		pr_err("cxl_alloc: Invalid magic on free: %p\n", ptr);
		return;
	}

	free_node = (struct cxl_free_block *)ptr;

	spin_lock_irqsave(&cxl_pool.lock, flags);
	list_add(&free_node->link, &cxl_pool.freelist); // Add back to freelist
	spin_unlock_irqrestore(&cxl_pool.lock, flags);
}

// --- Test Function ---
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

	pr_info("cxl_alloc: Freeing p2 (%p)...\n", p2);
	cxl_free(p2);
	pr_info("cxl_alloc: Freeing p1 (%p)...\n", p1);
	cxl_free(p1);
	pr_info("cxl_alloc: Freeing p3 (%p)...\n", p3);
	cxl_free(p3);

	pr_info("cxl_alloc: --- Test Complete ---\n");
}

// --- Module Init and Exit ---
static int __init cxl_module_init(void)
{
	int ret;

	ret = cxl_alloc_init(dax_path);
	if (ret)
		return ret;

	// Run a quick test to verify it works
	cxl_test_allocations();

	return 0;
}

static void __exit cxl_module_exit(void)
{
	cxl_alloc_exit();
}

module_init(cxl_module_init);
module_exit(cxl_module_exit);