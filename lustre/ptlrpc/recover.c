// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2017, Intel Corporation.
 */

/*
 * This file is part of Lustre, http://www.lustre.org/
 *
 * Author: Mike Shaver <shaver@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_RPC
#include <linux/list.h>
#include <libcfs/libcfs.h>
#include <obd_support.h>
#include <lustre_ha.h>
#include <lustre_net.h>
#include <lustre_import.h>
#include <lustre_export.h>
#include <obd.h>
#include <obd_class.h>

#include "ptlrpc_internal.h"

/*
 * Identify what request from replay list needs to be replayed next
 * (based on what we have already replayed) and send it to server.
 */
int ptlrpc_replay_next(struct obd_import *imp, int *inflight)
{
	int rc = 0;
	struct ptlrpc_request *req = NULL;
	__u64 last_transno;

	ENTRY;
	*inflight = 0;

	/* It might have committed some after we last spoke, so make sure we
	 * get rid of them now.
	 */
	spin_lock(&imp->imp_lock);
	imp->imp_last_transno_checked = 0;
	ptlrpc_free_committed(imp);
	last_transno = imp->imp_last_replay_transno;

	CDEBUG(D_HA, "import %p from %s committed %llu last %llu\n",
	       imp, obd2cli_tgt(imp->imp_obd),
	       imp->imp_peer_committed_transno, last_transno);

	/* Replay all the committed open requests on committed_list first */
	if (!list_empty(&imp->imp_committed_list)) {
		req = list_last_entry(&imp->imp_committed_list,
				      struct ptlrpc_request, rq_replay_list);

		/* The last request on committed_list hasn't been replayed */
		if (req->rq_transno > last_transno) {
			if (!imp->imp_resend_replay ||
			    imp->imp_replay_cursor == &imp->imp_committed_list)
				imp->imp_replay_cursor =
					imp->imp_replay_cursor->next;

			while (imp->imp_replay_cursor !=
			       &imp->imp_committed_list) {
				req = list_entry(imp->imp_replay_cursor,
						 struct ptlrpc_request,
						 rq_replay_list);
				if (req->rq_transno > last_transno)
					break;

				req = NULL;
				LASSERT(!list_empty(imp->imp_replay_cursor));
				imp->imp_replay_cursor =
					imp->imp_replay_cursor->next;
			}
		} else {
			/* All requests on committed_list have been replayed */
			imp->imp_replay_cursor = &imp->imp_committed_list;
			req = NULL;
		}
	}

	/* All the requests in committed list have been replayed, let's replay
	 * the imp_replay_list
	 */
	if (req == NULL) {
		struct ptlrpc_request *tmp;

		list_for_each_entry(tmp, &imp->imp_replay_list,
				    rq_replay_list) {
			if (tmp->rq_transno > last_transno) {
				req = tmp;
				break;
			}
		}
	}

	/* If need to resend the last sent transno (because a reconnect
	 * has occurred), then stop on the matching req and send it again.
	 * If, however, the last sent transno has been committed then we
	 * continue replay from the next request.
	 */
	if (req != NULL && imp->imp_resend_replay)
		lustre_msg_add_flags(req->rq_reqmsg, MSG_RESENT);

	/* ptlrpc_prepare_replay() may fail to add the reqeust into unreplied
	 * list if the request hasn't been added to replay list then. Another
	 * exception is that resend replay could have been removed from the
	 * unreplied list.
	 */
	if (req != NULL && list_empty(&req->rq_unreplied_list)) {
		DEBUG_REQ(D_HA, req, "resend_replay=%d, last_transno=%llu",
			  imp->imp_resend_replay, last_transno);
		ptlrpc_add_unreplied(req);
		imp->imp_known_replied_xid = ptlrpc_known_replied_xid(imp);
	}

	imp->imp_resend_replay = 0;
	spin_unlock(&imp->imp_lock);

	if (req != NULL) {
		LASSERT(!list_empty(&req->rq_unreplied_list));

		rc = ptlrpc_replay_req(req);
		if (rc) {
			CERROR("recovery replay error %d for req %llu\n",
			       rc, req->rq_xid);
			RETURN(rc);
		}
		*inflight = 1;
	}
	RETURN(rc);
}

/*
 * Schedule resending of request on sending_list. This is done after
 * we completed replaying of requests and locks.
 */
int ptlrpc_resend(struct obd_import *imp)
{
	struct ptlrpc_request *req;

	ENTRY;

	/* As long as we're in recovery, nothing should be added to the sending
	 * list, so we don't need to hold the lock during this iteration and
	 * resend process.
	 *
	 * Well... what if lctl recover is called twice at the same time?
	 */
	spin_lock(&imp->imp_lock);
	if (imp->imp_state != LUSTRE_IMP_RECOVER) {
		spin_unlock(&imp->imp_lock);
		RETURN(-1);
	}

	list_for_each_entry(req, &imp->imp_sending_list, rq_list) {
		LASSERTF((long)req > PAGE_SIZE && req != LP_POISON,
			 "req %px bad\n", req);
		LASSERTF(req->rq_type != LI_POISON, "req %px freed\n", req);

		/* If the request is allowed to be sent during replay and it
		 * is not timeout yet, then it does not need to be resent.
		 */
		if (!ptlrpc_no_resend(req) &&
		    (req->rq_timedout || !req->rq_allow_replay))
			ptlrpc_resend_req(req);
	}
	spin_unlock(&imp->imp_lock);

	CFS_FAIL_TIMEOUT(OBD_FAIL_LDLM_ENQUEUE_OLD_EXPORT, 2);
	RETURN(0);
}

/*
 * Go through all requests in delayed list and wake their threads
 * for resending
 */
void ptlrpc_wake_delayed(struct obd_import *imp)
{
	struct ptlrpc_request *req;

	spin_lock(&imp->imp_lock);
	list_for_each_entry(req, &imp->imp_delayed_list, rq_list) {
		DEBUG_REQ(D_HA, req, "waking (set %p):", req->rq_set);
		ptlrpc_client_wake_req(req);
	}
	spin_unlock(&imp->imp_lock);
}

void ptlrpc_request_handle_notconn(struct ptlrpc_request *failed_req)
{
	struct obd_import *imp = failed_req->rq_import;
	int conn = lustre_msg_get_conn_cnt(failed_req->rq_reqmsg);

	ENTRY;
	CDEBUG(D_HA, "import %s of %s@%s abruptly disconnected: reconnecting\n",
		imp->imp_obd->obd_name, obd2cli_tgt(imp->imp_obd),
		imp->imp_connection->c_remote_uuid.uuid);

	if (ptlrpc_set_import_discon(imp, conn, true)) {
		/* to control recovery via lctl {disable|enable}_recovery */
		if (imp->imp_deactive == 0)
			ptlrpc_connect_import(imp);
	}

	/* Wait for recovery to complete and resend. If evicted, then this
	 * request will be errored out later
	 */
	spin_lock(&failed_req->rq_lock);
	if (!failed_req->rq_no_resend)
		failed_req->rq_resend = 1;
	spin_unlock(&failed_req->rq_lock);

	EXIT;
}

/**
 * ptlrpc_set_import_active() - Administratively active/deactive a client.
 * @imp: import object
 * @active: flag to activate(1) or de-activate(0)
 *
 * This should only be called by the ioctl interface, currently
 *  - the lctl deactivate and activate commands
 *  - echo 0/1 >> /proc/osc/XXX/active
 *  - client umount -f (ll_umount_begin)
 *
 * Returns:
 * * %0 on successful activation/deactivation
 * * %negative on failure
 */
int ptlrpc_set_import_active(struct obd_import *imp, int active)
{
	struct obd_device *obd = imp->imp_obd;
	int rc = 0;

	ENTRY;
	LASSERT(obd);

	/* When deactivating, mark import invalid, & abort in-flight requests */
	if (!active) {
		LCONSOLE_WARN("setting import %s INACTIVE by administrator request\n",
			      obd2cli_tgt(imp->imp_obd));

		/* set before invalidate to avoid messages about imp_inval
		 * set without imp_deactive in ptlrpc_import_delay_req
		 */
		spin_lock(&imp->imp_lock);
		imp->imp_deactive = 1;
		spin_unlock(&imp->imp_lock);

		obd_import_event(imp->imp_obd, imp, IMP_EVENT_DEACTIVATE);

		ptlrpc_invalidate_import(imp);
	}

	/* When activating, mark import valid, and attempt recovery */
	if (active) {
		CDEBUG(D_HA, "setting import %s VALID\n",
		       obd2cli_tgt(imp->imp_obd));

		spin_lock(&imp->imp_lock);
		imp->imp_deactive = 0;
		spin_unlock(&imp->imp_lock);
		obd_import_event(imp->imp_obd, imp, IMP_EVENT_ACTIVATE);

		rc = ptlrpc_recover_import(imp, NULL, 0);
	}

	RETURN(rc);
}
EXPORT_SYMBOL(ptlrpc_set_import_active);

bool ptlrpc_import_in_recovery_disconnect(struct obd_import *imp,
					  bool disconnect_is_recovery)
{
	bool in_recovery = true;

	spin_lock(&imp->imp_lock);
	if (imp->imp_state < LUSTRE_IMP_DISCON ||
	    (!disconnect_is_recovery && imp->imp_state == LUSTRE_IMP_DISCON) ||
	    imp->imp_state >= LUSTRE_IMP_FULL ||
	    imp->imp_obd->obd_no_recov)
		in_recovery = false;
	spin_unlock(&imp->imp_lock);

	return in_recovery;
}

/* Attempt to reconnect an import */
int ptlrpc_recover_import(struct obd_import *imp, char *new_uuid, int async)
{
	int rc = 0;

	ENTRY;
	spin_lock(&imp->imp_lock);
	if (imp->imp_state == LUSTRE_IMP_NEW || imp->imp_deactive ||
	    atomic_read(&imp->imp_inval_count))
		rc = -EINVAL;
	spin_unlock(&imp->imp_lock);
	if (rc)
		GOTO(out, rc);

	/* force import to be disconnected. */
	ptlrpc_set_import_discon(imp, 0, false);

	if (new_uuid) {
		struct obd_uuid uuid;

		/* intruct import to use new uuid */
		obd_str2uuid(&uuid, new_uuid);
		rc = import_set_conn_priority(imp, &uuid);
		if (rc)
			GOTO(out, rc);
	}

	/* Check if reconnect is already in progress */
	spin_lock(&imp->imp_lock);
	if (imp->imp_state != LUSTRE_IMP_DISCON) {
		imp->imp_force_verify = 1;
		rc = -EALREADY;
	}
	spin_unlock(&imp->imp_lock);

	if (!rc) {
		CFS_RACE(OBD_FAIL_PTLRPC_CONNECT_RACE);
		rc = ptlrpc_connect_import(imp);
	}

	if (!async && (rc == -EALREADY || rc == 0)) {
		long timeout = cfs_time_seconds(obd_timeout);

		CDEBUG(D_HA, "%s: recovery started, waiting %u seconds\n",
		       obd2cli_tgt(imp->imp_obd), obd_timeout);

		rc = wait_event_idle_timeout(imp->imp_recovery_waitq,
			!ptlrpc_import_in_recovery_disconnect(imp, true),
			timeout);
		if (rc == 0)
			rc = -ETIMEDOUT;
		else
			rc = 0;
		CDEBUG(D_HA, "%s: recovery finished %s, rc = %d\n",
		       obd2cli_tgt(imp->imp_obd),
		       ptlrpc_import_state_name(imp->imp_state), rc);
	}
	EXIT;

out:
	return rc;
}
EXPORT_SYMBOL(ptlrpc_recover_import);

bool ptlrpc_import_in_recovery(struct obd_import *imp)
{
	return ptlrpc_import_in_recovery_disconnect(imp, false);
}
