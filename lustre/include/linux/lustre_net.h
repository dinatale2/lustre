/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 *  Copyright (C) 2002 Cluster File Systems, Inc.
 *
 *   This file is part of Lustre, http://www.lustre.org.
 *
 *   Lustre is free software; you can redistribute it and/or
 *   modify it under the terms of version 2 of the GNU General Public
 *   License as published by the Free Software Foundation.
 *
 *   Lustre is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Lustre; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef _LUSTRE_NET_H
#define _LUSTRE_NET_H

#include <linux/kp30.h>
#include <linux/obd_support.h>
#include <linux/obd_class.h>
#include <portals/p30.h>
#include <linux/lustre_idl.h>

/* FOO_REQUEST_PORTAL is for incoming requests on the FOO
 * FOO_REPLY_PORTAL   is for incoming replies on the FOO
 * FOO_BULK_PORTAL    is for incoming bulk on the FOO
 */

#define OSC_REQUEST_PORTAL 1
#define OSC_REPLY_PORTAL   2
#define OSC_BULK_PORTAL    3

#define OST_REQUEST_PORTAL 4
#define OST_REPLY_PORTAL   5
#define OST_BULK_PORTAL    6

#define MDC_REQUEST_PORTAL 7
#define MDC_REPLY_PORTAL   8
#define MDC_BULK_PORTAL    9

#define MDS_REQUEST_PORTAL 10
#define MDS_REPLY_PORTAL   11
#define MDS_BULK_PORTAL    12

/* default rpc ring length */
#define RPC_RING_LENGTH    2

/* generic wrappable next */
#define NEXT_INDEX(index, max)	(((index+1) >= max) ? 0 : (index+1))

#define SVC_STOPPING 1
#define SVC_RUNNING 2
#define SVC_STOPPED 4
#define SVC_KILLED  8
#define SVC_EVENT  16
#define SVC_LIST   32
#define SVC_SIGNAL 64

typedef int (*rep_unpack_t)(char *, int, struct ptlrep_hdr **, union ptl_rep *);
typedef int (*req_pack_t)(char *, int, char *, int, struct ptlreq_hdr **,
                          union ptl_req*, int *, char **); 

typedef int (*req_unpack_t)(char *buf, int len, struct ptlreq_hdr **, 
                            union ptl_req *);
typedef int (*rep_pack_t)(char *buf1, int len1, char *buf2, int len2,
                          struct ptlrep_hdr **, union ptl_rep*, 
                          int *replen, char **repbuf); 

struct ptlrpc_client {
        struct lustre_peer cli_server;
        struct obd_device *cli_obd;
        __u32 cli_request_portal;
        __u32 cli_reply_portal;
        rep_unpack_t cli_rep_unpack;
        req_pack_t cli_req_pack;

        spinlock_t cli_lock;
        __u32 cli_xid;
};

/* These do double-duty in rq_type and rq_flags */
#define PTL_RPC_INTR    1
#define PTL_RPC_REQUEST 2
#define PTL_RPC_REPLY   3
#define PTL_RPC_BULK    4
#define PTL_RPC_SENT    5
#define PTL_BULK_SENT   6
#define PTL_BULK_RCVD   6

struct ptlrpc_request { 
        int rq_type; /* one of PTL_RPC_REQUEST, PTL_RPC_REPLY, PTL_RPC_BULK */
	struct list_head rq_list;
	struct obd_device *rq_obd;
	int rq_status;
        int rq_flags; 
        __u32 rq_connid;
        __u32 rq_xid;

	char *rq_reqbuf;
	int rq_reqlen;
	struct ptlreq_hdr *rq_reqhdr;
	union ptl_req rq_req;

 	char *rq_repbuf;
	int rq_replen;
	struct ptlrep_hdr *rq_rephdr;
	union ptl_rep rq_rep;

        char *rq_bulkbuf;
        int rq_bulklen;

        void * rq_reply_handle;
	wait_queue_head_t rq_wait_for_rep;

        /* incoming reply */
        ptl_md_t rq_reply_md;
        ptl_handle_md_t rq_reply_md_h;
        ptl_handle_me_t rq_reply_me_h;

        /* outgoing req/rep */
        ptl_md_t rq_req_md;
        ptl_handle_md_t rq_req_md_h;

        __u32 rq_reply_portal;
        __u32 rq_req_portal;

        struct lustre_peer rq_peer;
};

struct ptlrpc_bulk_desc {
        int b_flags;
        struct lustre_peer b_peer;
        __u32 b_portal;
        char *b_buf;
        int b_buflen;
        int (*b_cb)(struct ptlrpc_request *, void *);
        __u32 b_xid;

	wait_queue_head_t b_waitq;

        ptl_md_t b_md;
        ptl_handle_md_t b_md_h;
        ptl_handle_me_t b_me_h;
};

struct ptlrpc_service {
        /* incoming request buffers */
        /* FIXME: perhaps a list of EQs, if multiple NIs are used? */
        char *srv_buf[RPC_RING_LENGTH];
        __u32 srv_buf_size;
        __u32 srv_me_active;
	__u32 srv_me_tail;
	__u32 srv_md_active;
        __u32 srv_ring_length;
        __u32 srv_req_portal;
        __u32 srv_rep_portal;
        __u32 srv_ref_count[RPC_RING_LENGTH];
        ptl_handle_me_t srv_me_h[RPC_RING_LENGTH];
        ptl_process_id_t srv_id;
        ptl_md_t srv_md[RPC_RING_LENGTH];
        ptl_handle_md_t srv_md_h[RPC_RING_LENGTH];
        __u32 srv_xid;

        /* event queue */
        ptl_handle_eq_t srv_eq_h;

        __u32 srv_flags; 
        struct lustre_peer srv_self;
	struct task_struct *srv_thread;
	wait_queue_head_t srv_waitq;
	wait_queue_head_t srv_ctl_waitq;
	int ost_flags;

	spinlock_t srv_lock;
	struct list_head srv_reqs;
        ptl_event_t  srv_ev;
        req_unpack_t      srv_req_unpack;
        rep_pack_t        srv_rep_pack;
        int (*srv_handler)(struct obd_device *obddev, 
                           struct ptlrpc_service *svc,
                           struct ptlrpc_request *req);
};

typedef int (*svc_handler_t)(struct obd_device *obddev,
                             struct ptlrpc_service *svc,
                             struct ptlrpc_request *req);



/* rpc/niobuf.c */
int ptlrpc_check_bulk_sent(struct ptlrpc_bulk_desc *);
int ptlrpc_send_bulk(struct ptlrpc_bulk_desc *, int portal);
int ptl_send_buf(struct ptlrpc_request *, struct lustre_peer *, int portal);
int ptlrpc_register_bulk(struct ptlrpc_bulk_desc *);
int ptlrpc_reply(struct obd_device *obddev, struct ptlrpc_service *svc,
                 struct ptlrpc_request *req);
int ptlrpc_error(struct obd_device *obddev, struct ptlrpc_service *svc,
                 struct ptlrpc_request *req);
int ptl_send_rpc(struct ptlrpc_request *request, struct lustre_peer *peer);
int ptl_received_rpc(struct ptlrpc_service *service);

/* rpc/client.c */
int ptlrpc_connect_client(int dev, char *uuid, int req_portal, int rep_portal, 
                          req_pack_t req_pack, rep_unpack_t rep_unpack,
                          struct ptlrpc_client *cl);
int ptlrpc_queue_wait(struct ptlrpc_client *cl, struct ptlrpc_request *req);
int ptlrpc_queue_req(struct ptlrpc_client *peer, struct ptlrpc_request *req);
struct ptlrpc_request *ptlrpc_prep_req(struct ptlrpc_client *cl, 
                                       int opcode, int namelen, char *name,
                                       int tgtlen, char *tgt);
void ptlrpc_free_req(struct ptlrpc_request *request);
struct ptlrpc_bulk_desc *ptlrpc_prep_bulk(struct lustre_peer *);

/* rpc/service.c */
struct ptlrpc_service *
ptlrpc_init_svc(__u32 bufsize, int req_portal, int rep_portal, char *uuid,
                req_unpack_t unpack, rep_pack_t pack, svc_handler_t);
void ptlrpc_stop_thread(struct ptlrpc_service *svc);
int ptlrpc_start_thread(struct obd_device *dev, struct ptlrpc_service *svc,
                        char *name);
int rpc_register_service(struct ptlrpc_service *service, char *uuid);
int rpc_unregister_service(struct ptlrpc_service *service);

struct ptlrpc_svc_data { 
        char *name;
        struct ptlrpc_service *svc; 
        struct obd_device *dev;
}; 

#endif
