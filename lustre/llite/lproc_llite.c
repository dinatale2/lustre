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
#define DEBUG_SUBSYSTEM S_LLITE

#include <linux/version.h>
#include <linux/lustre_lite.h>
#include <linux/lprocfs_status.h>
#include <linux/seq_file.h>
#include <linux/obd_support.h>

#include "llite_internal.h"

/* /proc/lustre/llite mount point registration */
struct proc_dir_entry *proc_lustre_fs_root;
struct file_operations llite_dump_pgcache_fops;

#ifndef LPROCFS
int lprocfs_register_mountpoint(struct proc_dir_entry *parent,
                                struct super_block *sb, char *osc, char *mdc)
{
        return 0;
}
void lprocfs_unregister_mountpoint(struct ll_sb_info *sbi){}
#else

long long mnt_instance;

static int ll_rd_blksize(char *page, char **start, off_t off, int count,
                         int *eof, void *data)
{
        struct super_block *sb = (struct super_block *)data;
        struct obd_statfs osfs;
        int rc;

        LASSERT(sb != NULL);
        rc = ll_statfs_internal(sb, &osfs, jiffies - HZ);
        if (!rc) {
              *eof = 1;
              rc = snprintf(page, count, "%u\n", osfs.os_bsize);
        }

        return rc;
}

static int ll_rd_kbytestotal(char *page, char **start, off_t off, int count,
                             int *eof, void *data)
{
        struct super_block *sb = (struct super_block *)data;
        struct obd_statfs osfs;
        int rc;

        LASSERT(sb != NULL);
        rc = ll_statfs_internal(sb, &osfs, jiffies - HZ);
        if (!rc) {
                __u32 blk_size = osfs.os_bsize >> 10;
                __u64 result = osfs.os_blocks;

                while (blk_size >>= 1)
                        result <<= 1;

                *eof = 1;
                rc = snprintf(page, count, LPU64"\n", result);
        }
        return rc;

}

static int ll_rd_kbytesfree(char *page, char **start, off_t off, int count,
                            int *eof, void *data)
{
        struct super_block *sb = (struct super_block *)data;
        struct obd_statfs osfs;
        int rc;

        LASSERT(sb != NULL);
        rc = ll_statfs_internal(sb, &osfs, jiffies - HZ);
        if (!rc) {
                __u32 blk_size = osfs.os_bsize >> 10;
                __u64 result = osfs.os_bfree;

                while (blk_size >>= 1)
                        result <<= 1;

                *eof = 1;
                rc = snprintf(page, count, LPU64"\n", result);
        }
        return rc;
}

static int ll_rd_kbytesavail(char *page, char **start, off_t off, int count,
                             int *eof, void *data)
{
        struct super_block *sb = (struct super_block *)data;
        struct obd_statfs osfs;
        int rc;

        LASSERT(sb != NULL);
        rc = ll_statfs_internal(sb, &osfs, jiffies - HZ);
        if (!rc) {
                __u32 blk_size = osfs.os_bsize >> 10;
                __u64 result = osfs.os_bavail;

                while (blk_size >>= 1)
                        result <<= 1;

                *eof = 1;
                rc = snprintf(page, count, LPU64"\n", result);
        }
        return rc;
}

static int ll_rd_filestotal(char *page, char **start, off_t off, int count,
                            int *eof, void *data)
{
        struct super_block *sb = (struct super_block *)data;
        struct obd_statfs osfs;
        int rc;

        LASSERT(sb != NULL);
        rc = ll_statfs_internal(sb, &osfs, jiffies - HZ);
        if (!rc) {
                 *eof = 1;
                 rc = snprintf(page, count, LPU64"\n", osfs.os_files);
        }
        return rc;
}

static int ll_rd_filesfree(char *page, char **start, off_t off, int count,
                           int *eof, void *data)
{
        struct super_block *sb = (struct super_block *)data;
        struct obd_statfs osfs;
        int rc;

        LASSERT(sb != NULL);
        rc = ll_statfs_internal(sb, &osfs, jiffies - HZ);
        if (!rc) {
                 *eof = 1;
                 rc = snprintf(page, count, LPU64"\n", osfs.os_ffree);
        }
        return rc;

}

static int ll_rd_fstype(char *page, char **start, off_t off, int count,
                        int *eof, void *data)
{
        struct super_block *sb = (struct super_block*)data;

        LASSERT(sb != NULL);
        *eof = 1;
        return snprintf(page, count, "%s\n", sb->s_type->name);
}

static int ll_rd_sb_uuid(char *page, char **start, off_t off, int count,
                         int *eof, void *data)
{
        struct super_block *sb = (struct super_block *)data;

        LASSERT(sb != NULL);
        *eof = 1;
        return snprintf(page, count, "%s\n", ll_s2sbi(sb)->ll_sb_uuid.uuid);
}

static int ll_rd_read_ahead(char *page, char **start, off_t off, int count,
                            int *eof, void *data)
{
        struct super_block *sb = (struct super_block*)data;
        struct ll_sb_info *sbi = ll_s2sbi(sb);
        int val, rc;
        ENTRY;

        *eof = 1;
        val = (sbi->ll_flags & LL_SBI_READAHEAD) ? 1 : 0;
        rc = snprintf(page, count, "%d\n", val);
        RETURN(rc);
}

static int ll_wr_read_ahead(struct file *file, const char *buffer,
                            unsigned long count, void *data)
{
        struct super_block *sb = (struct super_block*)data;
        struct ll_sb_info *sbi = ll_s2sbi(sb);
        int readahead;
        ENTRY;

        if (1 != sscanf(buffer, "%d", &readahead))
                RETURN(-EINVAL);

        if (readahead)
                sbi->ll_flags |= LL_SBI_READAHEAD;
        else
                sbi->ll_flags &= ~LL_SBI_READAHEAD;

        RETURN(count);
}

static int ll_wr_config_update(struct file *file, const char *buffer,
                               unsigned long count, void *data)
{
        struct super_block *sb = (struct super_block*)data;
        struct ll_sb_info *sbi = ll_s2sbi(sb);
        ENTRY;

        CWARN("Starting a LOV/OST update !\n");
        RETURN(ll_process_config_update(sbi, 0));
}

static int ll_rd_max_read_ahead_mb(char *page, char **start, off_t off,
                                   int count, int *eof, void *data)
{
        struct super_block *sb = data;
        struct ll_sb_info *sbi = ll_s2sbi(sb);
        unsigned val;

        spin_lock(&sbi->ll_lock);
        val = (sbi->ll_max_read_ahead_pages << PAGE_CACHE_SHIFT) >> 20;
        spin_unlock(&sbi->ll_lock);

        return snprintf(page, count, "%u\n", val);
}

static int ll_wr_max_read_ahead_mb(struct file *file, const char *buffer,
                                   unsigned long count, void *data)
{
        struct super_block *sb = data;
        struct ll_sb_info *sbi = ll_s2sbi(sb);
        int val, rc;

        rc = lprocfs_write_helper(buffer, count, &val);
        if (rc)
                return rc;

        if (val < 0 || val > (num_physpages << PAGE_SHIFT) >> 20)
                return -ERANGE;

        spin_lock(&sbi->ll_lock);
        sbi->ll_max_read_ahead_pages = (val << 20) >> PAGE_CACHE_SHIFT;
        spin_unlock(&sbi->ll_lock);

        return count;
}

static struct lprocfs_vars lprocfs_obd_vars[] = {
        { "uuid",         ll_rd_sb_uuid,          0, 0 },
        //{ "mntpt_path",   ll_rd_path,             0, 0 },
        { "fstype",       ll_rd_fstype,           0, 0 },
        { "blocksize",    ll_rd_blksize,          0, 0 },
        { "kbytestotal",  ll_rd_kbytestotal,      0, 0 },
        { "kbytesfree",   ll_rd_kbytesfree,       0, 0 },
        { "kbytesavail",  ll_rd_kbytesavail,      0, 0 },
        { "filestotal",   ll_rd_filestotal,       0, 0 },
        { "filesfree",    ll_rd_filesfree,        0, 0 },
        //{ "filegroups",   lprocfs_rd_filegroups,  0, 0 },
        { "read_ahead",   ll_rd_read_ahead, ll_wr_read_ahead, 0 },
        { "config_update", 0, ll_wr_config_update, 0 },
        { "max_read_ahead_mb", ll_rd_max_read_ahead_mb,
                               ll_wr_max_read_ahead_mb, 0 },
        { 0 }
};

#define MAX_STRING_SIZE 128

struct llite_file_opcode {
        __u32       opcode;
        __u32       type;
        const char *opname;
} llite_opcode_table[LPROC_LL_FILE_OPCODES] = {
        /* file operation */
        { LPROC_LL_DIRTY_HITS,     LPROCFS_TYPE_REGS, "dirty_pages_hits" },
        { LPROC_LL_DIRTY_MISSES,   LPROCFS_TYPE_REGS, "dirty_pages_misses" },
        { LPROC_LL_WB_WRITEPAGE,   LPROCFS_CNTR_AVGMINMAX|LPROCFS_TYPE_PAGES,
                                   "writeback_from_writepage" },
        { LPROC_LL_WB_PRESSURE,    LPROCFS_CNTR_AVGMINMAX|LPROCFS_TYPE_PAGES,
                                   "writeback_from_pressure" },
        { LPROC_LL_WB_OK,          LPROCFS_CNTR_AVGMINMAX|LPROCFS_TYPE_PAGES,
                                   "writeback_ok_pages" },
        { LPROC_LL_WB_FAIL,        LPROCFS_CNTR_AVGMINMAX|LPROCFS_TYPE_PAGES,
                                   "writeback_failed_pages" },
        { LPROC_LL_READ_BYTES,     LPROCFS_CNTR_AVGMINMAX|LPROCFS_TYPE_BYTES,
                                   "read_bytes" },
        { LPROC_LL_WRITE_BYTES,    LPROCFS_CNTR_AVGMINMAX|LPROCFS_TYPE_BYTES,
                                   "write_bytes" },
        { LPROC_LL_BRW_READ,       LPROCFS_CNTR_AVGMINMAX|LPROCFS_TYPE_PAGES,
                                   "brw_read" },
        { LPROC_LL_BRW_WRITE,      LPROCFS_CNTR_AVGMINMAX|LPROCFS_TYPE_PAGES,
                                   "brw_write" },

        { LPROC_LL_IOCTL,          LPROCFS_TYPE_REGS, "ioctl" },
        { LPROC_LL_OPEN,           LPROCFS_TYPE_REGS, "open" },
        { LPROC_LL_RELEASE,        LPROCFS_TYPE_REGS, "close" },
        { LPROC_LL_MAP,            LPROCFS_TYPE_REGS, "mmap" },
        { LPROC_LL_LLSEEK,         LPROCFS_TYPE_REGS, "seek" },
        { LPROC_LL_FSYNC,          LPROCFS_TYPE_REGS, "fsync" },
        /* inode operation */
        { LPROC_LL_SETATTR,        LPROCFS_TYPE_REGS, "setattr" },
        { LPROC_LL_TRUNC,          LPROCFS_TYPE_REGS, "punch" },
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0))
        { LPROC_LL_GETATTR,        LPROCFS_TYPE_REGS, "getattr" },
#else
        { LPROC_LL_REVALIDATE,     LPROCFS_TYPE_REGS, "getattr" },
#endif
        /* special inode operation */
        { LPROC_LL_STAFS,          LPROCFS_TYPE_REGS, "statfs" },
        { LPROC_LL_ALLOC_INODE,    LPROCFS_TYPE_REGS, "alloc_inode" },
        { LPROC_LL_DIRECT_READ,    LPROCFS_CNTR_AVGMINMAX|LPROCFS_TYPE_PAGES,
                                   "direct_read" },
        { LPROC_LL_DIRECT_WRITE,   LPROCFS_CNTR_AVGMINMAX|LPROCFS_TYPE_PAGES,
                                   "direct_write" },

};

int lprocfs_register_mountpoint(struct proc_dir_entry *parent,
                                struct super_block *sb, char *osc, char *mdc)
{
        struct lprocfs_vars lvars[2];
        struct ll_sb_info *sbi = ll_s2sbi(sb);
        struct obd_device *obd;
        char name[MAX_STRING_SIZE + 1];
        int err, id;
        struct lprocfs_stats *svc_stats = NULL;
        struct proc_dir_entry *mdc_symlink, *osc_symlink;
        struct proc_dir_entry *entry;
        ENTRY;

        memset(lvars, 0, sizeof(lvars));

        name[MAX_STRING_SIZE] = '\0';
        lvars[0].name = name;

        LASSERT(sbi != NULL);
        LASSERT(mdc != NULL);
        LASSERT(osc != NULL);

        /* Mount info */
        snprintf(name, MAX_STRING_SIZE, "fs%llu", mnt_instance);

        mnt_instance++;
        sbi->ll_proc_root = lprocfs_register(name, parent, NULL, NULL);
        if (IS_ERR(sbi->ll_proc_root)) {
                err = PTR_ERR(sbi->ll_proc_root);
                sbi->ll_proc_root = NULL;
                RETURN(err);
        }

        entry = create_proc_entry("dump_page_cache", 0444, sbi->ll_proc_root);
        if (entry == NULL)
                GOTO(out, err = -ENOMEM);
        entry->proc_fops = &llite_dump_pgcache_fops;
        entry->data = sbi;

        svc_stats = lprocfs_alloc_stats(LPROC_LL_FILE_OPCODES);
        if (svc_stats == NULL) {
                err = -ENOMEM;
                goto out;
        }
        /* do counter init */
        for (id = 0; id < LPROC_LL_FILE_OPCODES; id++) {
                __u32 type = llite_opcode_table[id].type;
                void *ptr = NULL;
                if (type & LPROCFS_TYPE_REGS)
                        ptr = "regs";
                else {
                        if (type & LPROCFS_TYPE_BYTES)
                                ptr = "bytes";
                        else {
                                if (type & LPROCFS_TYPE_PAGES)
                                        ptr = "pages";
                        }
                }
                lprocfs_counter_init(svc_stats, llite_opcode_table[id].opcode,
                                     (type & LPROCFS_CNTR_AVGMINMAX),
                                     llite_opcode_table[id].opname, ptr);
        }
        err = lprocfs_register_stats(sbi->ll_proc_root, "stats", svc_stats);
        if (err)
                goto out;
        else
                sbi->ll_stats = svc_stats;
        /* need place to keep svc_stats */

        /* Static configuration info */
        err = lprocfs_add_vars(sbi->ll_proc_root, lprocfs_obd_vars, sb);
        if (err)
                goto out;

        /* MDC info */
        obd = class_name2obd(mdc);

        LASSERT(obd != NULL);
        LASSERT(obd->obd_type != NULL);
        LASSERT(obd->obd_type->typ_name != NULL);

        snprintf(name, MAX_STRING_SIZE, "../../%s/%s",
                 obd->obd_type->typ_name, obd->obd_name);
        mdc_symlink = proc_symlink(obd->obd_type->typ_name, sbi->ll_proc_root,
                                   name);
        if (mdc_symlink == NULL) {
                err = -ENOMEM;
                goto out;
        }

        /* OSC */
        obd = class_name2obd(osc);

        LASSERT(obd != NULL);
        LASSERT(obd->obd_type != NULL);
        LASSERT(obd->obd_type->typ_name != NULL);

       snprintf(name, MAX_STRING_SIZE, "../../%s/%s",
                obd->obd_type->typ_name, obd->obd_name);
       osc_symlink = proc_symlink(obd->obd_type->typ_name, sbi->ll_proc_root,
                                  name);
       if (osc_symlink == NULL)
               err = -ENOMEM;


out:
        if (err) {
                if (svc_stats)
                        lprocfs_free_stats(svc_stats);
                if (sbi->ll_proc_root)
                        lprocfs_remove(sbi->ll_proc_root);
        }
        RETURN(err);
}

void lprocfs_unregister_mountpoint(struct ll_sb_info *sbi)
{
        if (sbi->ll_proc_root) {
                struct proc_dir_entry *file_stats =
                        lprocfs_srch(sbi->ll_proc_root, "stats");

                if (file_stats) {
                        lprocfs_free_stats(sbi->ll_stats);
                        lprocfs_remove(file_stats);
                }
        }
}
#undef MAX_STRING_SIZE

static struct ll_async_page *llite_pglist_next_llap(struct ll_sb_info *sbi,
                                                    struct list_head *list)
{
        struct ll_async_page *llap;
        struct list_head *pos;

        list_for_each(pos, list) {
                if (pos == &sbi->ll_pglist)
                        return NULL;
                llap = list_entry(pos, struct ll_async_page, llap_proc_item);
                if (llap->llap_page == NULL)
                        continue;
                return llap;
        }
        LBUG();
        return NULL;
}

#define seq_page_flag(seq, page, flag, has_flags) do {                  \
                if (test_bit(PG_##flag, &(page)->flags)) {              \
                        if (!has_flags)                                 \
                                has_flags = 1;                          \
                        else                                            \
                                seq_putc(seq, '|');                     \
                        seq_puts(seq, #flag);                           \
                }                                                       \
        } while(0);

static int llite_dump_pgcache_seq_show(struct seq_file *seq, void *v)
{
        struct ll_async_page *llap, *dummy_llap = seq->private;
        struct ll_sb_info *sbi = dummy_llap->llap_cookie;

        /* 2.4 doesn't seem to have SEQ_START_TOKEN, so we implement
         * it in our own state */
        if (dummy_llap->llap_magic == 0) {
                seq_printf(seq, "generation | llap .cookie | page ");
                seq_printf(seq, "inode .index [ page flags ]\n");
                return 0;
        }

        spin_lock(&sbi->ll_lock);

        llap = llite_pglist_next_llap(sbi, &dummy_llap->llap_proc_item);
        if (llap != NULL)  {
                int has_flags = 0;
                struct page *page = llap->llap_page;

                seq_printf(seq, "%lu | %p %p | %p %p %lu [",
                                sbi->ll_pglist_gen,
                                llap, llap->llap_cookie,
                                page, page->mapping->host, page->index);
                seq_page_flag(seq, page, locked, has_flags);
                seq_page_flag(seq, page, error, has_flags);
                seq_page_flag(seq, page, referenced, has_flags);
                seq_page_flag(seq, page, uptodate, has_flags);
                seq_page_flag(seq, page, dirty, has_flags);
                seq_page_flag(seq, page, highmem, has_flags);
                if (!has_flags)
                        seq_puts(seq, "-]\n");
                else
                        seq_puts(seq, "]\n");
        }

        spin_unlock(&sbi->ll_lock);

        return 0;
}

static void *llite_dump_pgcache_seq_start(struct seq_file *seq, loff_t *pos)
{
        struct ll_async_page *llap = seq->private;

        if (llap->llap_magic == 2)
                return NULL;

        return (void *)1;
}

static void *llite_dump_pgcache_seq_next(struct seq_file *seq, void *v,
                                         loff_t *pos)
{
        struct ll_async_page *llap, *dummy_llap = seq->private;
        struct ll_sb_info *sbi = dummy_llap->llap_cookie;

        /* bail if we just displayed the banner */
        if (dummy_llap->llap_magic == 0) {
                dummy_llap->llap_magic = 1;
                return dummy_llap;
        }

        /* we've just displayed the llap that is after us in the list.
         * we advance to a position beyond it, returning null if there
         * isn't another llap in the list beyond that new position. */
        spin_lock(&sbi->ll_lock);
        llap = llite_pglist_next_llap(sbi, &dummy_llap->llap_proc_item);
        list_del_init(&dummy_llap->llap_proc_item);
        if (llap) {
                list_add(&dummy_llap->llap_proc_item, &llap->llap_proc_item);
                llap = llite_pglist_next_llap(sbi, &dummy_llap->llap_proc_item);
        }
        spin_unlock(&sbi->ll_lock);

        ++*pos;
        if (llap == NULL) {
                dummy_llap->llap_magic = 2;
                return NULL;
        }
        return dummy_llap;
}

static void llite_dump_pgcache_seq_stop(struct seq_file *seq, void *v)
{
}

struct seq_operations llite_dump_pgcache_seq_sops = {
        .start = llite_dump_pgcache_seq_start,
        .stop = llite_dump_pgcache_seq_stop,
        .next = llite_dump_pgcache_seq_next,
        .show = llite_dump_pgcache_seq_show,
};

/* we're displaying llaps in a list_head list.  we don't want to hold a lock
 * while we walk the entire list, and we don't want to have to seek into
 * the right position in the list as an app advances with many syscalls.  we
 * allocate a dummy llap and hang it off file->private.  its position in
 * the list records where the app is currently displaying.  this way our
 * seq .start and .stop don't actually do anything.  .next returns null
 * when the dummy hits the end of the list which eventually leads to .release
 * where we tear down.  this kind of displaying is super-racey, so we put
 * a generation counter on the list so the output shows when the list
 * changes between reads.
 */
static int llite_dump_pgcache_seq_open(struct inode *inode, struct file *file)
{
        struct proc_dir_entry *dp = PDE(inode);
        struct ll_async_page *llap;
        struct seq_file *seq;
        struct ll_sb_info *sbi = dp->data;
        int rc;

        OBD_ALLOC_GFP(llap, sizeof(*llap), GFP_KERNEL);
        if (llap == NULL)
                return -ENOMEM;
        llap->llap_page = NULL;
        llap->llap_cookie = sbi;
        llap->llap_magic = 0;

        rc = seq_open(file, &llite_dump_pgcache_seq_sops);
        if (rc) {
                OBD_FREE(llap, sizeof(*llap));
                return rc;
        }
        seq = file->private_data;
        seq->private = llap;

        spin_lock(&sbi->ll_lock);
        list_add(&llap->llap_proc_item, &sbi->ll_pglist);
        spin_unlock(&sbi->ll_lock);

        return 0;
}

static int llite_dump_pgcache_seq_release(struct inode *inode,
                                          struct file *file)
{
        struct seq_file *seq = file->private_data;
        struct ll_async_page *llap = seq->private;
        struct ll_sb_info *sbi = llap->llap_cookie;

        spin_lock(&sbi->ll_lock);
        if (!list_empty(&llap->llap_proc_item))
                list_del_init(&llap->llap_proc_item);
        spin_unlock(&sbi->ll_lock);
        OBD_FREE(llap, sizeof(*llap));

        return seq_release(inode, file);
}

struct file_operations llite_dump_pgcache_fops = {
        .owner   = THIS_MODULE,
        .open    = llite_dump_pgcache_seq_open,
        .read    = seq_read,
        .release = llite_dump_pgcache_seq_release,
};

#endif /* LPROCFS */
