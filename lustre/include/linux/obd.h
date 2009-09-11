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
 * Copyright  2008 Sun Microsystems, Inc. All rights reserved
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef __LINUX_OBD_H
#define __LINUX_OBD_H

#ifndef __OBD_H
#error Do not #include this file directly. #include <obd.h> instead
#endif

#include <obd_support.h>

#ifdef __KERNEL__
# include <linux/fs.h>
# include <linux/list.h>
# include <linux/sched.h>  /* for struct task_struct, for current.h */
# include <asm/current.h>  /* for smp_lock.h */
# include <linux/smp_lock.h>
# include <linux/proc_fs.h>
# include <linux/mount.h>
# ifndef HAVE_VFS_INTENT_PATCHES
#  include <linux/lustre_intent.h>
# endif
#endif

typedef struct {
        spinlock_t          lock;

#ifdef CLIENT_OBD_LIST_LOCK_DEBUG
        unsigned long       time;
        struct task_struct *task;
        const char         *func;
        int                 line;
#endif

} client_obd_lock_t;

#ifdef CLIENT_OBD_LIST_LOCK_DEBUG
static inline void __client_obd_list_lock(client_obd_lock_t *lock,
                                          const char *func,
                                          int line)
{
        unsigned long cur = jiffies;
        while (1) {
                if (spin_trylock(&lock->lock)) {
                        LASSERT(lock->task == NULL);
                        lock->task = current;
                        lock->func = func;
                        lock->line = line;
                        lock->time = jiffies;
                        break;
                }

                if ((jiffies - cur > 5 * HZ) &&
                    (jiffies - lock->time > 5 * HZ)) {
                        LCONSOLE_WARN("LOCK UP! the lock %p was acquired"
                                      " by <%s:%d:%s:%d> %lu time, I'm %s:%d\n",
                                      lock, lock->task->comm, lock->task->pid,
                                      lock->func, lock->line,
                                      (jiffies - lock->time),
                                      current->comm, current->pid);
                        LCONSOLE_WARN("====== for process holding the "
                                      "lock =====\n");
                        libcfs_debug_dumpstack(lock->task);
                        LCONSOLE_WARN("====== for current process =====\n");
                        libcfs_debug_dumpstack(NULL);
                        LCONSOLE_WARN("====== end =======\n");
                        cfs_pause(1000* HZ);
                }
        }
}

#define client_obd_list_lock(lock) \
        __client_obd_list_lock(lock, __FUNCTION__, __LINE__)

static inline void client_obd_list_unlock(client_obd_lock_t *lock)
{
        LASSERT(lock->task != NULL);
        lock->task = NULL;
        lock->time = jiffies;
        spin_unlock(&lock->lock);
}

#else /* ifdef CLIEBT_OBD_LIST_LOCK_DEBUG */
static inline void client_obd_list_lock(client_obd_lock_t *lock)
{
	spin_lock(&lock->lock);
}

static inline void client_obd_list_unlock(client_obd_lock_t *lock)
{
        spin_unlock(&lock->lock);
}

#endif /* ifdef CLIEBT_OBD_LIST_LOCK_DEBUG */

static inline void client_obd_list_lock_init(client_obd_lock_t *lock)
{
        spin_lock_init(&lock->lock);
}

static inline void client_obd_list_lock_done(client_obd_lock_t *lock)
{}


static inline int client_obd_list_is_locked(client_obd_lock_t *lock)
{
        return spin_is_locked(&lock->lock);
}

#if defined(__KERNEL__) && !defined(HAVE_ADLER)
/* zlib_adler() is an inline function defined in zutil.h */
#define HAVE_ADLER
#endif
#endif /* __LINUX_OBD_H */
