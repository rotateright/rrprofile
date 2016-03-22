/**
 * @file oprofilefs.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 *
 * A simple filesystem for configuration and
 * access of oprofile.
 */

#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#ifdef RRPROFILE
#include "../oprofile.h"
#else
#include <linux/oprofile.h>
#endif // RRPROFILE
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <asm/uaccess.h>

#include "oprof.h"

#define OPROFILEFS_MAGIC 0x6f70726f

DEFINE_SPINLOCK(oprofilefs_lock);

static struct inode *oprofilefs_get_inode(struct super_block *sb, int mode)
{
	struct inode *inode = new_inode(sb);

	if (inode) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
		inode->i_ino = get_next_ino();
#endif
		inode->i_mode = mode;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
		inode->i_uid = 0;
		inode->i_gid = 0;
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
		inode->i_blksize = PAGE_CACHE_SIZE;
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
		inode->i_blocks = 0;
#endif
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	}
	return inode;
}


static const struct super_operations s_ops = {
	.statfs		= simple_statfs,
	.drop_inode 	= generic_delete_inode,
};


ssize_t oprofilefs_str_to_user(char const *str, char __user *buf, size_t count, loff_t *offset)
{
	return simple_read_from_buffer(buf, count, offset, str, strlen(str));
}


#define TMPBUFSIZE 50

ssize_t oprofilefs_ulong_to_user(unsigned long val, char __user *buf, size_t count, loff_t *offset)
{
	char tmpbuf[TMPBUFSIZE];
	size_t maxlen = snprintf(tmpbuf, TMPBUFSIZE, "%lu\n", val);
	if (maxlen > TMPBUFSIZE)
		maxlen = TMPBUFSIZE;
	return simple_read_from_buffer(buf, count, offset, tmpbuf, maxlen);
}


int oprofilefs_ulong_from_user(unsigned long *val, char const __user *buf, size_t count)
{
	char tmpbuf[TMPBUFSIZE];
#ifndef RRPROFILE
	unsigned long flags;
#endif // RRPROFILE

	if (!count)
		return 0;

	if (count > TMPBUFSIZE - 1)
		return -EINVAL;

	memset(tmpbuf, 0x0, TMPBUFSIZE);

	if (copy_from_user(tmpbuf, buf, count))
		return -EFAULT;

#ifdef RRPROFILE
	spin_lock(&oprofilefs_lock);
#else
	spin_lock_irqsave(&oprofilefs_lock, flags);
#endif // RRPROFILE
	*val = simple_strtoul(tmpbuf, NULL, 0);
#ifdef RRPROFILE
	spin_unlock(&oprofilefs_lock);
#else
	spin_unlock_irqrestore(&oprofilefs_lock, flags);
#endif // RRPROFILE
	return 0;
}


static ssize_t ulong_read_file(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	unsigned long *val = file->private_data;
	return oprofilefs_ulong_to_user(*val, buf, count, offset);
}


static ssize_t ulong_write_file(struct file *file, char const __user *buf, size_t count, loff_t *offset)
{
	unsigned long value;
	int retval;

	if (*offset)
		return -EINVAL;

	retval = oprofilefs_ulong_from_user(&value, buf, count);
	if (retval)
		return retval;

	retval = oprofile_set_ulong(file->private_data, value);
	if (retval)
		return retval;
		
	return count;
}


static int default_open(struct inode *inode, struct file *filp)
{
#ifdef HAS_IPRIVATE
	if (inode->i_private)
		filp->private_data = inode->i_private;
#else
	if (inode->u.generic_ip)
		filp->private_data = inode->u.generic_ip;
#endif
	return 0;
}


static const struct file_operations ulong_fops = {
	.read		= ulong_read_file,
	.write		= ulong_write_file,
	.open		= default_open,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	.llseek		= default_llseek,
#endif
};


static const struct file_operations ulong_ro_fops = {
	.read		= ulong_read_file,
	.open		= default_open,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	.llseek		= default_llseek,
#endif
};


static int __oprofilefs_create_file(struct super_block *sb,
	struct dentry *root, char const *name, const struct file_operations *fops,
	int perm, void *priv)
{
	struct dentry *dentry;
	struct inode *inode;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
	dentry = d_alloc_name(root, name);
#else
	struct qstr qname;
	qname.name = name;
	qname.len = strlen(name);
	qname.hash = full_name_hash(qname.name, qname.len);
	dentry = d_alloc(root, &qname);
#endif
	if (!dentry)
		return -ENOMEM;
	inode = oprofilefs_get_inode(sb, S_IFREG | perm);
	if (!inode) {
		dput(dentry);
		return -ENOMEM;
	}
	inode->i_fop = fops;
	d_add(dentry, inode);
#ifdef HAS_IPRIVATE
	dentry->d_inode->i_private = priv;
#else
	dentry->d_inode->u.generic_ip = priv;
#endif
	return 0;
}


int oprofilefs_create_ulong(struct super_block *sb, struct dentry *root,
	char const *name, unsigned long *val)
{
	return __oprofilefs_create_file(sb, root, name,
					&ulong_fops, 0644, val);
}


int oprofilefs_create_ro_ulong(struct super_block *sb, struct dentry *root,
	char const *name, unsigned long *val)
{
	return __oprofilefs_create_file(sb, root, name,
					&ulong_ro_fops, 0444, val);
}

#ifdef RRPROFILE
int oprofilefs_create_tid_buffer_file(struct super_block * sb, struct dentry * root,
	char const * name, struct file_operations * fops, struct rrprofile_tid_buffer * tid_buf)
{
	return __oprofilefs_create_file(sb, root, name,
						     fops, 0666, tid_buf);
}
#endif // RRPROFILE


static ssize_t atomic_read_file(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
	atomic_t *val = file->private_data;
	return oprofilefs_ulong_to_user(atomic_read(val), buf, count, offset);
}


static const struct file_operations atomic_ro_fops = {
	.read		= atomic_read_file,
	.open		= default_open,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	.llseek		= default_llseek,
#endif // >= 2.6.37
};


int oprofilefs_create_ro_atomic(struct super_block *sb, struct dentry *root,
	char const *name, atomic_t *val)
{
	return __oprofilefs_create_file(sb, root, name,
					&atomic_ro_fops, 0444, val);
}


int oprofilefs_create_file(struct super_block *sb, struct dentry *root,
	char const *name, const struct file_operations *fops)
{
	return __oprofilefs_create_file(sb, root, name, fops, 0644, NULL);
}


int oprofilefs_create_file_perm(struct super_block *sb, struct dentry *root,
	char const *name, const struct file_operations *fops, int perm)
{
	return __oprofilefs_create_file(sb, root, name, fops, perm, NULL);
}


struct dentry *oprofilefs_mkdir(struct super_block *sb,
	struct dentry *root, char const *name)
{
	struct dentry *dentry;
	struct inode *inode;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
	dentry = d_alloc_name(root, name);
#else
	struct qstr qname;
	qname.name = name;
	qname.len = strlen(name);
	qname.hash = full_name_hash(qname.name, qname.len);
	dentry = d_alloc(root, &qname);
#endif
	if (!dentry)
		return NULL;
	inode = oprofilefs_get_inode(sb, S_IFDIR | 0755);
	if (!inode) {
		dput(dentry);
		return NULL;
	}
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	d_add(dentry, inode);
	return dentry;
}


static int oprofilefs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root_inode;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0)
	struct dentry *root_dentry;
#endif

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = OPROFILEFS_MAGIC;
	sb->s_op = &s_ops;
	sb->s_time_gran = 1;

	root_inode = oprofilefs_get_inode(sb, S_IFDIR | 0755);
	if (!root_inode)
		return -ENOMEM;
	root_inode->i_op = &simple_dir_inode_operations;
	root_inode->i_fop = &simple_dir_operations;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root)
		return -ENOMEM;

	oprofile_create_files(sb, sb->s_root);
#else
	root_dentry = d_alloc_root(root_inode);
	if (!root_dentry) {
		iput(root_inode);
		return -ENOMEM;
	}

	sb->s_root = root_dentry;

	oprofile_create_files(sb, root_dentry);
#endif // >= 3.4.0

	// FIXME: verify kill_litter_super removes our dentries
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
static struct dentry *oprofilefs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_single(fs_type, flags, data, oprofilefs_fill_super);
}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
static int oprofilefs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_single(fs_type, flags, data, oprofilefs_fill_super, mnt);
}
#else
static struct super_block *oprofilefs_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return get_sb_single(fs_type, flags, data, oprofilefs_fill_super);
}
#endif

static struct file_system_type oprofilefs_type = {
	.owner		= THIS_MODULE,
#ifdef RRPROFILE
	.name		= "rrprofilefs",
#else
	.name		= "oprofilefs",
#endif // RRPROFILE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37)
	.mount		= oprofilefs_mount,
#else
	.get_sb		= oprofilefs_get_sb,
#endif // >= 2.6.37
	.kill_sb	= kill_litter_super,
};


int __init oprofilefs_register(void)
{
	return register_filesystem(&oprofilefs_type);
}


void __exit oprofilefs_unregister(void)
{
	unregister_filesystem(&oprofilefs_type);
}
