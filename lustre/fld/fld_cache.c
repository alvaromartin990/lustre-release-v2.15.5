// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, 2014, Intel Corporation.
 */

/*
 * This file is part of Lustre, http://www.lustre.org/
 *
 * FLD (Fids Location Database)
 *
 * Author: Pravin Shelar <pravin.shelar@sun.com>
 * Author: Yury Umanets <umka@clusterfs.com>
 */

#include <linux/string.h>
#define DEBUG_SUBSYSTEM S_FLD

#include <libcfs/libcfs.h>
#include <linux/module.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <obd_support.h>
#include <lustre_fld.h>
#include "fld_internal.h"
#include <fid_cxl_alloc.h> /* Include CXL allocator */

/*
 * FLD cache entry data access helpers
 *
 * When using CXL, fce_data stores an OFFSET (not a pointer).
 * These functions handle conversion transparently.
 */

/**
 * fce_data_get - Get fce_data as a valid pointer
 * @entry: The FLD cache entry
 *
 * Returns the fce_data field as a valid kernel pointer. If the entry is in
 * CXL memory, fce_data is stored as an offset and will be converted to a
 * pointer. If not in CXL, fce_data is already a pointer.
 */
static inline struct lu_seq_range *fce_data_get(struct fld_cache_entry *entry)
{
    struct lu_seq_range *data;

    if (!entry || !entry->fce_data)
        return NULL;

    /* Check if entry itself is in CXL memory */
    if (fid_cxl_ptr_is_cxl(entry)) {
        /* Entry is in CXL, so fce_data is stored as an offset */
        size_t offset = (size_t)entry->fce_data;
        data = (struct lu_seq_range *)fid_cxl_offset_to_ptr(offset);

        if (data) {
            /* Invalidate to ensure we have the latest data from CXL */
            invalidate_region(data, sizeof(*data));
        }

        return data;
    }

    /* Not in CXL (DRAM mode), fce_data is already a pointer */
    return entry->fce_data;
}

/**
 * fce_data_set - Set fce_data with proper offset/pointer conversion
 * @entry: The FLD cache entry
 * @data: Pointer to the lu_seq_range data
 *
 * Sets the fce_data field. If the entry is in CXL memory, stores the value
 * as an offset and flushes the entire entry structure. Otherwise, stores it
 * as a pointer.
 */
static inline void fce_data_set(struct fld_cache_entry *entry,
                                struct lu_seq_range *data)
{
    if (!entry || !data)
        return;

    /* Check if entry is in CXL memory */
    if (fid_cxl_ptr_is_cxl(entry)) {
        /* Store as offset */
        size_t offset = fid_cxl_ptr_to_offset(data);
        if (offset == 0) {
            pr_err("[CXL_FLD]: Failed to convert data pointer %p to CXL offset\n", data);
            return;
        }
        entry->fce_data = (struct lu_seq_range *)offset;
        /* Flush the entire entry structure to CXL memory */
        flush_region_and_sfence(entry, sizeof(*entry));
    } else {
        /* Store as pointer (DRAM mode) */
        entry->fce_data = data;
    }
}

/**
 * fce_data_free - Free fce_data with proper offset-to-pointer conversion
 * @entry: The FLD cache entry whose data should be freed
 *
 * Frees the lu_seq_range data associated with an FLD cache entry. If the entry
 * is in CXL memory, fce_data contains an offset which must be converted to a
 * pointer before freeing. Uses fid_cxl_free_hybrid which handles both CXL
 * and kernel memory automatically.
 */
static inline void fce_data_free(struct fld_cache_entry *entry)
{
    struct lu_seq_range *data_ptr;

    if (!entry || !entry->fce_data)
        return;

    /* Get the actual pointer (handles offset conversion for CXL) */
    data_ptr = fce_data_get(entry);
    if (data_ptr) {
        /* fid_cxl_free_hybrid handles both CXL and OBD memory */
        fid_cxl_free_hybrid(data_ptr, sizeof(struct lu_seq_range));
    }

    /* Clear the fce_data field */
    entry->fce_data = NULL;
}

/**
 * fce_data_atomic_update - Atomically update fce_data with new CXL data
 * @entry: The FLD cache entry to update
 * @new_data: New lu_seq_range pointer (already allocated in CXL)
 *
 * Atomically updates the fce_data field. For CXL entries, this converts the
 * new pointer to an offset before storing. Returns the old data pointer
 * (already converted from offset if needed) for the caller to free.
 *
 * IMPORTANT: The caller must free the returned old_data pointer using
 * fid_cxl_free_hybrid() if it is non-NULL.
 */
static inline struct lu_seq_range *fce_data_atomic_update(
    struct fld_cache_entry *entry,
    struct lu_seq_range *new_data)
{
    struct lu_seq_range *old_data_ptr = NULL;

    if (!entry || !new_data)
        return NULL;

    /* First, get the old data pointer (handles offset conversion) */
    old_data_ptr = fce_data_get(entry);

    /* Check if entry is in CXL memory */
    if (fid_cxl_ptr_is_cxl(entry)) {
        /* Convert new pointer to offset for storage */
        size_t new_offset = fid_cxl_ptr_to_offset(new_data);
        if (new_offset == 0) {
            pr_err("[CXL_FLD]: Failed to convert new_data %p to CXL offset\n", new_data);
            return NULL;
        }

        /* Atomic update using the offset value */
        fid_cxl_atomic_update((void **)&entry->fce_data, (void *)new_offset);

        /* Flush the entry to ensure offset is persisted */
        flush_region_and_sfence(entry, sizeof(*entry));
    } else {
        /* DRAM mode: just update the pointer directly */
        fid_cxl_atomic_update((void **)&entry->fce_data, new_data);
    }

    return old_data_ptr;
}

/**
 * This function initializes the FLD cache.
 * It first tries to recover an existing cache from CXL.
 * If not found, it allocates a new cache structure using CXL.
 * It then initializes the cache structure and registers it as the root.
 */
struct fld_cache *fld_cache_init(const char *name, int cache_size,
				 int cache_threshold)
{
	struct fld_cache *cache = NULL;
	bool using_cxl = false;

	ENTRY;

	LASSERT(name != NULL);
	LASSERT(cache_threshold < cache_size);

	/*
	 * Try to initialize CXL allocator and recover existing cache.
	 *
	 * fid_cxl_init() returns 0 only if true CXL/DAX hardware is available.
	 * If no CXL hardware, it returns -ENODEV and we fall back to OBD_ALLOC.
	 *
	 * This ensures:
	 * - True CXL: Memory is shared across nodes, global roots work correctly
	 * - No CXL: Each MDT/OST uses independent kernel memory via OBD_ALLOC
	 */
	if (fid_cxl_init() == 0) {
		pr_info("[CXL_FLD]: CXL allocator initialized - attempting recovery from global root...\n");
		cache = (struct fld_cache *)fid_cxl_get_fld_cache();

		if (cache) {
			/* VALIDATE THE RECOVERED CACHE STRUCTURE */

			/* 1. Check cache is in valid CXL range */
			if (!fid_cxl_ptr_is_cxl(cache)) {
				pr_err("[CXL_FLD]: Recovered cache %p not in CXL range\n", cache);
				cache = NULL;
				goto allocate_new;
			}

			/* 2. Invalidate before reading to ensure cache coherence */
			invalidate_region(cache, sizeof(*cache));

			/* 3. Sanity check fields */
			if (cache->fci_cache_size <= 0 || cache->fci_cache_size > 100000) {
				pr_err("[CXL_FLD]: Invalid cache_size %d, expected positive <= 100000\n",
				       cache->fci_cache_size);
				cache = NULL;
				goto allocate_new;
			}

			if (cache->fci_cache_count < 0 ||
			    cache->fci_cache_count > cache->fci_cache_size) {
				pr_err("[CXL_FLD]: Invalid cache_count %d (size=%d)\n",
				       cache->fci_cache_count, cache->fci_cache_size);
				cache = NULL;
				goto allocate_new;
			}

			/* 4. Validate list heads aren't corrupted */
			if (!list_empty(&cache->fci_lru)) {
				struct fld_cache_entry *entry =
					list_first_entry(&cache->fci_lru,
							 struct fld_cache_entry, fce_lru);
				if (!fid_cxl_ptr_is_cxl(entry)) {
					pr_err("[CXL_FLD]: LRU list entry %p not in CXL\n", entry);
					cache = NULL;
					goto allocate_new;
				}
			}

			if (!list_empty(&cache->fci_entries_head)) {
				struct fld_cache_entry *entry =
					list_first_entry(&cache->fci_entries_head,
							 struct fld_cache_entry, fce_list);
				if (!fid_cxl_ptr_is_cxl(entry)) {
					pr_err("[CXL_FLD]: Entries list entry %p not in CXL\n", entry);
					cache = NULL;
					goto allocate_new;
				}
			}

			/* All validation passed */
			printk(KERN_ALERT "[CXL_FLD]: Successfully recovered and validated FLD cache from CXL at %p\n", cache);
			pr_info("[CXL_FLD]: Re-initializing volatile fields for recovered cache\n");
			pr_info("[CXL_FLD]: Recovered cache stats - size: %d, count: %d\n",
				cache->fci_cache_size, cache->fci_cache_count);

			/* Re-init volatile fields that must be reinitialized after recovery */
			rwlock_init(&cache->fci_lock);
			using_cxl = true;
			RETURN(cache);
		}

allocate_new:
		pr_info("[CXL_FLD]: No existing FLD cache found in CXL or validation failed, allocating new...\n");

		/* Allocate FLD cache structure using CXL if recovery failed */
		cache = (struct fld_cache *)fid_cxl_alloc_hybrid(sizeof(struct fld_cache));
		if (cache) {
			using_cxl = true;
			printk(KERN_ALERT "[CXL_FLD]: Allocated new FLD cache in CXL at %p\n", cache);
		}
	}
	
	/* Fallback to OBD_ALLOC if CXL is not available */
	if (!cache) {
		printk(KERN_ALERT "[CXL_FLD]: CXL not available, falling back to OBD_ALLOC for FLD cache\n");
		OBD_ALLOC_PTR(cache);
		if (!cache) {
			printk(KERN_ALERT "[CXL_FLD]: Failed to allocate FLD cache structure\n");
			RETURN(NULL);
		}
		using_cxl = false;
	}
	
	/* Clear memory */
	memset(cache, 0, sizeof(struct fld_cache)); // Clear memory
	if (using_cxl)
		flush_region_and_sfence(cache, sizeof(struct fld_cache)); // Flush cache if using CXL

	INIT_LIST_HEAD(&cache->fci_entries_head); // Initialize list head
	INIT_LIST_HEAD(&cache->fci_lru); // Initialize LRU list head

	cache->fci_cache_count = 0; // Initialize cache count
	rwlock_init(&cache->fci_lock); // Initialize lock

	// copy the name into the cache structure
	strscpy(cache->fci_name, name, sizeof(cache->fci_name));

	// set the cache size and threshold
	cache->fci_cache_size = cache_size;
	cache->fci_threshold = cache_threshold;

	// Initialize FLD cache info
	memset(&cache->fci_stat, 0, sizeof(cache->fci_stat)); // fills a block of memory with a specified byte value. It's used to initialize memory to a known state
	
	if (using_cxl) {
		/* Persist the new cache structure */
		flush_region_and_sfence(cache, sizeof(struct fld_cache));

		/*
		 * Register as global root for CXL shared memory.
		 * Since vzalloc fallback is now removed, using_cxl=true means
		 * we have true CXL hardware and can safely use global roots.
		 */
		pr_info("[CXL_FLD]: Registering cache as global root in CXL\n");
		fid_cxl_set_fld_cache(cache);
	}

	// print the cache info
	pr_info("[CXL_FLD]: FLD cache allocated at %p (%s), size %zu bytes\n", 
	        cache, using_cxl ? "CXL" : "OBD", sizeof(struct fld_cache));
	CDEBUG(D_INFO, "%s: FLD cache - Size: %d, Threshold: %d\n",
	       cache->fci_name, cache_size, cache_threshold);

	RETURN(cache);
}

/**
 * This function destroys the FLD cache.
 * It flushes the cache and releases the CXL memory.
 */
void fld_cache_fini(struct fld_cache *cache)
{
	LASSERT(cache != NULL);

	pr_info("[CXL_FID]: reached %s:%d\n", __func__, __LINE__);
	pr_info("[CXL_FLD]: Destroying FLD cache at %p\n", cache);
	fld_cache_flush(cache);
}

/**
 * This function deletes a given node from the list.
 */
static void fld_cache_entry_delete(struct fld_cache *cache,
				   struct fld_cache_entry *node)
{
	if (!cache || !node) {
		pr_err("[CXL_FLD]: fld_cache_entry_delete called with NULL cache or node\n");
		return;
	}

	list_del(&node->fce_list);
	list_del(&node->fce_lru);
	cache->fci_cache_count--;

	pr_info("[CXL_FLD]: Deleting FLD cache entry at %p\n", node);

	/* Flush list changes if in CXL mode */
	if (fid_cxl_ptr_is_cxl(cache))
		flush_region_and_sfence(cache, sizeof(struct fld_cache));

	/* Free the node's data using proper offset-to-pointer conversion */
	if (node->fce_data) {
		fce_data_free(node);
	}

	/* Free the node itself - fid_cxl_free_hybrid handles both CXL and OBD */
	fid_cxl_free_hybrid(node, sizeof(struct fld_cache_entry));
}

/**
 * This function fixes the list by checking new entry with NEXT entry in order.
 */
static void fld_fix_new_list(struct fld_cache *cache)
{
	struct fld_cache_entry *f_curr;
	struct fld_cache_entry *f_next;
	struct lu_seq_range *c_range;
	struct lu_seq_range *n_range;
	struct list_head *head;

	ENTRY;

	if (!cache) {
		pr_err("[CXL_FLD]: fld_fix_new_list called with NULL cache\n");
		EXIT;
		return;
	}

	head = &cache->fci_entries_head;

restart_fixup:

	list_for_each_entry_safe(f_curr, f_next, head, fce_list) {
		/* Get current range (helper handles invalidation in CXL mode) */
		c_range = fce_data_get(f_curr);
		if (!c_range) {
			pr_err("[CXL_FLD]: Failed to get fce_data from current entry\n");
			continue;
		}

		LASSERT(lu_seq_range_is_sane(c_range));

		/* if we are at the last range, break */
		if (&f_next->fce_list == head)
			break;

		/* Get next range (helper handles invalidation in CXL mode) */
		n_range = fce_data_get(f_next);
		if (!n_range) {
			pr_err("[CXL_FLD]: Failed to get fce_data from next entry\n");
			continue;
		}

		if (c_range->lsr_flags != n_range->lsr_flags)
			continue;

		LASSERTF(c_range->lsr_start <= n_range->lsr_start,
			 "cur lsr_start "DRANGE" next lsr_start "DRANGE"\n",
			 PRANGE(c_range), PRANGE(n_range));

		/* check merge possibility with next range */
		if (c_range->lsr_end == n_range->lsr_start) {
			if (c_range->lsr_index != n_range->lsr_index)
				continue;

			/* Update n_range->lsr_start atomically */
			struct lu_seq_range new_val = *n_range;
			new_val.lsr_start = c_range->lsr_start;

			/* Allocate new slot for update */
			struct lu_seq_range *new_slot = fid_cxl_alloc_hybrid(sizeof(struct lu_seq_range));
			if (new_slot) {
				struct lu_seq_range *old_data;

				*new_slot = new_val;
				flush_region_and_sfence(new_slot, sizeof(*new_slot));

				/* Atomic swap using helper (handles offset conversion) */
				old_data = fce_data_atomic_update(f_next, new_slot);

				/* Free old data (already converted to pointer by helper) */
				if (old_data)
					fid_cxl_free_hybrid(old_data, sizeof(*old_data));
			}

			fld_cache_entry_delete(cache, f_curr);
			continue;
		}

		/* check if current range overlaps with next range. */
		if (n_range->lsr_start < c_range->lsr_end) {
			struct lu_seq_range new_val = *n_range;
			int updated = 0;

			if (c_range->lsr_index == n_range->lsr_index) {
				new_val.lsr_start = c_range->lsr_start;
				new_val.lsr_end = max(c_range->lsr_end,
						       n_range->lsr_end);
				updated = 1;
				fld_cache_entry_delete(cache, f_curr);
			} else {
				if (n_range->lsr_end <= c_range->lsr_end) {
					new_val = *c_range;
					updated = 1;
					fld_cache_entry_delete(cache, f_curr);
				} else {
					new_val.lsr_start = c_range->lsr_end;
					updated = 1;
				}
			}

			if (updated) {
				struct lu_seq_range *new_slot = fid_cxl_alloc_hybrid(sizeof(struct lu_seq_range));
				if (new_slot) {
					struct lu_seq_range *old_data;

					*new_slot = new_val;
					flush_region_and_sfence(new_slot, sizeof(*new_slot));

					/* Atomic swap using helper (handles offset conversion) */
					old_data = fce_data_atomic_update(f_next, new_slot);

					/* Free old data (already converted to pointer by helper) */
					if (old_data)
						fid_cxl_free_hybrid(old_data, sizeof(*old_data));
				}
			}

			/* we could have overlap over next range too. better restart. */
			goto restart_fixup;
		}

		/* kill duplicates */
		if (c_range->lsr_start == n_range->lsr_start &&
		    c_range->lsr_end == n_range->lsr_end)
			fld_cache_entry_delete(cache, f_curr);
	}

	EXIT;
}

/**
 * This function adds a new entry to the FLD cache.
 * It updates the cache and fixes the list.
 * Now the structure being updated is in CXL memory.
 */
static inline void fld_cache_entry_add(struct fld_cache *cache,
				       struct fld_cache_entry *f_new,
				       struct list_head *pos)
{
	list_add(&f_new->fce_list, pos);
	list_add(&f_new->fce_lru, &cache->fci_lru);

	pr_info("[CXL_FID]: reached %s:%d struct fld_cache_entry %zu\n", __func__, __LINE__, sizeof(struct fld_cache_entry));
	pr_info("[CXL_FLD]: Adding FLD cache entry at %p\n", f_new);

	cache->fci_cache_count++;
	
	/* Flush list changes */
	flush_region_and_sfence(cache, sizeof(struct fld_cache));
	
	fld_fix_new_list(cache);
}

/**
 * Check if cache needs to be shrunk. If so - do it.
 * Remove one entry in list and so on until cache is shrunk enough.
 */
static int fld_cache_shrink(struct fld_cache *cache)
{
	int num = 0;

	ENTRY;

	LASSERT(cache != NULL);

	pr_info("[CXL_FLD]: Shrinking FLD cache at %p\n", cache);

	if (cache->fci_cache_count < cache->fci_cache_size)
		RETURN(0);

	while (cache->fci_cache_count + cache->fci_threshold >
	       cache->fci_cache_size &&
	       !list_empty(&cache->fci_lru)) {
		struct fld_cache_entry *flde =
			list_last_entry(&cache->fci_lru, struct fld_cache_entry,
					fce_lru);

		fld_cache_entry_delete(cache, flde);
		num++;
	}

	RETURN(0);
}

/**
 * This function flushes the FLD cache.
 * It sets the cache size to 0 and shrinks the cache.
 * Now the structure being updated is in CXL memory.
 */
void fld_cache_flush(struct fld_cache *cache)
{
	ENTRY;

	pr_info("[CXL_FLD]: Flushing FLD cache at %p\n", cache);

	write_lock(&cache->fci_lock);
	cache->fci_cache_size = 0; // set cache size to 0
	fld_cache_shrink(cache); // shrink the cache
	write_unlock(&cache->fci_lock);

	// it does not return anything cause it just flushed the cache

	EXIT;
}

/**
 * This function punches a hole in the FLD cache.
 * It divides the existing range and adds a new entry accordingly.
 * Now the structure being updated is in CXL memory.
 * In terms of our data structure, it is like adding a new entry to the cache.
 */

static void fld_cache_punch_hole(struct fld_cache *cache,
				 struct fld_cache_entry *f_curr,
				 struct fld_cache_entry *f_new)
{
	const struct lu_seq_range *range = fce_data_get(f_new);
	u64 new_start;
	u64 new_end;
	struct lu_seq_range *f_curr_data;
	struct fld_cache_entry *fldt;
	struct lu_seq_range *fldt_data;
	struct lu_seq_range *f_new_data;

	ENTRY;

	if (!range) {
		pr_err("[CXL_FLD]: Failed to get range data from f_new\n");
		EXIT;
		return;
	}

	new_start = range->lsr_start;
	new_end  = range->lsr_end;

	f_curr_data = fce_data_get(f_curr);
	if (!f_curr_data) {
		pr_err("[CXL_FLD]: Failed to get range data from f_curr\n");
		EXIT;
		return;
	}

	pr_info("[CXL_FLD]: Punching hole in FLD cache at %p\n", cache);

	/* Allocate cache entry using CXL */
	fldt = (struct fld_cache_entry *)fid_cxl_alloc_hybrid(sizeof(struct fld_cache_entry)); // so we must allocate a new cache entry
	if (!fldt) {
		// in case that we failed to allocate a new cache entry, we must free the new entry
		f_new_data = fce_data_get(f_new);
		if (f_new_data)
			fid_cxl_free_hybrid(f_new_data, sizeof(struct lu_seq_range));
		fid_cxl_free_hybrid(f_new, sizeof(struct fld_cache_entry));
		EXIT;
		return;
	}
	/* Allocate cache entry data using CXL */
	fldt_data = (struct lu_seq_range *)fid_cxl_alloc_hybrid(sizeof(struct lu_seq_range));
	if (!fldt_data) {
		fid_cxl_free_hybrid(fldt, sizeof(struct fld_cache_entry));
		f_new_data = fce_data_get(f_new);
		if (f_new_data)
			fid_cxl_free_hybrid(f_new_data, sizeof(struct lu_seq_range));
		fid_cxl_free_hybrid(f_new, sizeof(struct fld_cache_entry));
		EXIT;
		return;
	}
	fce_data_set(fldt, fldt_data);

	/* fldt update*/
	fldt_data->lsr_start = new_end;
	fldt_data->lsr_end = f_curr_data->lsr_end;
	fldt_data->lsr_index = f_curr_data->lsr_index;
	flush_region_and_sfence(fldt_data, sizeof(*fldt_data)); // make sure the data is written to CXL memory

	/* f_curr update - Atomic */
	struct lu_seq_range *curr_new_data = fid_cxl_alloc_hybrid(sizeof(struct lu_seq_range));
	if (curr_new_data) {
		struct lu_seq_range *old_data;

		*curr_new_data = *f_curr_data;
		curr_new_data->lsr_end = new_start;
		flush_region_and_sfence(curr_new_data, sizeof(*curr_new_data));

		/* Atomic swap using helper (handles offset conversion) */
		old_data = fce_data_atomic_update(f_curr, curr_new_data);

		/* Free old data (already converted to pointer by helper) */
		if (old_data)
			fid_cxl_free_hybrid(old_data, sizeof(*old_data));
	}

	/* add these two entries to list */
	fld_cache_entry_add(cache, f_new, &f_curr->fce_list);
	fld_cache_entry_add(cache, fldt, &f_new->fce_list);

	pr_info("[CXL_FLD]: Added new entry to FLD cache at %p\n", f_new);

	EXIT;
}

/**
 * handle range overlap in fld cache.
 */
static void fld_cache_overlap_handle(struct fld_cache *cache,
				struct fld_cache_entry *f_curr,
				struct fld_cache_entry *f_new)
{
	const struct lu_seq_range *range;
	struct lu_seq_range *curr_data;
	u64 new_start;
	u64 new_end;
	u32 mdt;

	// Get the range of the new entry
	range = fce_data_get(f_new);
	if (!range) {
		pr_err("[CXL_FLD]: Failed to get fce_data from f_new\n");
		return;
	}

	/* Ensure cache coherence if in CXL mode */
	if (fid_cxl_ptr_is_cxl(range)) {
		invalidate_region((void *)range, sizeof(*range));
	}

	new_start = range->lsr_start;
	new_end = range->lsr_end;
	mdt = range->lsr_index;

	// Get the range of the current entry
	curr_data = fce_data_get(f_curr);
	if (!curr_data) {
		pr_err("[CXL_FLD]: Failed to get fce_data from f_curr\n");
		return;
	}

	/* Ensure cache coherence if in CXL mode */
	if (fid_cxl_ptr_is_cxl(curr_data)) {
		invalidate_region(curr_data, sizeof(*curr_data));
	}

	pr_info("[CXL_FLD]: Handling overlap in FLD cache at %p\n", cache);

	/* this is overlap case, these case are checking overlapping with
	 * prev range only. fixup will handle overlaping with next range.
	 */

	if (curr_data->lsr_index == mdt) {
		// this declares the new slot for the current entry into CXL memory
		struct lu_seq_range *new_slot = fid_cxl_alloc_hybrid(sizeof(struct lu_seq_range));

		if (new_slot) {
			struct lu_seq_range *old_data;

			*new_slot = *curr_data; // copy the current data into the new slot
			new_slot->lsr_start = min(curr_data->lsr_start, new_start); // update the start of the new slot
			new_slot->lsr_end = max(curr_data->lsr_end, new_end); // update the end of the new slot
			flush_region_and_sfence(new_slot, sizeof(*new_slot)); // flush the new slot to CXL memory

			/* Atomic swap using helper (handles offset conversion) */
			old_data = fce_data_atomic_update(f_curr, new_slot);

			/* Free old data (already converted to pointer by helper) */
			if (old_data)
				fid_cxl_free_hybrid(old_data, sizeof(*old_data));
		}

		/* Free the new entry using proper helper (handles offset-to-pointer conversion) */
		fce_data_free(f_new);
		fid_cxl_free_hybrid(f_new, sizeof(struct fld_cache_entry)); // free the new entry
		fld_fix_new_list(cache);
	// so, if the new entry is not in the same MDT as the current entry, we need to add it to the list
	// we need to free the new entry and the current entry
	} else if (new_start <= curr_data->lsr_start &&
			curr_data->lsr_end <= new_end) {
		/* case 1: new range completely overshadowed existing range.
		 *         e.g. whole range migrated. update fld cache entry
		 */

		struct lu_seq_range *new_slot = fid_cxl_alloc_hybrid(sizeof(struct lu_seq_range));
		if (new_slot) {
			struct lu_seq_range *old_data;

			*new_slot = *range;
			flush_region_and_sfence(new_slot, sizeof(*new_slot));

			/* Atomic swap using helper (handles offset conversion) */
			old_data = fce_data_atomic_update(f_curr, new_slot);

			/* Free old data (already converted to pointer by helper) */
			if (old_data)
				fid_cxl_free_hybrid(old_data, sizeof(*old_data));
		}

		/* Free the new entry using proper helper (handles offset-to-pointer conversion) */
		fce_data_free(f_new);
		fid_cxl_free_hybrid(f_new, sizeof(struct fld_cache_entry));
		fld_fix_new_list(cache);
	// in this other case, we punch a new hole since it fits
	} else if (curr_data->lsr_start < new_start &&
			new_end < curr_data->lsr_end) {
		/* case 2: new range fit within existing range. */

		fld_cache_punch_hole(cache, f_curr, f_new);
		/* Free the new entry using proper helper (handles offset-to-pointer conversion) */
		fce_data_free(f_new);
		fid_cxl_free_hybrid(f_new, sizeof(struct fld_cache_entry));
		fld_fix_new_list(cache);
	// in this case, it overlaps with the current entry
	// we need to free the new entry and the current entry
	} else  if (new_end <= curr_data->lsr_end) {
		/* case 3: overlap:
		 *         [new_start [c_start  new_end)  c_end)
		 */

		LASSERT(new_start <= curr_data->lsr_start);

		// first, we create a new slot for the current entry
		struct lu_seq_range *new_slot = fid_cxl_alloc_hybrid(sizeof(struct lu_seq_range));
		if (new_slot) {
			struct lu_seq_range *old_data;

			*new_slot = *curr_data; // copy the current data into the new slot
			new_slot->lsr_start = new_end; // update the start of the new slot
			flush_region_and_sfence(new_slot, sizeof(*new_slot)); // flush the new slot to CXL memory

			/* Atomic swap using helper (handles offset conversion) */
			old_data = fce_data_atomic_update(f_curr, new_slot);

			/* Free old data (already converted to pointer by helper) */
			if (old_data)
				fid_cxl_free_hybrid(old_data, sizeof(*old_data));
		}

		fld_cache_entry_add(cache, f_new, f_curr->fce_list.prev); // add the new entry to the list
	// in case 4, it overlaps so we need to create a new slot for the current entry
	} else if (curr_data->lsr_start <= new_start) {
		/* case 4: overlap:
		 *         [c_start [new_start c_end) new_end)
		 */

		LASSERT(curr_data->lsr_end <= new_end);

		// first, we create a new slot for the current entry
		struct lu_seq_range *new_slot = fid_cxl_alloc_hybrid(sizeof(struct lu_seq_range));
		if (new_slot) {
			struct lu_seq_range *old_data;

			*new_slot = *curr_data; // copy the current data into the new slot
			new_slot->lsr_end = new_start; // update the end of the new slot
			flush_region_and_sfence(new_slot, sizeof(*new_slot)); // flush the new slot to CXL memory

			/* Atomic swap using helper (handles offset conversion) */
			old_data = fce_data_atomic_update(f_curr, new_slot);

			/* Free old data (already converted to pointer by helper) */
			if (old_data)
				fid_cxl_free_hybrid(old_data, sizeof(*old_data));
		}

		fld_cache_entry_add(cache, f_new, &f_curr->fce_list);
	} else
		CERROR("NEW range ="DRANGE" curr = "DRANGE"\n",
		       PRANGE(range), PRANGE(curr_data));
}

/**
 * Create a new FLD cache entry.
 *
 * This function allocates a new FLD cache entry and its associated data
 * using CXL memory. It initializes the data with the provided range and
 * returns a pointer to the new entry.
 *
 * @param range The range to be stored in the new entry
 *
 * @return A pointer to the new FLD cache entry, or an error pointer if
 *         allocation fails
 */
struct fld_cache_entry
*fld_cache_entry_create(const struct lu_seq_range *range)
{
	struct fld_cache_entry *f_new;
	struct lu_seq_range *data;

	LASSERT(lu_seq_range_is_sane(range));

	pr_info("[CXL_FID]: Creating new FLD cache entry\n");

	/*
	 * Use hybrid allocation - tries CXL first, falls back to OBD_ALLOC
	 */
	f_new = (struct fld_cache_entry *)fid_cxl_alloc_hybrid(sizeof(struct fld_cache_entry));
	if (!f_new) {
		pr_err("[CXL_FLD]: Failed to allocate FLD cache entry\n");
		RETURN(ERR_PTR(-ENOMEM));
	}

	/* Allocate data using hybrid allocation */
	data = (struct lu_seq_range *)fid_cxl_alloc_hybrid(sizeof(struct lu_seq_range));
	if (!data) {
		fid_cxl_free_hybrid(f_new, sizeof(struct fld_cache_entry));
		RETURN(ERR_PTR(-ENOMEM));
	}

	/* Initialize data */
	*data = *range;

	/* Flush if in CXL mode */
	if (fid_cxl_ptr_is_cxl(data))
		flush_region_and_sfence(data, sizeof(*data));

	/* Use helper to set fce_data (handles offset/pointer conversion and flushing) */
	fce_data_set(f_new, data);

	RETURN(f_new);
}

/**
 * Insert FLD entry in FLD cache. Now, this function is called when we need to insert a new entry into the FLD cache.
 * This cache now is in CXL memory.
 * This function handles all cases of merging and breaking up of ranges.
 */
int fld_cache_insert_nolock(struct fld_cache *cache,
			    struct fld_cache_entry *f_new)
{
	struct fld_cache_entry *f_curr;
	struct fld_cache_entry *n;
	struct list_head *head;
	struct list_head *prev = NULL;
	struct lu_seq_range *f_new_data = fce_data_get(f_new);
	u64 new_start;
	u64 new_end;
	__u32 new_flags;

	ENTRY;

	if (!f_new_data) {
		pr_err("[CXL_FLD]: Failed to get fce_data from f_new in insert_nolock\n");
		RETURN(-EINVAL);
	}

	new_start = f_new_data->lsr_start;
	new_end = f_new_data->lsr_end;
	new_flags = f_new_data->lsr_flags;

	pr_info("[CXL_FID]: Inserting new FLD cache entry nolock\n");

	fld_cache_shrink(cache); // Why shrink? 
	// shrink the cache to free up space for the new entry

	head = &cache->fci_entries_head; // get the head of the list

	list_for_each_entry_safe(f_curr, n, head, fce_list) {
		/*
		 * Get data using helper - invalidates only if in CXL mode
		 * (helper checks fid_cxl_ptr_is_cxl before invalidating)
		 */
		struct lu_seq_range *curr_data = fce_data_get(f_curr);

		if (!curr_data) {
			pr_err("[CXL_FLD]: Failed to get fce_data from f_curr\n");
			continue;
		}

		/* add list if next is end of list */
		if (new_end < curr_data->lsr_start ||
		   (new_end == curr_data->lsr_start &&
		    new_flags != curr_data->lsr_flags))
			break;

		prev = &f_curr->fce_list;
		/* check if this range is to left of new range. */
		if (new_start < curr_data->lsr_end &&
		    new_flags == curr_data->lsr_flags) {
			fld_cache_overlap_handle(cache, f_curr, f_new);
			goto out;
		}
	}

	// flush
	flush_region_and_sfence(f_new, sizeof(*f_new)); 

	if (prev == NULL)
		prev = head;

	CDEBUG(D_INFO, "insert range "DRANGE"\n", PRANGE(f_new_data));
	/* Add new entry to cache and lru list. */
	fld_cache_entry_add(cache, f_new, prev); // this will flush the new entry to CXL memory
out:
	RETURN(0);
}

/* 
This function is called when we need to insert a new entry into the FLD cache.
This cache now is in CXL memory.
This function handles all cases of merging and breaking up of ranges.
*/
int fld_cache_insert(struct fld_cache *cache,
		     const struct lu_seq_range *range)
{
	struct fld_cache_entry	*flde;
	int rc;

	pr_info("[CXL_FID]: reached %s:%d struct lu_seq_range %zu\n", __func__, __LINE__, sizeof(struct lu_seq_range));
	pr_info("[CXL_FID]: Inserting new FLD cache entry\n");

	flde = fld_cache_entry_create(range); // flde is a pointer to the new entry
	if (IS_ERR(flde))
		RETURN(PTR_ERR(flde));

	write_lock(&cache->fci_lock); // takes care of locking
	rc = fld_cache_insert_nolock(cache, flde); // this will flush the new entry to CXL memory
	write_unlock(&cache->fci_lock);
	if (rc) {
		/* Free the entry using proper helper (handles offset-to-pointer conversion) */
		if (flde->fce_data)
			fce_data_free(flde);
		fid_cxl_free_hybrid(flde, sizeof(struct fld_cache_entry));
	}

	RETURN(rc);
}

/* 
This function is called when we need to delete an entry from the FLD cache.
This cache now is in CXL memory.
*/
void fld_cache_delete_nolock(struct fld_cache *cache,
		      const struct lu_seq_range *range)
{
	struct fld_cache_entry *flde;
	struct fld_cache_entry *tmp;
	struct list_head *head;

	pr_info("[CXL_FID]: Deleting FLD cache entry\n");

	head = &cache->fci_entries_head; // get the head of the list
	list_for_each_entry_safe(flde, tmp, head, fce_list) {
		struct lu_seq_range *data = fce_data_get(flde); // get the data from the current entry
		if (!data) {
			pr_err("[CXL_FLD]: Failed to get fce_data from flde\n");
			continue;
		}

		/* Ensure cache coherence across nodes if in CXL mode */
		if (fid_cxl_ptr_is_cxl(data)) {
			invalidate_region(data, sizeof(*data));
		}

		/* add list if next is end of list */
		if (range->lsr_start == data->lsr_start ||
		   (range->lsr_end == data->lsr_end &&
		    range->lsr_flags == data->lsr_flags)) {
			fld_cache_entry_delete(cache, flde);
			break;
		}
	} // do we need to flush the cache?
}

/**
 * This function is called when we need to lookup a range in the FLD cache.
 * This cache now is in CXL memory.
 */
int fld_cache_lookup(struct fld_cache *cache,
		     const u64 seq, struct lu_seq_range *range)
{
	struct fld_cache_entry *flde;
	struct fld_cache_entry *prev = NULL;
	struct list_head *head;

	ENTRY;

	pr_info("[CXL_FID]: reached %s:%d\n", __func__, __LINE__);
	pr_info("[CXL_FID]: Looking up FLD cache entry\n");

	read_lock(&cache->fci_lock);
	head = &cache->fci_entries_head; // get the head of the list

	cache->fci_stat.fst_count++;
	list_for_each_entry(flde, head, fce_list) {
		struct lu_seq_range *data = fce_data_get(flde); // get the data from the current entry
		if (!data) {
			pr_err("[CXL_FLD]: Failed to get fce_data from flde\n");
			continue;
		}

		/* Ensure cache coherence across nodes if in CXL mode */
		if (fid_cxl_ptr_is_cxl(data)) {
			invalidate_region(data, sizeof(*data));
		}

		if (data->lsr_start > seq) {
			if (prev != NULL) {
				struct lu_seq_range *prev_data = fce_data_get(prev);
				if (!prev_data) {
					pr_err("[CXL_FLD]: Failed to get fce_data from prev\n");
					break;
				}
				/* Ensure cache coherence for prev data if in CXL mode */
				if (fid_cxl_ptr_is_cxl(prev_data)) {
					invalidate_region(prev_data, sizeof(*prev_data));
				}
				*range = *prev_data; // if we found a range that is greater than the seq, return the previous range
			}
			break;
		}

		prev = flde; // update the previous entry
		if (lu_seq_range_within(data, seq)) {
			*range = *data; // if we found a range that is within the seq, return it

			cache->fci_stat.fst_cache++;
			read_unlock(&cache->fci_lock);
			// flush the range to CXL memory
			flush_region_and_sfence(range, sizeof(*range));
			RETURN(0);
		}
	}
	read_unlock(&cache->fci_lock);
	RETURN(-ENOENT);
}
