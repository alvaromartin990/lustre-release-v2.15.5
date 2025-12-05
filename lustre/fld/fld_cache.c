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
#include "../fid/fid_cxl_alloc.h" /* Include CXL allocator */

/**
 * This function initializes the FLD cache.
 * It first tries to recover an existing cache from CXL.
 * If not found, it allocates a new cache structure using CXL.
 * It then initializes the cache structure and registers it as the root.
 */
struct fld_cache *fld_cache_init(const char *name, int cache_size,
				 int cache_threshold)
{
	struct fld_cache *cache;

	ENTRY;

	LASSERT(name != NULL);
	LASSERT(cache_threshold < cache_size);

	/* 
	 * Try to recover existing cache from CXL 
	 */
	if (fid_cxl_init() == 0) {
		cache = (struct fld_cache *)fid_cxl_get_fld_cache();
		if (cache) {
			printk(KERN_ALERT "[CXL_FLD]: Recovered FLD cache from CXL at %p\n", cache);
			/* Re-init volatile fields if necessary, but careful not to wipe persistent data */
			/* For now, we assume we trust the CXL state */
			RETURN(cache);
		}
	} 

	/* Allocate FLD cache structure using CXL */
	// This is important to do it here, not in the init function
	// because we want to be sure that the CXL is initialized
	cache = (struct fld_cache *)fid_cxl_alloc(sizeof(struct fld_cache));
	if (!cache) {
		printk(KERN_ALERT "[CXL_FLD]: Failed to allocate FLD cache structure in CXL\n");
		RETURN(NULL);
	}
	
	/* Clear memory */
	memset(cache, 0, sizeof(struct fld_cache)); // Clear memory
	flush_region_and_sfence(cache, sizeof(struct fld_cache)); // Flush cache

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
	
	/* Persist the new cache structure */
	flush_region_and_sfence(cache, sizeof(struct fld_cache));
	
	/* Register as root */
	fid_cxl_set_fld_cache(cache);

	// print the cache info
	pr_info("[CXL_FLD]: FLD cache mmap allocated at %p, size %zu bytes\n", cache, sizeof(struct fld_cache));
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

	pr_info("[CXL_FLD]: Destroying FLD cache at %p\n", cache);
	fld_cache_flush(cache);
}

/**
 * This function deletes a given node from the list.
 */
static void fld_cache_entry_delete(struct fld_cache *cache,
				   struct fld_cache_entry *node)
{
	list_del(&node->fce_list);
	list_del(&node->fce_lru);
	cache->fci_cache_count--;

	pr_info("[CXL_FLD]: Deleting FLD cache entry at %p\n", node);
	
	/* Flush list changes */
	flush_region_and_sfence(cache, sizeof(struct fld_cache));

	/* Free the node */
	if (node->fce_data) {
		fid_cxl_free(node->fce_data, sizeof(struct lu_seq_range));
	} 
	fid_cxl_free(node, sizeof(struct fld_cache_entry));
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
	struct list_head *head = &cache->fci_entries_head;

	ENTRY;

restart_fixup:

	list_for_each_entry_safe(f_curr, f_next, head, fce_list) {
		// the goal here is to merge ranges if possible
		// they are the ranges of fids
		// we are checking if the current range overlaps with the next range
		c_range = f_curr->fce_data; // current range
		n_range = f_next->fce_data; // next range
		
		/* Invalidate to ensure fresh data for both ranges */
		invalidate_region(c_range, sizeof(*c_range));
		invalidate_region(n_range, sizeof(*n_range));

		LASSERT(lu_seq_range_is_sane(c_range));
		// if we are at the last range, break
		if (&f_next->fce_list == head)
			break;

		if (c_range->lsr_flags != n_range->lsr_flags)
			continue; // if the flags are different, continue

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
			struct lu_seq_range *new_slot = fid_cxl_alloc(sizeof(struct lu_seq_range));
			if (new_slot) {
				*new_slot = new_val;
				flush_region_and_sfence(new_slot, sizeof(*new_slot));
				
				/* Atomic swap */
				void *old_data = f_next->fce_data;
				fid_cxl_atomic_update((void **)&f_next->fce_data, new_slot);
				
				/* Free old data */
				fid_cxl_free(old_data, sizeof(*old_data));
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
			
			if (updated) { // this updates the next range
				struct lu_seq_range *new_slot = fid_cxl_alloc(sizeof(struct lu_seq_range));
				if (new_slot) {
					*new_slot = new_val;
					flush_region_and_sfence(new_slot, sizeof(*new_slot));
					void *old_data = f_next->fce_data;
					fid_cxl_atomic_update((void **)&f_next->fce_data, new_slot);
					fid_cxl_free(old_data, sizeof(*old_data));
				}
			}

			/* we could have overlap over next
			 * range too. better restart.
			 */
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
	const struct lu_seq_range *range = f_new->fce_data;
	const u64 new_start  = range->lsr_start;
	const u64 new_end  = range->lsr_end;
	struct fld_cache_entry *fldt;
	struct lu_seq_range *fldt_data;

	ENTRY;

	pr_info("[CXL_FLD]: Punching hole in FLD cache at %p\n", cache);
	
	/* Allocate cache entry using CXL */
	fldt = (struct fld_cache_entry *)fid_cxl_alloc(sizeof(struct fld_cache_entry)); // so we must allocate a new cache entry
	if (!fldt) {
		// in case that we failed to allocate a new cache entry, we must free the new entry
		fid_cxl_free(f_new->fce_data, sizeof(struct lu_seq_range));
		fid_cxl_free(f_new, sizeof(struct fld_cache_entry));
		EXIT;	
		return;
	}
	/* Allocate cache entry data using CXL */
	fldt_data = (struct lu_seq_range *)fid_cxl_alloc(sizeof(struct lu_seq_range));
	if (!fldt_data) {
		fid_cxl_free(fldt, sizeof(struct fld_cache_entry));
		fid_cxl_free(f_new->fce_data, sizeof(struct lu_seq_range));
		fid_cxl_free(f_new, sizeof(struct fld_cache_entry));
		EXIT;
		return;
	}
	fldt->fce_data = fldt_data;

	/* fldt update*/
	fldt_data->lsr_start = new_end;
	fldt_data->lsr_end = f_curr->fce_data->lsr_end;
	fldt_data->lsr_index = f_curr->fce_data->lsr_index;
	flush_region_and_sfence(fldt_data, sizeof(*fldt_data)); // make sure the data is written to CXL memory

	/* f_curr update - Atomic */
	struct lu_seq_range *curr_new_data = fid_cxl_alloc(sizeof(struct lu_seq_range));
	if (curr_new_data) {
		*curr_new_data = *f_curr->fce_data;
		curr_new_data->lsr_end = new_start;
		flush_region_and_sfence(curr_new_data, sizeof(*curr_new_data));
		
		void *old_data = f_curr->fce_data;
		fid_cxl_atomic_update((void **)&f_curr->fce_data, curr_new_data);
		fid_cxl_free(old_data, sizeof(*old_data));
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

	// this declares the range of the new entry
	const struct lu_seq_range *range = f_new->fce_data;
	const u64 new_start  = range->lsr_start;
	const u64 new_end  = range->lsr_end;
	const u32 mdt = range->lsr_index;
	struct lu_seq_range *curr_data = f_curr->fce_data;

	pr_info("[CXL_FLD]: Handling overlap in FLD cache at %p\n", cache);

	/* this is overlap case, these case are checking overlapping with
	 * prev range only. fixup will handle overlaping with next range.
	 */

	if (curr_data->lsr_index == mdt) {
		// this declares the new slot for the current entry into CXL memory
		struct lu_seq_range *new_slot = fid_cxl_alloc(sizeof(struct lu_seq_range));

		if (new_slot) {
			*new_slot = *curr_data; // copy the current data into the new slot
			new_slot->lsr_start = min(curr_data->lsr_start, new_start); // update the start of the new slot
			new_slot->lsr_end = max(curr_data->lsr_end, new_end); // update the end of the new slot
			flush_region_and_sfence(new_slot, sizeof(*new_slot)); // flush the new slot to CXL memory
			
			fid_cxl_atomic_update((void **)&f_curr->fce_data, new_slot);
			
			// free the old data
			fid_cxl_free(curr_data, sizeof(*curr_data));
		}

		fid_cxl_free(f_new->fce_data, sizeof(struct lu_seq_range)); // free the new entry data
		fid_cxl_free(f_new, sizeof(struct fld_cache_entry)); // free the new entry
		fld_fix_new_list(cache);
	// so, if the new entry is not in the same MDT as the current entry, we need to add it to the list
	// we need to free the new entry and the current entry
	} else if (new_start <= curr_data->lsr_start &&
			curr_data->lsr_end <= new_end) {
		/* case 1: new range completely overshadowed existing range.
		 *         e.g. whole range migrated. update fld cache entry
		 */

		struct lu_seq_range *new_slot = fid_cxl_alloc(sizeof(struct lu_seq_range));
		if (new_slot) {
			*new_slot = *range;
			flush_region_and_sfence(new_slot, sizeof(*new_slot));
			
			fid_cxl_atomic_update((void **)&f_curr->fce_data, new_slot);
			
			// free the old data
			fid_cxl_free(curr_data, sizeof(*curr_data));
		}
		
		// free the new entry data
		fid_cxl_free(f_new->fce_data, sizeof(struct lu_seq_range));
		// free the new entry
		fid_cxl_free(f_new, sizeof(struct fld_cache_entry));
		fld_fix_new_list(cache);
	// in this other case, we punch a new hole since it fits
	} else if (curr_data->lsr_start < new_start &&
			new_end < curr_data->lsr_end) {
		/* case 2: new range fit within existing range. */

		fld_cache_punch_hole(cache, f_curr, f_new);
		// we need to free the new entry and the current entry
		fid_cxl_free(f_new->fce_data, sizeof(struct lu_seq_range));
		fid_cxl_free(f_new, sizeof(struct fld_cache_entry));
		fld_fix_new_list(cache);
	// in this case, it overlaps with the current entry
	// we need to free the new entry and the current entry
	} else  if (new_end <= curr_data->lsr_end) {
		/* case 3: overlap:
		 *         [new_start [c_start  new_end)  c_end)
		 */

		LASSERT(new_start <= curr_data->lsr_start);
		
		// first, we create a new slot for the current entry
		struct lu_seq_range *new_slot = fid_cxl_alloc(sizeof(struct lu_seq_range));
		if (new_slot) {
			*new_slot = *curr_data; // copy the current data into the new slot
			new_slot->lsr_start = new_end; // update the start of the new slot
			flush_region_and_sfence(new_slot, sizeof(*new_slot)); // flush the new slot to CXL memory
			
			fid_cxl_atomic_update((void **)&f_curr->fce_data, new_slot); // update the current entry with the new slot
			fid_cxl_free(curr_data, sizeof(*curr_data)); // free the old data
		}
		
		fld_cache_entry_add(cache, f_new, f_curr->fce_list.prev); // add the new entry to the list
    // in case 4, it overlaps so we need to create a new slot for the current entry
	} else if (curr_data->lsr_start <= new_start) {
		/* case 4: overlap:
		 *         [c_start [new_start c_end) new_end)
		 */

		LASSERT(curr_data->lsr_end <= new_end);

		// first, we create a new slot for the current entry
		struct lu_seq_range *new_slot = fid_cxl_alloc(sizeof(struct lu_seq_range));
		if (new_slot) {
			*new_slot = *curr_data; // copy the current data into the new slot
			new_slot->lsr_end = new_start; // update the end of the new slot
			flush_region_and_sfence(new_slot, sizeof(*new_slot)); // flush the new slot to CXL memory
			
			fid_cxl_atomic_update((void **)&f_curr->fce_data, new_slot); // update the current entry with the new slot
			fid_cxl_free(curr_data, sizeof(*curr_data)); // free the old data
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

	/* Allocate cache entry using CXL */
	f_new = (struct fld_cache_entry *)fid_cxl_alloc(sizeof(struct fld_cache_entry));
	if (!f_new) {
		RETURN(ERR_PTR(-ENOMEM));
	}
	
	/* Allocate data in CXL */
	data = (struct lu_seq_range *)fid_cxl_alloc(sizeof(struct lu_seq_range));
	if (!data) {
		fid_cxl_free(f_new, sizeof(struct fld_cache_entry));
		RETURN(ERR_PTR(-ENOMEM));
	}
	
	/* Initialize data */
	*data = *range;
	flush_region_and_sfence(data, sizeof(*data));
	
	f_new->fce_data = data; // update the data pointer
	flush_region_and_sfence(f_new, sizeof(*f_new)); // flush the new entry to CXL memory

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
	const u64 new_start  = f_new->fce_data->lsr_start;
	const u64 new_end  = f_new->fce_data->lsr_end;
	__u32 new_flags  = f_new->fce_data->lsr_flags;

	ENTRY;

	pr_info("[CXL_FID]: Inserting new FLD cache entry nolock\n");

	fld_cache_shrink(cache); // Why shrink? 
	// shrink the cache to free up space for the new entry

	head = &cache->fci_entries_head; // get the head of the list

	list_for_each_entry_safe(f_curr, n, head, fce_list) {
		struct lu_seq_range *curr_data = f_curr->fce_data; // get the data from the current entry
		
		invalidate_region(curr_data, sizeof(*curr_data)); // invalidate the cache region
		
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

	CDEBUG(D_INFO, "insert range "DRANGE"\n", PRANGE(f_new->fce_data));
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

	pr_info("[CXL_FID]: Inserting new FLD cache entry\n");

	flde = fld_cache_entry_create(range); // flde is a pointer to the new entry
	if (IS_ERR(flde))
		RETURN(PTR_ERR(flde));

	write_lock(&cache->fci_lock); // takes care of locking
	rc = fld_cache_insert_nolock(cache, flde); // this will flush the new entry to CXL memory
	write_unlock(&cache->fci_lock);
	if (rc) {
		// free the new entry
		fid_cxl_free(flde->fce_data, sizeof(struct lu_seq_range));
		fid_cxl_free(flde, sizeof(struct fld_cache_entry));
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
		struct lu_seq_range *data = flde->fce_data; // get the data from the current entry
		invalidate_region(data, sizeof(*data)); // invalidate the cache region
		
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

	pr_info("[CXL_FID]: Looking up FLD cache entry\n");

	read_lock(&cache->fci_lock);
	head = &cache->fci_entries_head; // get the head of the list

	cache->fci_stat.fst_count++;
	list_for_each_entry(flde, head, fce_list) {
		struct lu_seq_range *data = flde->fce_data; // get the data from the current entry
		invalidate_region(data, sizeof(*data)); // invalidate the cache region because we are reading from it
		
		if (data->lsr_start > seq) {
			if (prev != NULL)
				*range = *prev->fce_data; // if we found a range that is greater than the seq, return the previous range				
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
