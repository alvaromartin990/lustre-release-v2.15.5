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

	/* Write directly to CXL memory */
	if (seq->lss_obj) {
		// Why memcpy instead of fid_cxl_alloc? 
		// fid_cxl_alloc is used for allocation, not for writing.
		// https://stackoverflow.com/questions/1536006/what-is-the-difference-between-memset-and-memcpy-in-c
		memcpy((void *)seq->lss_obj, &info->sti_space, sizeof(struct lu_seq_range)); // memcpy is used for writing by copying the data from one location to another
		// copies the first sizeof(struct lu_seq_range) bytes of the memory area src to memory area dest
		
		/* Flush and fence to ensure persistence */
		flush_region_and_sfence((void *)seq->lss_obj, sizeof(struct lu_seq_range));
	} else {
		rc = -EINVAL;
		GOTO(exit, rc);
	}

	// if (out) {
	// 	pr_info("[CXL_FID]: Updating FLD server\n");
	// 	rc = fld_server_create(env, seq->lss_site->ss_server_fld, out, NULL);
	// }

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
		invalidate_region(seq->lss_obj, sizeof(struct lu_seq_range));
		
		memcpy(&info->sti_space, (void *)seq->lss_obj, sizeof(struct lu_seq_range));
		
		range_le_to_cpu(&seq->lss_space, &info->sti_space);
		CDEBUG(D_INFO, "%s: Space - "DRANGE"\n",
		       seq->lss_name, PRANGE(&seq->lss_space));
		rc = 0;
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
	ENTRY;

	/* Initialize CXL if needed */
	rc = fid_cxl_init();
	if (rc)
		RETURN(rc);

	/* Allocate space for the sequence range in CXL */
	seq->lss_obj = (struct dt_object *)fid_cxl_alloc(sizeof(struct lu_seq_range));
	if (!seq->lss_obj) {
		CERROR("%s: Can't allocate CXL memory for sequence range\n",
		       seq->lss_name);
		RETURN(-ENOMEM);
	}

	/* Initialize the memory with 0 if it's new, or trust it's persistent.
	 * For this implementation, assume we might need to read it.
	 * If it's a fresh allocation, it might be garbage or zero.
	 * We'll assume the caller handles initialization via seq_store_update if needed,
	 * or we should zero it here.
	 */
	pr_info("[CXL_FID]: Initializing sequence range in CXL memory\n");

	// https://stackoverflow.com/questions/1536006/what-is-the-difference-between-memset-and-memcpy-in-c
	// memset is used for writing with memset to ensure it's initialized
	memset(seq->lss_obj, 0, sizeof(struct lu_seq_range)); // write with memset to ensure it's initialized - it will zero the memory
	flush_region_and_sfence(seq->lss_obj, sizeof(struct lu_seq_range)); // we need to flush the cache and fence to ensure persistence


	seq->lss_dev = dt;
	rc = 0;

	CDEBUG(D_INFO, "%s: Allocated CXL memory for sequence storage at %p\n",
	       seq->lss_name, seq->lss_obj);

	/* Register this as the Sequence Controller root */
	fid_cxl_set_seq_ctrl(seq->lss_obj);

	RETURN(rc);
}

void seq_store_fini(struct lu_server_seq *seq, const struct lu_env *env)
{
	ENTRY;
	pr_info("[CXL_FID]: Finalizing sequence range in CXL memory\n");

	if (seq->lss_obj) {
		fid_cxl_free(seq->lss_obj, sizeof(struct lu_seq_range));
		seq->lss_obj = NULL;
	}

	EXIT;
}
