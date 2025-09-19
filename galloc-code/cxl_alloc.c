#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/dax.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/spinlock.h>
#include <linux/slab.h>    // For ALIGN()
#include <linux/file.h>    // Required for filp_open/close
#include <linux/uaccess.h> // Required for file modes

#include "cxl_alloc.h"

// --- Module Info ---
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lustre Developer");
MODULE_DESCRIPTION("CXL Memory Allocator Test Module");

// --- Module Parameter ---
static char *dax_path = "/dev/dax0.0";
module_param(dax_path, charp, 0644);
MODULE_PARM_DESC(dax_path, "Path to the DAX device (e.g., /dev/dax0.0)");

#define CXL_BLOCK_MAGIC 0xDABBADF00DCAFEFEULL

/* Header for every memory block */
struct cxl_block_header {
	u64 magic;
	size_t size;
};

/* Node for the freelist */
struct cxl_free_block {
	struct list_head link;
};

/* Global state for the allocator */
static struct {
	struct dax_device *dax_dev;
	struct block_device *bdev;
	struct file *file_handle; // To hold the open file
	void *addr;
	size_t size;
	struct list_head freelist;
	spinlock_t lock;
} cxl_pool;

// FIXED: Add forward declaration for cxl_alloc_exit to resolve implicit declaration error.
void cxl_alloc_exit(void);

int cxl_alloc_init(const char *path)
{
	struct cxl_block_header *initial_block;
	struct cxl_free_block *free_node;
	struct inode *inode;
	u64 start_off; // For the extra argument

	pr_info("cxl_alloc: Initializing with device %s\n", path);

	spin_lock_init(&cxl_pool.lock);
	INIT_LIST_HEAD(&cxl_pool.freelist);

	cxl_pool.file_handle = filp_open(path, O_RDWR | O_EXCL, 0);
	if (IS_ERR(cxl_pool.file_handle)) {
		pr_err("cxl_alloc: Failed to open device path %s\n", path);
		return PTR_ERR(cxl_pool.file_handle);
	}

	inode = file_inode(cxl_pool.file_handle);
	if (!S_ISBLK(inode->i_mode)) {
		pr_err("cxl_alloc: Path %s is not a block device\n", path);
		filp_close(cxl_pool.file_handle, NULL);
		return -EINVAL;
	}

	cxl_pool.bdev = I_BDEV(inode);

	// FIXED: Provide the required start_off argument.
	cxl_pool.dax_dev = fs_dax_get_by_bdev(cxl_pool.bdev, &start_off);
	if (!cxl_pool.dax_dev) {
		pr_err("cxl_alloc: Failed to get DAX device; is it configured for dax?\n");
		filp_close(cxl_pool.file_handle, NULL);
		return -ENXIO;
	}

	cxl_pool.size = i_size_read(cxl_pool.bdev->bd_inode);

	if (dax_direct_access(cxl_pool.dax_dev, 0, cxl_pool.size / PAGE_SIZE,
			      DAX_ACCESS, &cxl_pool.addr, NULL) < 0) {
		pr_err("cxl_alloc: Failed to map DAX device\n");
		dax_put(cxl_pool.dax_dev);
		filp_close(cxl_pool.file_handle, NULL);
		return -EFAULT;
	}

	if (cxl_pool.size < sizeof(struct cxl_block_header) + sizeof(struct cxl_free_block)) {
		pr_err("cxl_alloc: CXL pool is too small\n");
		cxl_alloc_exit(); // This call now works due to the forward declaration
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

void cxl_alloc_exit(void)
{
	if (cxl_pool.dax_dev) {
		dax_put(cxl_pool.dax_dev);
		cxl_pool.dax_dev = NULL;
	}
	if (cxl_pool.file_handle && !IS_ERR(cxl_pool.file_handle)) {
		filp_close(cxl_pool.file_handle, NULL);
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
	list_for_each_entry(free_block, &cxl_pool.freelist, link) {
		hdr = (struct cxl_block_header *)free_block - 1;
		if (hdr->magic != CXL_BLOCK_MAGIC) {
			pr_crit_once("cxl_alloc: Corrupted block header!\n");
			continue;
		}
		if (hdr->size >= aligned_size) {
			found_block = free_block;
			break;
		}
	}
	if (found_block) {
		list_del(&found_block->link);
		hdr = (struct cxl_block_header *)found_block - 1;
		ptr = (void *)(hdr + 1);
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

	if (!ptr) return;

	hdr = (struct cxl_block_header *)ptr - 1;
	if (hdr->magic != CXL_BLOCK_MAGIC) {
		pr_err("cxl_alloc: Invalid magic on free: %p\n", ptr);
		return;
	}
	free_node = (struct cxl_free_block *)ptr;
	spin_lock_irqsave(&cxl_pool.lock, flags);
	list_add(&free_node->link, &cxl_pool.freelist);
	spin_unlock_irqrestore(&cxl_pool.lock, flags);
}

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