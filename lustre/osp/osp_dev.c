/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright  2009 Sun Microsystems, Inc. All rights reserved
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/osp/osp_dev.c
 *
 * Lustre OST Proxy Device
 *
 * Author: Alex Zhuravlev <bzzz@sun.com>
 * Author: Mikhail Pershin <tappro@whamcloud.com>
 */

#ifndef EXPORT_SYMTAB
# define EXPORT_SYMTAB
#endif
#define DEBUG_SUBSYSTEM S_MDS

#include <obd_class.h>
#include <lustre_param.h>

#include "osp_internal.h"

extern struct lu_object_operations osp_lu_obj_ops;

struct lu_object *osp_object_alloc(const struct lu_env *env,
                                    const struct lu_object_header *hdr,
                                    struct lu_device *d)
{
        struct lu_object_header *h;
        struct osp_object       *o;
        struct lu_object        *l;

        LASSERT(hdr == NULL);

        OBD_ALLOC_PTR(o);
        if (o != NULL) {
                l = &o->opo_obj.do_lu;
                h = &o->opo_header;

                lu_object_header_init(h);
                dt_object_init(&o->opo_obj, h, d);
                lu_object_add_top(h, l);

                l->lo_ops = &osp_lu_obj_ops;

                return l;
        } else {
                return NULL;
        }
}

static int osp_shutdown(const struct lu_env *env, struct osp_device *d)
{
        struct obd_import *imp;
        int rc = 0;
        ENTRY;

        /* release last_used file */
        lu_object_put(env, &d->opd_last_used_file->do_lu);

        imp = d->opd_obd->u.cli.cl_import;

        /* Mark import deactivated now, so we don't try to reconnect if any
         * of the cleanup RPCs fails (e.g. ldlm cancel, etc).  We don't
         * fully deactivate the import, or that would drop all requests. */
        cfs_spin_lock(&imp->imp_lock);
        imp->imp_deactive = 1;
        cfs_spin_unlock(&imp->imp_lock);

        ptlrpc_deactivate_import(imp);

        /* Some non-replayable imports (MDS's OSCs) are pinged, so just
         * delete it regardless.  (It's safe to delete an import that was
         * never added.) */
        (void) ptlrpc_pinger_del_import(imp);

        rc = ptlrpc_disconnect_import(imp, 0);
        if (rc)
                CERROR("can't disconnect: %d\n", rc);

        ptlrpc_invalidate_import(imp);

        /* stop precreate thread */
        osp_precreate_fini(d);

        /* stop sync thread */
        osp_sync_fini(d);

        RETURN(rc);
}

static int osp_process_config(const struct lu_env *env,
                              struct lu_device *dev,
                              struct lustre_cfg *lcfg)
{
        struct osp_device *d = lu2osp_dev(dev);
        int                rc;
        ENTRY;

        switch(lcfg->lcfg_command) {

                case LCFG_CLEANUP:
                        rc = osp_shutdown(env, d);
                        break;

                case LCFG_PARAM: {
                        struct lprocfs_static_vars lvars = { 0 };

                        lprocfs_osp_init_vars(&lvars);

                        LASSERT(d->opd_obd);
                        rc = class_process_proc_param(PARAM_OSP, lvars.obd_vars,
                                                      lcfg, d->opd_obd);
                        if (rc > 0)
                                rc = 0;
                        break;
                }

                default:
                        CERROR("unknown command %u\n", lcfg->lcfg_command);
                        rc = 0;
                        break;
        }

        RETURN(rc);
}

static int osp_recovery_complete(const struct lu_env *env,
                                     struct lu_device *dev)
{
        struct osp_device *osp = lu2osp_dev(dev);
        int                rc = 0;
        ENTRY;
        osp->opd_recovery_completed = 1;
        cfs_waitq_signal(&osp->opd_pre_waitq);
        RETURN(rc);
}

const struct lu_device_operations osp_lu_ops = {
        .ldo_object_alloc      = osp_object_alloc,
        .ldo_process_config    = osp_process_config,
        .ldo_recovery_complete = osp_recovery_complete,
};

/**
 * provides with statfs from corresponded OST
 *
 */
static int osp_statfs(const struct lu_env *env,
                       struct dt_device *dev, struct obd_statfs *sfs)
{
        struct osp_device *d = dt2osp_dev(dev);
        ENTRY;

        if (unlikely(d->opd_imp_active == 0)) {
                /*
                 * in case of inactive OST we return nulls
                 * so that caller can understand this device
                 * is unusable for new objects
                 *
                 * XXX: shouldn't we take normal statfs and fill
                 * just few specific fields with zeroes? 
                 */
                memset(sfs, 0, sizeof(*sfs));
                sfs->os_bsize = 4096;
                RETURN(0);
        }

        /* return recently updated data */
        *sfs = d->opd_statfs;

        /*
         * layer above osp (usually lod) can use ffree to estimate
         * how many objects are available for immediate creation
         */
        cfs_spin_lock(&d->opd_pre_lock);
        sfs->os_ffree = d->opd_pre_last_created - d->opd_pre_next;
        cfs_spin_unlock(&d->opd_pre_lock);

        CDEBUG(D_OTHER, "%s: %lu blocks, %lu free, %lu avail, "
               "%lu files, %lu free files\n", d->opd_obd->obd_name,
               (unsigned long) sfs->os_blocks, (unsigned long) sfs->os_bfree,
               (unsigned long) sfs->os_bavail, (unsigned long) sfs->os_files,
               (unsigned long) sfs->os_ffree);
        RETURN(0);
}

static int osp_sync(const struct lu_env *env, struct dt_device *dev)
{
        ENTRY;

        /*
         * XXX: wake up sync thread, command it to start flushing asap?
         */

        RETURN(0);
}

static const struct dt_device_operations osp_dt_ops = {
        .dt_statfs         = osp_statfs,
        .dt_sync           = osp_sync,
};

static int osp_init_last_used(const struct lu_env *env, struct osp_device *m)
{
        struct osp_thread_info *osi = osp_env_info(env);
        struct dt_object_format dof = { 0 };
        struct dt_object       *o;
        int                     rc;
        ENTRY;

        osi->osi_attr.la_valid = LA_MODE;
        osi->osi_attr.la_mode = S_IFREG | 0666;
        lu_local_obj_fid(&osi->osi_fid, MDD_LOV_OBJ_OID);
        dof.dof_type = DFT_REGULAR;
        o = dt_find_or_create(env, m->opd_storage, &osi->osi_fid, &dof,
                              &osi->osi_attr);
        if (IS_ERR(o))
                RETURN(PTR_ERR(o));

        rc = dt_attr_get(env, o, &osi->osi_attr, NULL);
        if (rc)
                GOTO(out, rc);

        m->opd_last_used_file = o;
        m->opd_last_used_id = 0;

        if (osi->osi_attr.la_size >= sizeof(osi->osi_id) * (m->opd_index + 1)) {
                osp_objid_buf_prep(osi, m->opd_index);
                rc = dt_record_read(env, o, &osi->osi_lb, &osi->osi_off);
                if (rc != 0)
                        GOTO(out, rc);
                m->opd_last_used_id = le64_to_cpu(osi->osi_id);
        }
        RETURN(0);
out:
        /* object will be released in device cleanup path */
        lu_object_put(env, &o->do_lu);
        m->opd_last_used_file = NULL;
        RETURN(rc);
}

static int osp_connect_to_osd(const struct lu_env *env, struct osp_device *m,
                              const char *nextdev)
{
        struct obd_connect_data *data = NULL;
        struct obd_device       *obd;
        int                      rc;
        ENTRY;

        LASSERT(m->opd_storage_exp == NULL);

        OBD_ALLOC_PTR(data);
        if (data == NULL)
                RETURN(-ENOMEM);

        obd = class_name2obd(nextdev);
        if (obd == NULL) {
                CERROR("can't locate next device: %s\n", nextdev);
                GOTO(out, rc = -ENOTCONN);
        }

        rc = obd_connect(env, &m->opd_storage_exp, obd, &obd->obd_uuid, data,
                         NULL);
        if (rc) {
                CERROR("cannot connect to next dev %s (%d)\n", nextdev, rc);
                GOTO(out, rc);
        }

        m->opd_dt_dev.dd_lu_dev.ld_site =
                m->opd_storage_exp->exp_obd->obd_lu_dev->ld_site;
        LASSERT(m->opd_dt_dev.dd_lu_dev.ld_site);
        m->opd_storage = lu2dt_dev(m->opd_storage_exp->exp_obd->obd_lu_dev);

out:
        OBD_FREE_PTR(data);
        RETURN(rc);
}

static int osp_init0(const struct lu_env *env, struct osp_device *m,
                      struct lu_device_type *ldt, struct lustre_cfg *cfg)
{
        struct lprocfs_static_vars  lvars = { 0 };
        struct proc_dir_entry      *osc_proc_dir;
        struct obd_import          *imp;
        class_uuid_t                uuid;
        int                         rc;
        ENTRY;

        m->opd_obd = class_name2obd(lustre_cfg_string(cfg, 0));
        if (m->opd_obd == NULL) {
                CERROR("Cannot find obd with name %s\n",
                       lustre_cfg_string(cfg, 0));
                RETURN(-ENODEV);
        }

        if (sscanf(lustre_cfg_buf(cfg, 4), "%d", &m->opd_index) != 1) {
                CERROR("can't find index in configuration\n");
                RETURN(-EINVAL);
        }

        m->opd_dt_dev.dd_lu_dev.ld_ops = &osp_lu_ops;
        m->opd_dt_dev.dd_ops = &osp_dt_ops;
        m->opd_obd->obd_lu_dev = &m->opd_dt_dev.dd_lu_dev;

        rc = osp_connect_to_osd(env, m, lustre_cfg_string(cfg, 3));
        if (rc)
                GOTO(out_fini, rc);

        rc = ptlrpcd_addref();
        if (rc)
                GOTO(out_disconnect, rc);

        rc = client_obd_setup(m->opd_obd, cfg);
        if (rc) {
                CERROR("can't setup obd: %d\n", rc);
                GOTO(out_ref, rc);
        }

        /* XXX: how do we do this well? */
        m->opd_obd->obd_set_up = 1;

        lprocfs_osp_init_vars(&lvars);
        if (lprocfs_obd_setup(m->opd_obd, lvars.obd_vars) == 0)
                ptlrpc_lprocfs_register_obd(m->opd_obd);

        /* for compatibility we link old procfs's OSC entries to osp ones */
        osc_proc_dir = lprocfs_srch(proc_lustre_root, "osc");
        if (osc_proc_dir) {
                cfs_proc_dir_entry_t *symlink = NULL;
                char *name, *p;
                OBD_ALLOC(name, strlen(m->opd_obd->obd_name) + 1);
                if (name) {
                        strcpy(name, m->opd_obd->obd_name);
                        p = strstr(name, "osp");
                        if (p) {
                                p[2] = 'c';
                                symlink = lprocfs_add_symlink(name,
                                                osc_proc_dir,
                                                "../osp/%s",
                                                m->opd_obd->obd_name);
                        }
                        OBD_FREE(name, strlen(m->opd_obd->obd_name) + 1);
                        m->opd_symlink = symlink;
                }
        }

        /*
         * Initialize last id from the storage - will be used in orphan cleanup
         */
        rc = osp_init_last_used(env, m);
        if (rc)
                GOTO(out_proc, rc);

        /*
         * Initialize precreation thread, it handles new connections as well
         */
        rc = osp_init_precreate(m);
        if (rc)
                GOTO(out_proc, rc);

        /*
         * Initialize synhronization mechanism taking care of propogating
         * changes to OST in near transactional manner
         */
        rc = osp_sync_init(env, m);
        if (rc)
                GOTO(out_precreat, rc);

        /*
         * Initiate connect to OST
         */
        ll_generate_random_uuid(uuid);
        class_uuid_unparse(uuid, &m->opd_cluuid);

        imp = m->opd_obd->u.cli.cl_import;

        rc = ptlrpc_init_import(imp);
        if (rc)
                GOTO(out, rc);
        RETURN(0);

out:
        /* stop sync thread */
        osp_sync_fini(m);
out_precreat:
        /* stop precreate thread */
        osp_precreate_fini(m);
out_proc:
        ptlrpc_lprocfs_unregister_obd(m->opd_obd);
        lprocfs_obd_cleanup(m->opd_obd);
        client_obd_cleanup(m->opd_obd);
out_ref:
        ptlrpcd_decref();
out_disconnect:
        obd_disconnect(m->opd_storage_exp);
out_fini:
        dt_device_fini(&m->opd_dt_dev);
        RETURN(rc);
}

static struct lu_device *osp_device_free(const struct lu_env *env,
                                         struct lu_device *lu)
{
        struct osp_device *m = lu2osp_dev(lu);
        ENTRY;

        dt_device_fini(&m->opd_dt_dev);
        OBD_FREE_PTR(m);
        RETURN(NULL);
}

static struct lu_device *osp_device_alloc(const struct lu_env *env,
                                          struct lu_device_type *t,
                                          struct lustre_cfg *lcfg)
{
        struct osp_device *m;
        struct lu_device  *l;

        OBD_ALLOC_PTR(m);
        if (m == NULL) {
                l = ERR_PTR(-ENOMEM);
        } else {
                int rc;

                l = osp2lu_dev(m);
                dt_device_init(&m->opd_dt_dev, t);
                rc = osp_init0(env, m, t, lcfg);
                if (rc != 0) {
                        osp_device_free(env, l);
                        l = ERR_PTR(rc);
                }
        }
        return l;
}

static struct lu_device *osp_device_fini(const struct lu_env *env,
                                         struct lu_device *d)
{
        struct osp_device *m = lu2osp_dev(d);
        struct obd_import *imp;
        int                rc;
        ENTRY;

        LASSERT(m->opd_storage_exp);
        obd_disconnect(m->opd_storage_exp);

        imp = m->opd_obd->u.cli.cl_import;

        if (imp->imp_rq_pool) {
                ptlrpc_free_rq_pool(imp->imp_rq_pool);
                imp->imp_rq_pool = NULL;
        }

        obd_cleanup_client_import(m->opd_obd);

        if (m->opd_symlink)
                lprocfs_remove(&m->opd_symlink);

        LASSERT(m->opd_obd);
        ptlrpc_lprocfs_unregister_obd(m->opd_obd);
        lprocfs_obd_cleanup(m->opd_obd);

        rc = client_obd_cleanup(m->opd_obd);
        LASSERTF(rc == 0, "error %d\n", rc);

        ptlrpcd_decref();

        RETURN(NULL);
}

static int osp_reconnect(const struct lu_env *env,
                         struct obd_export *exp, struct obd_device *obd,
                         struct obd_uuid *cluuid,
                         struct obd_connect_data *data,
                         void *localdata)
{
        /* XXX: nothing to do ? */
        return 0;
}

/*
 * we use exports to track all LOD users
 */
static int osp_obd_connect(const struct lu_env *env, struct obd_export **exp,
                           struct obd_device *obd, struct obd_uuid *cluuid,
                           struct obd_connect_data *data, void *localdata)
{
        struct osp_device       *osp = lu2osp_dev(obd->obd_lu_dev);
        struct obd_connect_data *ocd;
        struct obd_import       *imp;
        struct lustre_handle     conn;
        int                      rc;
        ENTRY;

        CDEBUG(D_CONFIG, "connect #%d\n", osp->opd_connects);

        rc = class_connect(&conn, obd, cluuid);
        if (rc)
                RETURN(rc);

        *exp = class_conn2export(&conn);

        /* Why should there ever be more than 1 connect? */
        /* XXX: locking ? */
        osp->opd_connects++;
        LASSERT(osp->opd_connects == 1);

        imp = osp->opd_obd->u.cli.cl_import;
        imp->imp_dlm_handle = conn;

        /* XXX: which flags we need on OST? */
        ocd = &imp->imp_connect_data;
        ocd->ocd_version = LUSTRE_VERSION_CODE;
        ocd->ocd_connect_flags = OBD_CONNECT_VERSION
                                 | OBD_CONNECT_AT
                                 | OBD_CONNECT_MDS
                                 | OBD_CONNECT_SKIP_ORPHAN;
        if (data->ocd_connect_flags & OBD_CONNECT_INDEX) {
                ocd->ocd_connect_flags |= OBD_CONNECT_INDEX;
                ocd->ocd_index = data->ocd_index;
        }
        imp->imp_connect_flags_orig = ocd->ocd_connect_flags;

        rc = ptlrpc_connect_import(imp);
        LASSERT(rc == 0);

        if (rc) {
                CERROR("can't connect obd: %d\n", rc);
                GOTO(out, rc);
        }

        ptlrpc_pinger_add_import(imp);

out:
        RETURN(rc);
}

/*
 * once last export (we don't count self-export) disappeared
 * osp can be released
 */
static int osp_obd_disconnect(struct obd_export *exp)
{
        struct obd_device *obd = exp->exp_obd;
        struct osp_device *osp = lu2osp_dev(obd->obd_lu_dev);
        int                rc;
        ENTRY;

        /* Only disconnect the underlying layers on the final disconnect. */
        /* XXX: locking ? */
        LASSERT(osp->opd_connects == 1);
        osp->opd_connects--;

        rc = class_disconnect(exp); /* bz 9811 */

        /* destroy the device */
        if (rc == 0)
                class_manual_cleanup(obd);

        RETURN(rc);
}

/*
 * lprocfs helpers still use OBD API, let's keep obd_statfs() support for a while
 */
static int osp_obd_statfs(struct obd_device *obd, struct obd_statfs *osfs,
                          __u64 max_age, __u32 flags)
{
        struct obd_statfs     *msfs;
        struct ptlrpc_request *req;
        struct obd_import     *imp = NULL;
        int rc;
        ENTRY;

        /*Since the request might also come from lprocfs, so we need
         *sync this with client_disconnect_export Bug15684*/
        cfs_down_read(&obd->u.cli.cl_sem);
        if (obd->u.cli.cl_import)
                imp = class_import_get(obd->u.cli.cl_import);
        cfs_up_read(&obd->u.cli.cl_sem);
        if (!imp)
                RETURN(-ENODEV);

        /* We could possibly pass max_age in the request (as an absolute
         * timestamp or a "seconds.usec ago") so the target can avoid doing
         * extra calls into the filesystem if that isn't necessary (e.g.
         * during mount that would help a bit).  Having relative timestamps
         * is not so great if request processing is slow, while absolute
         * timestamps are not ideal because they need time synchronization. */
        req = ptlrpc_request_alloc(imp, &RQF_OST_STATFS);

        class_import_put(imp);

        if (req == NULL)
                RETURN(-ENOMEM);

        rc = ptlrpc_request_pack(req, LUSTRE_OST_VERSION, OST_STATFS);
        if (rc) {
                ptlrpc_request_free(req);
                RETURN(rc);
        }
        ptlrpc_request_set_replen(req);
        req->rq_request_portal = OST_CREATE_PORTAL;
        ptlrpc_at_set_req_timeout(req);

        if (flags & OBD_STATFS_NODELAY) {
                /* procfs requests not want stat in wait for avoid deadlock */
                req->rq_no_resend = 1;
                req->rq_no_delay = 1;
        }

        rc = ptlrpc_queue_wait(req);
        if (rc)
                GOTO(out, rc);

        msfs = req_capsule_server_get(&req->rq_pill, &RMF_OBD_STATFS);
        if (msfs == NULL) {
                GOTO(out, rc = -EPROTO);
        }

        *osfs = *msfs;

        EXIT;
 out:
        ptlrpc_req_finished(req);
        return rc;
}

static int osp_import_event(struct obd_device *obd,
                            struct obd_import *imp,
                            enum obd_import_event event)
{
        struct osp_device *d = lu2osp_dev(obd->obd_lu_dev);

        switch (event) {
                case IMP_EVENT_DISCON:
                        d->opd_got_disconnected = 1;
                        d->opd_imp_connected = 0;
                        osp_pre_update_status(d, -ENODEV);
                        cfs_waitq_signal(&d->opd_pre_waitq);
                        CDEBUG(D_HA, "got disconnected\n");
                        break;

                case IMP_EVENT_INACTIVE:
                        d->opd_imp_active = 0;
                        osp_pre_update_status(d, -ENODEV);
                        cfs_waitq_signal(&d->opd_pre_waitq);
                        CDEBUG(D_HA, "got inactive\n");
                        break;

                case IMP_EVENT_INVALIDATE:
                        /* XXX: what? */
                        break;

                case IMP_EVENT_ACTIVE:
                        d->opd_imp_active = 1;
                        if (d->opd_got_disconnected)
                                d->opd_new_connection = 1;
                        d->opd_imp_connected = 1;
                        cfs_waitq_signal(&d->opd_pre_waitq);
                        __osp_sync_check_for_work(d);
                        CDEBUG(D_HA, "got connected\n");
                        break;

                case IMP_EVENT_OCD:
                        /* XXX: what? */
                        break;
                case IMP_EVENT_DEACTIVATE:
                case IMP_EVENT_ACTIVATE:
                        /* XXX: need fix? */
                        break;

                default:
                        LBUG();
        }
        return 0;
}

static int osp_iocontrol(unsigned int cmd, struct obd_export *exp, int len,
                         void *karg, void *uarg)
{
        struct obd_device *obd = exp->exp_obd;
        struct osp_device *d;
        struct obd_ioctl_data *data = karg;
        int err = 0;
        ENTRY;

        LASSERT(obd->obd_lu_dev);
        d = lu2osp_dev(obd->obd_lu_dev);
        LASSERT(d->opd_dt_dev.dd_ops == &osp_dt_ops);

        if (!cfs_try_module_get(THIS_MODULE)) {
                CERROR("Can't get module. Is it alive?");
                return -EINVAL;
        }
        switch (cmd) {
        case OBD_IOC_LOV_GET_CONFIG:
        case LL_IOC_LOV_SETSTRIPE:
        case LL_IOC_LOV_GETSTRIPE:
                CERROR("unsupported function: %d\n", cmd);
                GOTO(out, err);

        case OBD_IOC_CLIENT_RECOVER:
                err = ptlrpc_recover_import(obd->u.cli.cl_import,
                                            data->ioc_inlbuf1, 0);
                if (err > 0)
                        err = 0;
                GOTO(out, err);

        case OBD_IOC_POLL_QUOTACHECK:
                CERROR("unsupported function: %d\n", cmd);
                GOTO(out, err);

        case IOC_OSC_SET_ACTIVE:
                err = ptlrpc_set_import_active(obd->u.cli.cl_import,
                                               data->ioc_offset);
                GOTO(out, err);

        case OBD_IOC_PING_TARGET:
                err = ptlrpc_obd_ping(obd);
                GOTO(out, err);

        default:
                CDEBUG(D_INODE, "unrecognized ioctl %#x by %s\n",
                       cmd, cfs_curproc_comm());
                GOTO(out, err = -ENOTTY);
        }
out:
        cfs_module_put(THIS_MODULE);
        return err;
}



/* context key constructor/destructor: mdt_key_init, mdt_key_fini */
LU_KEY_INIT_FINI(osp, struct osp_thread_info);
static void osp_key_exit(const struct lu_context *ctx,
                         struct lu_context_key *key, void *data)
{
        struct osp_thread_info *info = data;

        info->osi_attr.la_valid = 0;
}

struct lu_context_key osp_thread_key = {
        .lct_tags = LCT_MD_THREAD,
        .lct_init = osp_key_init,
        .lct_fini = osp_key_fini,
        .lct_exit = osp_key_exit
};

/* context key constructor/destructor: mdt_txn_key_init, mdt_txn_key_fini */
LU_KEY_INIT_FINI(osp_txn, struct osp_txn_info);

struct lu_context_key osp_txn_key = {
        .lct_tags = LCT_OSP_THREAD,
        .lct_init = osp_txn_key_init,
        .lct_fini = osp_txn_key_fini
};
LU_TYPE_INIT_FINI(osp, &osp_thread_key, &osp_txn_key);

static struct lu_device_type_operations osp_device_type_ops = {
        .ldto_init           = osp_type_init,
        .ldto_fini           = osp_type_fini,

        .ldto_start          = osp_type_start,
        .ldto_stop           = osp_type_stop,

        .ldto_device_alloc   = osp_device_alloc,
        .ldto_device_free    = osp_device_free,

        .ldto_device_fini    = osp_device_fini
};

static struct lu_device_type osp_device_type = {
        .ldt_tags     = LU_DEVICE_DT,
        .ldt_name     = LUSTRE_OSP_NAME,
        .ldt_ops      = &osp_device_type_ops,
        .ldt_ctx_tags = LCT_MD_THREAD
};

static int osp_obd_health_check(struct obd_device *obd)
{
        struct osp_device *d = lu2osp_dev(obd->obd_lu_dev);
        int                rc;
        ENTRY;

        LASSERT(d);
        if (d->opd_imp_active)
                rc = 0;
        else
                rc = 1;
        RETURN(rc);
}

static struct obd_ops osp_obd_device_ops = {
        .o_owner                = THIS_MODULE,
        .o_add_conn             = client_import_add_conn,
        .o_del_conn             = client_import_del_conn,
        .o_reconnect            = osp_reconnect,
        .o_connect              = osp_obd_connect,
        .o_disconnect           = osp_obd_disconnect,
        .o_health_check         = osp_obd_health_check,
        .o_import_event         = osp_import_event,
        .o_iocontrol            = osp_iocontrol,
        .o_statfs               = osp_obd_statfs,
};


static int __init osp_mod_init(void)
{
        struct lprocfs_static_vars  lvars;
        cfs_proc_dir_entry_t       *osc_proc_dir;
        int                         rc;

        lprocfs_osp_init_vars(&lvars);

        rc = class_register_type(&osp_obd_device_ops, NULL, lvars.module_vars,
                                 LUSTRE_OSP_NAME, &osp_device_type);

        /* create "osc" entry in procfs for compatibility purposes */
        if (rc == 0) {
                osc_proc_dir = lprocfs_srch(proc_lustre_root, "osc");
                if (osc_proc_dir == NULL) {
                        osc_proc_dir = lprocfs_register("osc", proc_lustre_root,
                                                        NULL, NULL);
                        if (IS_ERR(osc_proc_dir))
                                CERROR("osp: can't create compat entry \"osc\": %d\n",
                                       (int) PTR_ERR(osc_proc_dir));
                }
        }

        return rc;
}

static void __exit osp_mod_exit(void)
{
        lprocfs_try_remove_proc_entry("osc", proc_lustre_root);

        class_unregister_type(LUSTRE_OSP_NAME);
}

MODULE_AUTHOR("Sun Microsystems, Inc. <http://www.lustre.org/>");
MODULE_DESCRIPTION("Lustre OST Proxy Device ("LUSTRE_OSP_NAME")");
MODULE_LICENSE("GPL");

cfs_module(osp, "0.1.0", osp_mod_init, osp_mod_exit);

