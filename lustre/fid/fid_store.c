// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2017, Intel Corporation.
 */

/*
 * This file is part of Lustre, http://www.lustre.org/
 *
 * Lustre Sequence Manager
 *
 * Author: Yury Umanets <umka@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_FID

#include <libcfs/libcfs.h>
#include <dt_object.h>
#include <obd_support.h>
#include <lustre_fid.h>
#include <lustre_fld.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include "fid_internal.h"
#include "fid_cxl_alloc.h"

#if 0
static struct lu_buf *seq_store_buf(struct seq_thread_info *info)
{
	struct lu_buf *buf;

	buf = &info->sti_buf;
	buf->lb_buf = &info->sti_space;
	buf->lb_len = sizeof(info->sti_space);
	return buf;
}

struct seq_update_callback {
	struct dt_txn_commit_cb suc_cb;
	struct lu_server_seq   *suc_seq;
};

static void seq_update_cb(struct lu_env *env, struct thandle *th,
			  struct dt_txn_commit_cb *cb, int err)
{
	struct seq_update_callback *ccb;

	ccb = container_of(cb, struct seq_update_callback, suc_cb);

	LASSERT(ccb->suc_seq != NULL);

	ccb->suc_seq->lss_need_sync = 0;
	OBD_FREE_PTR(ccb);
}

static int seq_update_cb_add(struct thandle *th, struct lu_server_seq *seq)
{
	struct seq_update_callback *ccb;
	struct dt_txn_commit_cb *dcb;
	int rc;

	OBD_ALLOC_PTR(ccb);
	if (!ccb)
		return -ENOMEM;

	ccb->suc_seq = seq;
	seq->lss_need_sync = 1;

	dcb = &ccb->suc_cb;
	dcb->dcb_func  = seq_update_cb;
	INIT_LIST_HEAD(&dcb->dcb_linkage);
	strscpy(dcb->dcb_name, "seq_update_cb", sizeof(dcb->dcb_name));

	rc = dt_trans_cb_add(th, dcb);
	if (rc)
		OBD_FREE_PTR(ccb);
	return rc;
}
#endif

/* This function implies that caller takes care about locking. */
int seq_store_update(const struct lu_env *env, struct lu_server_seq *seq,
		     struct lu_seq_range *out, int sync)
{
	struct seq_thread_info *info;
	int rc = 0;

	info = lu_context_key_get(&env->le_ctx, &seq_thread_key);
	LASSERT(info != NULL);

	/* Store ranges in le format. */
	range_cpu_to_le(&info->sti_space, &seq->lss_space);

	/* Check if we're using CXL (lss_obj is a CXL pointer, not a real dt_object)
	 * We determine this by checking if lss_dev is NULL - if using CXL, we don't 
	 * use the dt_device */
	if (!seq->lss_dev && seq->lss_obj) {
		/* CXL path: Write directly to CXL memory */
		pr_info("[CXL_FID]: reached %s:%d struct lu_seq_range %zu\n", __func__, __LINE__, sizeof(struct lu_seq_range));
		/* lss_obj is actually a CXL memory pointer, not a dt_object */
		memcpy((void *)seq->lss_obj, &info->sti_space, sizeof(struct lu_seq_range));
		
		/* Flush and fence to ensure persistence */
		flush_region_and_sfence((void *)seq->lss_obj, sizeof(struct lu_seq_range));
	} else if (seq->lss_dev && seq->lss_obj) {
		/* Traditional DT path: lss_obj is a real dt_object */
		/* This would use dt_transaction mechanism - not implemented in original code */
		CERROR("%s: DT storage not implemented, CXL storage required\n", seq->lss_name);
		rc = -EOPNOTSUPP;
		GOTO(exit, rc);
	} else {
		rc = -EINVAL;
		GOTO(exit, rc);
	}

exit:
	return rc;
}

/*
 * This function implies that caller takes care about locking or locking is not
 * needed (init time).
 */
int seq_store_read(struct lu_server_seq *seq,
		   const struct lu_env *env)
{
	struct seq_thread_info *info;
	int rc;
	ENTRY;

	info = lu_context_key_get(&env->le_ctx, &seq_thread_key);
	LASSERT(info != NULL);

	/* Read directly from CXL memory */
	if (seq->lss_obj) {
		/* Invalidate cache region to ensure we read latest data from CXL */
		pr_info("[CXL_FID]: reached %s:%d struct lu_seq_range %zu\n", __func__, __LINE__, sizeof(struct lu_seq_range));
		invalidate_region(seq->lss_obj, sizeof(struct lu_seq_range));
		
		memcpy(&info->sti_space, (void *)seq->lss_obj, sizeof(struct lu_seq_range));
		
		range_le_to_cpu(&seq->lss_space, &info->sti_space);

		if (lu_seq_range_is_zero(&seq->lss_space)) {
			CDEBUG(D_INFO, "%s: Zero sequence found in CXL, requesting init\n",
			       seq->lss_name);
			rc = -ENODATA;
		} else {
			CDEBUG(D_INFO, "%s: Space - "DRANGE"\n",
			       seq->lss_name, PRANGE(&seq->lss_space));
			rc = 0;
		}
	} else {
		rc = -EINVAL;
	}

	RETURN(rc);
}

int seq_store_init(struct lu_server_seq *seq,
		   const struct lu_env *env,
		   struct dt_device *dt)
{
	int rc;
	struct lu_seq_range existing_range;
	bool using_cxl = false;
	ENTRY;

	pr_info("[CXL_FID]: seq_store_init starting for %s (seq=%p)\n", seq->lss_name, seq);

	/*
	 * Allocate memory for sequence range - tries CXL first, falls back to OBD_ALLOC.
	 */
	seq->lss_obj = (struct dt_object *)fid_cxl_alloc_hybrid(sizeof(struct lu_seq_range));
	if (!seq->lss_obj) {
		CERROR("%s: Can't allocate memory for sequence range\n",
		       seq->lss_name);
		RETURN(-ENOMEM);
	}

	/* Check if we got CXL memory */
	using_cxl = fid_cxl_ptr_is_cxl(seq->lss_obj);
	pr_info("[CXL_FID]: Allocated %s memory at %p for %s\n",
		using_cxl ? "CXL" : "OBD", seq->lss_obj, seq->lss_name);

	if (using_cxl) {
		/*
		 * CXL path: Check if memory contains valid persistent data.
		 * CXL memory may persist across reboots, so we should NOT blindly zero it.
		 */
		pr_info("[CXL_FID]: Checking for existing valid data in CXL memory\n");

		/* Invalidate CPU cache to ensure we read the actual CXL memory contents */
		invalidate_region(seq->lss_obj, sizeof(struct lu_seq_range));

		/* Read existing data to check validity */
		memcpy(&existing_range, seq->lss_obj, sizeof(struct lu_seq_range));

		/* Convert from little-endian storage format to CPU format for validation */
		range_le_to_cpu(&existing_range, &existing_range);

		/*
		 * Check if the existing data looks valid:
		 * - If all zeros, it's uninitialized (fresh allocation)
		 * - If not sane (invalid range), it's garbage/corrupted
		 * In either case, we zero it. Otherwise, preserve the existing data.
		 */
		if (lu_seq_range_is_zero(&existing_range)) {
			pr_info("[CXL_FID]: CXL memory contains zeros (fresh allocation)\n");
			flush_region_and_sfence(seq->lss_obj, sizeof(struct lu_seq_range));
		} else if (!lu_seq_range_is_sane(&existing_range)) {
			pr_info("[CXL_FID]: CXL memory contains invalid data, zeroing\n");
			memset(seq->lss_obj, 0, sizeof(struct lu_seq_range));
			flush_region_and_sfence(seq->lss_obj, sizeof(struct lu_seq_range));
		} else {
			pr_info("[CXL_FID]: Found existing valid sequence range in CXL: "
				"start=0x%llx end=0x%llx index=%u flags=%u\n",
				existing_range.lsr_start, existing_range.lsr_end,
				existing_range.lsr_index, existing_range.lsr_flags);
		}

		/* Register as global Sequence Controller root for CXL */
		pr_info("[CXL_FID]: Registering seq_ctrl root for %s\n", seq->lss_name);
		fid_cxl_set_seq_ctrl(seq->lss_obj);
	} else {
		/*
		 * OBD_ALLOC path: Initialize to zero (non-persistent memory)
		 */
		pr_info("[CXL_FID]: Initializing OBD memory to zero for %s\n", seq->lss_name);
		memset(seq->lss_obj, 0, sizeof(struct lu_seq_range));
	}

	/* lss_dev = NULL indicates we're using memory-based storage, not DT */
	seq->lss_dev = NULL;
	rc = 0;

	pr_info("[CXL_FID]: seq_store_init completed for %s (using %s)\n",
		seq->lss_name, using_cxl ? "CXL" : "OBD_ALLOC");

	RETURN(rc);
}

void seq_store_fini(struct lu_server_seq *seq, const struct lu_env *env)
{
	ENTRY;
	pr_info("[CXL_FID]: seq_store_fini called (seq=%p)\n", seq);

	if (!seq) {
		pr_warn("[CXL_FID]: seq_store_fini called with NULL seq!\n");
		return;
	}

	pr_info("[CXL_FID]: Finalizing sequence storage for %s\n", seq->lss_name);

	if (seq->lss_obj) {
		pr_info("[CXL_FID]: Freeing memory at %p for %s\n",
			seq->lss_obj, seq->lss_name);
		/* Hybrid free auto-detects CXL vs OBD */
		fid_cxl_free_hybrid(seq->lss_obj, sizeof(struct lu_seq_range));
		seq->lss_obj = NULL;
	}

	pr_info("[CXL_FID]: seq_store_fini completed for %s\n", seq->lss_name);
	EXIT;
}
