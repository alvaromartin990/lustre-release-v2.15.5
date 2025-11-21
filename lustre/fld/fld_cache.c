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

#define DEBUG_SUBSYSTEM S_FLD

#include <libcfs/libcfs.h>
#include <linux/module.h>
#include <linux/math64.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <obd_support.h>
#include <lustre_fld.h>
#include "fld_internal.h"

/**
 * create fld cache.
 */
struct fld_cache *fld_cache_init(const char *name, int cache_size,
				 int cache_threshold)
{
	struct fld_cache *cache;

	ENTRY;

	LASSERT(name != NULL);
	LASSERT(cache_threshold < cache_size);

	printk(KERN_ALERT "FLD_CACHE: Creating FLD cache: %s, size: %d, threshold: %d\n", name, cache_size, cache_threshold);
	printk(KERN_ALERT "FLD_CACHE: FLD cache struct size: %zu bytes\n", sizeof(struct fld_cache));
	printk(KERN_ALERT "FLD_CACHE: Safe Testing Mode Enabled\n");
	
	/* Allocate FLD cache structure using mmap-backed DRAM */
	u64 start_cache = ktime_get_ns();
	printk(KERN_ALERT "DRAM_TIMING_START: fld_cache mmap_alloc fld_cache size=%zu time=%llu\n", 
	       sizeof(struct fld_cache), start_cache);
	// cache = (struct fld_cache *)vm_mmap(NULL, 0, sizeof(struct fld_cache), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0);
	OBD_ALLOC_PTR(cache);
	u64 end_cache = ktime_get_ns();
	printk(KERN_ALERT "DRAM_TIMING_END: fld_cache mmap_alloc fld_cache duration=%llu time=%llu\n",
	       end_cache - start_cache, end_cache);
	if (IS_ERR(cache)) {
		printk(KERN_ALERT "Failed to mmap FLD cache structure\n");
		RETURN(cache);
	}
	printk(KERN_ALERT "FLD cache mmap allocated at %p, size %zu bytes\n", cache, sizeof(struct fld_cache));
	
	/* Clear the mmap'd memory */
	memset(cache, 0, sizeof(struct fld_cache));

	INIT_LIST_HEAD(&cache->fci_entries_head);
	INIT_LIST_HEAD(&cache->fci_lru);

	cache->fci_cache_count = 0;
	rwlock_init(&cache->fci_lock);

	// In case of using a CXL device, we might want to allocate this struct in CXL memory.
	
	printk(KERN_ALERT "Using strscpy, within fld_cache_init, to copy name to cache->fci_name\n");
	strscpy(cache->fci_name, name, sizeof(cache->fci_name));

	cache->fci_cache_size = cache_size;
	cache->fci_threshold = cache_threshold;

	/* Init fld cache info. */
	// In case of using a CXL device, we might want to allocate this struct in CXL memory.
	printk(KERN_ALERT "Using memset, within fld_cache_init, to zero out cache->fci_stat\n");
	memset(&cache->fci_stat, 0, sizeof(cache->fci_stat));

	CDEBUG(D_INFO, "%s: FLD cache - Size: %d, Threshold: %d\n",
	       cache->fci_name, cache_size, cache_threshold);
	printk(KERN_ALERT "FLD_CACHE: FLD cache init complete: %s at %p, max_entries=%d\n", 
	       cache->fci_name, cache, cache_size);

	RETURN(cache);
}

/**
 * destroy fld cache.
 */
void fld_cache_fini(struct fld_cache *cache)
{
	LASSERT(cache != NULL);
	fld_cache_flush(cache);

	CDEBUG(D_INFO, "FLD cache statistics (%s):\n", cache->fci_name);
	CDEBUG(D_INFO, "  Cache reqs: %llu\n", cache->fci_stat.fst_cache);
	CDEBUG(D_INFO, "  Total reqs: %llu\n", cache->fci_stat.fst_count);

	printk(KERN_ALERT "FLD_CACHE: FLD cache cleanup: %s, final_entries=%d\n", 
	       cache->fci_name, cache->fci_cache_count);
	/* Unmap the mmap-backed DRAM instead of OBD_FREE_PTR */
	u64 start_cache_unmap = ktime_get_ns();
	printk(KERN_ALERT "DRAM_TIMING_START: fld_cache munmap fld_cache size=%zu time=%llu\n",
	       sizeof(struct fld_cache), start_cache_unmap);
	// vm_munmap((unsigned long)cache, sizeof(struct fld_cache));
	OBD_FREE_PTR(cache);
	u64 end_cache_unmap = ktime_get_ns();
	printk(KERN_ALERT "DRAM_TIMING_END: fld_cache munmap fld_cache duration=%llu time=%llu\n",
	       end_cache_unmap - start_cache_unmap, end_cache_unmap);
}

/**
 * delete given node from list.
 */
static void fld_cache_entry_delete(struct fld_cache *cache,
				   struct fld_cache_entry *node)
{
	list_del(&node->fce_list);
	list_del(&node->fce_lru);
	cache->fci_cache_count--;
	printk(KERN_ALERT "FLD_CACHE: FLD entry deleted: %p, cache_count now %d\n", node, cache->fci_cache_count);
	/* Unmap the mmap-backed DRAM instead of OBD_FREE_PTR */
	u64 start_entry_unmap = ktime_get_ns();
	printk(KERN_ALERT "DRAM_TIMING_START: fld_cache entry_delete munmap fld_cache_entry size=%zu time=%llu\n",
	       sizeof(struct fld_cache_entry), start_entry_unmap);
	// vm_munmap((unsigned long)node, sizeof(struct fld_cache_entry));
	OBD_FREE_PTR(node);
	u64 end_entry_unmap = ktime_get_ns();
	printk(KERN_ALERT "DRAM_TIMING_END: fld_cache entry_delete munmap fld_cache_entry duration=%llu time=%llu\n",
	       end_entry_unmap - start_entry_unmap, end_entry_unmap);
}

/**
 * fix list by checking new entry with NEXT entry in order.
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
		c_range = &f_curr->fce_range;
		n_range = &f_next->fce_range;

		LASSERT(lu_seq_range_is_sane(c_range));
		if (&f_next->fce_list == head)
			break;

		if (c_range->lsr_flags != n_range->lsr_flags)
			continue;

		LASSERTF(c_range->lsr_start <= n_range->lsr_start,
			 "cur lsr_start "DRANGE" next lsr_start "DRANGE"\n",
			 PRANGE(c_range), PRANGE(n_range));

		/* check merge possibility with next range */
		if (c_range->lsr_end == n_range->lsr_start) {
			if (c_range->lsr_index != n_range->lsr_index)
				continue;
			n_range->lsr_start = c_range->lsr_start;
			fld_cache_entry_delete(cache, f_curr);
			continue;
		}

		/* check if current range overlaps with next range. */
		if (n_range->lsr_start < c_range->lsr_end) {
			if (c_range->lsr_index == n_range->lsr_index) {
				n_range->lsr_start = c_range->lsr_start;
				n_range->lsr_end = max(c_range->lsr_end,
						       n_range->lsr_end);
				fld_cache_entry_delete(cache, f_curr);
			} else {
				if (n_range->lsr_end <= c_range->lsr_end) {
					*n_range = *c_range;
					fld_cache_entry_delete(cache, f_curr);
				} else
					n_range->lsr_start = c_range->lsr_end;
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
 * add node to fld cache
 */
static inline void fld_cache_entry_add(struct fld_cache *cache,
				       struct fld_cache_entry *f_new,
				       struct list_head *pos)
{
	list_add(&f_new->fce_list, pos);
	list_add(&f_new->fce_lru, &cache->fci_lru);

	cache->fci_cache_count++;
	printk(KERN_ALERT "FLD entry_add: %p added, cache_count=%d, est_footprint=%zu bytes\n",
	       f_new, cache->fci_cache_count, 
	       cache->fci_cache_count * sizeof(struct fld_cache_entry) + sizeof(struct fld_cache));
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

	CDEBUG(D_INFO, "%s: FLD cache - Shrunk by %d entries\n",
	       cache->fci_name, num);
	printk(KERN_ALERT "FLD cache_shrink: %s evicted %d entries, remaining=%d, est_footprint=%zu bytes\n",
	       cache->fci_name, num, cache->fci_cache_count,
	       cache->fci_cache_count * sizeof(struct fld_cache_entry) + sizeof(struct fld_cache));

	RETURN(0);
}

/**
 * kill all fld cache entries.
 */
void fld_cache_flush(struct fld_cache *cache)
{
	ENTRY;

	write_lock(&cache->fci_lock);
	cache->fci_cache_size = 0;
	fld_cache_shrink(cache);
	write_unlock(&cache->fci_lock);

	EXIT;
}

/**
 * punch hole in existing range. divide this range and add new
 * entry accordingly.
 */

static void fld_cache_punch_hole(struct fld_cache *cache,
				 struct fld_cache_entry *f_curr,
				 struct fld_cache_entry *f_new)
{
	const struct lu_seq_range *range = &f_new->fce_range;
	const u64 new_start  = range->lsr_start;
	const u64 new_end  = range->lsr_end;
	struct fld_cache_entry *fldt;

	ENTRY;
	/* Allocate cache entry using mmap-backed DRAM */
	u64 start_punch = ktime_get_ns();
	printk(KERN_ALERT "DRAM_TIMING_START: fld_cache punch_hole mmap_alloc fld_cache_entry size=%zu time=%llu\n", 
	       sizeof(struct fld_cache_entry), start_punch);
	// fldt = (struct fld_cache_entry *)vm_mmap(NULL, 0, sizeof(struct fld_cache_entry),
	// 					 PROT_READ | PROT_WRITE,
	// 					 MAP_PRIVATE | MAP_ANONYMOUS, 0);
	OBD_ALLOC_GFP(fldt, sizeof(*fldt), GFP_ATOMIC);
	u64 end_punch = ktime_get_ns();
	printk(KERN_ALERT "DRAM_TIMING_END: fld_cache punch_hole mmap_alloc fld_cache_entry duration=%llu time=%llu\n",
	       end_punch - start_punch, end_punch);
	if (IS_ERR(fldt)) {
		printk(KERN_ALERT "FLD punch_hole mmap failed, freeing f_new %p\n", f_new);
		// vm_munmap((unsigned long)f_new, sizeof(struct fld_cache_entry));
		OBD_FREE_PTR(f_new);
		EXIT;
		/* overlap is not allowed, so dont mess up list. */
		return;
	}
	printk(KERN_ALERT "FLD punch_hole: allocated fldt %p (%zu bytes)\n", fldt, sizeof(struct fld_cache_entry));
	/* Clear the mmap'd memory */
	memset(fldt, 0, sizeof(struct fld_cache_entry));
	/*  break f_curr RANGE into three RANGES:
	 *        f_curr, f_new , fldt
	 */

	/* fldt */
	fldt->fce_range.lsr_start = new_end;
	fldt->fce_range.lsr_end = f_curr->fce_range.lsr_end;
	fldt->fce_range.lsr_index = f_curr->fce_range.lsr_index;

	/* f_curr */
	f_curr->fce_range.lsr_end = new_start;

	/* add these two entries to list */
	fld_cache_entry_add(cache, f_new, &f_curr->fce_list);
	fld_cache_entry_add(cache, fldt, &f_new->fce_list);

	/* no need to fixup */
	EXIT;
}

/**
 * handle range overlap in fld cache.
 */
static void fld_cache_overlap_handle(struct fld_cache *cache,
				struct fld_cache_entry *f_curr,
				struct fld_cache_entry *f_new)
{
	const struct lu_seq_range *range = &f_new->fce_range;
	const u64 new_start  = range->lsr_start;
	const u64 new_end  = range->lsr_end;
	const u32 mdt = range->lsr_index;

	/* this is overlap case, these case are checking overlapping with
	 * prev range only. fixup will handle overlaping with next range.
	 */

	if (f_curr->fce_range.lsr_index == mdt) {
		f_curr->fce_range.lsr_start = min(f_curr->fce_range.lsr_start,
						  new_start);

		f_curr->fce_range.lsr_end = max(f_curr->fce_range.lsr_end,
						new_end);

		/* Unmap the mmap-backed DRAM instead of OBD_FREE_PTR */
		vm_munmap((unsigned long)f_new, sizeof(struct fld_cache_entry));
		// OBD_FREE_PTR(f_new);
		fld_fix_new_list(cache);

	} else if (new_start <= f_curr->fce_range.lsr_start &&
			f_curr->fce_range.lsr_end <= new_end) {
		/* case 1: new range completely overshadowed existing range.
		 *         e.g. whole range migrated. update fld cache entry
		 */

		f_curr->fce_range = *range;
		/* Unmap the mmap-backed DRAM instead of OBD_FREE_PTR */
		vm_munmap((unsigned long)f_new, sizeof(struct fld_cache_entry));
		// OBD_FREE_PTR(f_new);
		fld_fix_new_list(cache);

	} else if (f_curr->fce_range.lsr_start < new_start &&
			new_end < f_curr->fce_range.lsr_end) {
		/* case 2: new range fit within existing range. */

		fld_cache_punch_hole(cache, f_curr, f_new);

	} else  if (new_end <= f_curr->fce_range.lsr_end) {
		/* case 3: overlap:
		 *         [new_start [c_start  new_end)  c_end)
		 */

		LASSERT(new_start <= f_curr->fce_range.lsr_start);

		f_curr->fce_range.lsr_start = new_end;
		fld_cache_entry_add(cache, f_new, f_curr->fce_list.prev);

	} else if (f_curr->fce_range.lsr_start <= new_start) {
		/* case 4: overlap:
		 *         [c_start [new_start c_end) new_end)
		 */

		LASSERT(f_curr->fce_range.lsr_end <= new_end);

		f_curr->fce_range.lsr_end = new_start;
		fld_cache_entry_add(cache, f_new, &f_curr->fce_list);
	} else
		CERROR("NEW range ="DRANGE" curr = "DRANGE"\n",
		       PRANGE(range), PRANGE(&f_curr->fce_range));
}

struct fld_cache_entry
*fld_cache_entry_create(const struct lu_seq_range *range)
{
	struct fld_cache_entry *f_new;

	LASSERT(lu_seq_range_is_sane(range));

	printk(KERN_ALERT "FLD_CACHE: FLD entry_create: entry_size=%zu bytes\n", sizeof(struct fld_cache_entry));
	/* Allocate cache entry using mmap-backed DRAM */
	u64 start_create = ktime_get_ns();
	printk(KERN_ALERT "DRAM_TIMING_START: fld_cache entry_create mmap_alloc fld_cache_entry size=%zu time=%llu\n", 
	       sizeof(struct fld_cache_entry), start_create);
	// f_new = (struct fld_cache_entry *)vm_mmap(NULL, 0, sizeof(struct fld_cache_entry), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0);
	OBD_ALLOC_PTR(f_new);
	u64 end_create = ktime_get_ns();
	printk(KERN_ALERT "DRAM_TIMING_END: fld_cache entry_create mmap_alloc fld_cache_entry duration=%llu time=%llu\n",
	       end_create - start_create, end_create);
	if (IS_ERR(f_new)) {
		printk(KERN_ALERT "Failed to mmap FLD cache entry\n");
		RETURN(f_new);
	}
	printk(KERN_ALERT "FLD_CACHE: FLD entry created: %p (%zu bytes), seq_range [%llu-%llu]\n", 
	       f_new, sizeof(struct fld_cache_entry), range->lsr_start, range->lsr_end);
	
	/* Clear the mmap'd memory and initialize */
	// memset(f_new, 0, sizeof(struct fld_cache_entry));
	f_new->fce_range = *range;
	RETURN(f_new);
}

/**
 * Insert FLD entry in FLD cache.
 *
 * This function handles all cases of merging and breaking up of
 * ranges.
 */
int fld_cache_insert_nolock(struct fld_cache *cache,
			    struct fld_cache_entry *f_new)
{
	struct fld_cache_entry *f_curr;
	struct fld_cache_entry *n;
	struct list_head *head;
	struct list_head *prev = NULL;
	const u64 new_start  = f_new->fce_range.lsr_start;
	const u64 new_end  = f_new->fce_range.lsr_end;
	__u32 new_flags  = f_new->fce_range.lsr_flags;

	ENTRY;

	/*
	 * Duplicate entries are eliminated in insert op.
	 * So we don't need to search new entry before starting
	 * insertion loop.
	 */

	fld_cache_shrink(cache);

	head = &cache->fci_entries_head;

	list_for_each_entry_safe(f_curr, n, head, fce_list) {
		/* add list if next is end of list */
		if (new_end < f_curr->fce_range.lsr_start ||
		   (new_end == f_curr->fce_range.lsr_start &&
		    new_flags != f_curr->fce_range.lsr_flags))
			break;

		prev = &f_curr->fce_list;
		/* check if this range is to left of new range. */
		if (new_start < f_curr->fce_range.lsr_end &&
		    new_flags == f_curr->fce_range.lsr_flags) {
			fld_cache_overlap_handle(cache, f_curr, f_new);
			goto out;
		}
	}

	if (prev == NULL)
		prev = head;

	CDEBUG(D_INFO, "insert range "DRANGE"\n", PRANGE(&f_new->fce_range));
	printk(KERN_ALERT "FLD insert_nolock: adding entry %p, cache_count will be %d\n", 
	       f_new, cache->fci_cache_count + 1);
	/* Add new entry to cache and lru list. */
	fld_cache_entry_add(cache, f_new, prev);
out:
	printk(KERN_ALERT "FLD insert_nolock complete: cache_count=%d\n", cache->fci_cache_count);
	RETURN(0);
}

int fld_cache_insert(struct fld_cache *cache,
		     const struct lu_seq_range *range)
{
	struct fld_cache_entry	*flde;
	int rc;

	flde = fld_cache_entry_create(range);
	if (IS_ERR(flde))
		RETURN(PTR_ERR(flde));

	printk(KERN_ALERT "FLD cache_insert: attempting insert of %p into cache %s\n", 
	       flde, cache->fci_name);
	write_lock(&cache->fci_lock);
	rc = fld_cache_insert_nolock(cache, flde);
	write_unlock(&cache->fci_lock);
	if (rc) {
		printk(KERN_ALERT "FLD cache_insert failed, freeing entry %p\n", flde);
		/* Unmap the mmap-backed DRAM instead of OBD_FREE_PTR */
		// vm_munmap((unsigned long)flde, sizeof(struct fld_cache_entry));
		OBD_FREE_PTR(flde);
	} else {
		printk(KERN_ALERT "FLD cache_insert success: cache %s now has %d entries\n", 
		       cache->fci_name, cache->fci_cache_count);
	}

	RETURN(rc);
}

void fld_cache_delete_nolock(struct fld_cache *cache,
		      const struct lu_seq_range *range)
{
	struct fld_cache_entry *flde;
	struct fld_cache_entry *tmp;
	struct list_head *head;

	head = &cache->fci_entries_head;
	list_for_each_entry_safe(flde, tmp, head, fce_list) {
		/* add list if next is end of list */
		if (range->lsr_start == flde->fce_range.lsr_start ||
		   (range->lsr_end == flde->fce_range.lsr_end &&
		    range->lsr_flags == flde->fce_range.lsr_flags)) {
			fld_cache_entry_delete(cache, flde);
			break;
		}
	}
}

/**
 * lookup \a seq sequence for range in fld cache.
 */
int fld_cache_lookup(struct fld_cache *cache,
		     const u64 seq, struct lu_seq_range *range)
{
	struct fld_cache_entry *flde;
	struct fld_cache_entry *prev = NULL;
	struct list_head *head;

	ENTRY;

	read_lock(&cache->fci_lock);
	head = &cache->fci_entries_head;

	cache->fci_stat.fst_count++;
	if ((cache->fci_stat.fst_count % 100) == 0) {
		printk(KERN_ALERT "FLD_CACHE: FLD lookup stats: %s total_reqs=%llu cache_hits=%llu entries=%d\n",
		       cache->fci_name, cache->fci_stat.fst_count, 
		       cache->fci_stat.fst_cache, cache->fci_cache_count);
	}
	list_for_each_entry(flde, head, fce_list) {
		if (flde->fce_range.lsr_start > seq) {
			if (prev != NULL)
				*range = prev->fce_range;
			break;
		}

		prev = flde;
		if (lu_seq_range_within(&flde->fce_range, seq)) {
			*range = flde->fce_range;

			cache->fci_stat.fst_cache++;
			read_unlock(&cache->fci_lock);
			RETURN(0);
		}
	}
	read_unlock(&cache->fci_lock);
	RETURN(-ENOENT);
}
