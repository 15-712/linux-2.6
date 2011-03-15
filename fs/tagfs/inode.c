/*
 * Tag based file system 
 *
 * Copyright (C) 2011 Tim Shields 
 *             	 2011 Ping-Yao Tseng
 *             	 2011 William Wang
 *
 * This file is released under the GPL.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/tagfs.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#include "table.h"

#define TAGFS_DEFAULT_MODE	0755

static int tagfs_set_page_dirty(struct page *page);

static const struct super_operations tagfs_ops;
static const struct inode_operations tagfs_dir_inode_operations;

static const struct inode_operations tagfs_file_inode_operations = {
	.setattr	= simple_setattr,
	.getattr	= simple_getattr,
};

static struct address_space_operations tagfs_aops = {
	.readpage	= simple_readpage,
	.write_begin	= simple_write_begin,
	.write_end	= simple_write_end,
	.set_page_dirty = tagfs_set_page_dirty,
};

static const struct file_operations tagfs_file_operations = {
	.read		= do_sync_read,
	.aio_read	= generic_file_aio_read,
	.write		= do_sync_write,
	.aio_write	= generic_file_aio_write,
	.mmap		= generic_file_mmap,
	.fsync		= noop_fsync,
	.splice_read	= generic_file_splice_read,
	.splice_write	= generic_file_splice_write,
	.llseek		= generic_file_llseek,
};

static struct backing_dev_info tagfs_backing_dev_info = {
	.name		= "tagfs",
	.ra_pages	= 0, /* No readahead */
	.capabilities	= BDI_CAP_NO_ACCT_AND_WRITEBACK | 
				BDI_CAP_MAP_DIRECT | BDI_CAP_MAP_COPY |
				BDI_CAP_READ_MAP | BDI_CAP_WRITE_MAP | BDI_CAP_EXEC_MAP,
};

static int tagfs_set_page_dirty(struct page *page) 
{
	if (!PageDirty(page))
		return !TestSetPageDirty(page);
	return 0;
}

struct inode *tagfs_create_inode(struct super_block *sb,
					const struct inode *dir, int mode, dev_t dev)
{
	struct inode * inode = new_inode(sb);

	if (inode) {
		inode->i_ino = get_next_ino();
		inode_init_owner(inode, dir, mode);
		inode->i_mapping->a_ops = &tagfs_aops;
		inode->i_mapping->backing_dev_info = &tagfs_backing_dev_info;
		mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
		mapping_set_unevictable(inode->i_mapping);
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &tagfs_file_inode_operations;
			inode->i_fop = &tagfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &tagfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;
			inc_nlink(inode);
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			break;
		}
	}
	return inode;
}

static int tagfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	struct inode * inode = tagfs_create_inode(dir->i_sb, dir, mode, dev);
	int error = -ENOSPC;
	
	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);
		error = 0;
		dir->i_mtime = dir->i_ctime = CURRENT_TIME;
	}
	return error;
}

static int tagfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int retval = tagfs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int tagfs_create(struct inode *dir, struct dentry *dentry, int mode, struct nameidata *nd)
{
	return tagfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int tagfs_symlink(struct inode *dir, struct dentry *dentry, const char * symname)
{
	return -ENOSYS;
}


static const struct inode_operations tagfs_dir_inode_operations = {
	.create 	= tagfs_create,
	.lookup 	= simple_lookup,
	.link		= simple_link,
	.unlink 	= simple_unlink,
	.symlink	= tagfs_symlink,
	.mkdir		= tagfs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= tagfs_mknod,
	.rename		= simple_rename,
};

static const struct super_operations tagfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.show_options	= generic_show_options,
};

struct tagfs_mount_opts {
	umode_t mode;
};

enum {
	Opt_mode,
	Opt_err
};

static const match_table_t tokens = {
	{Opt_mode, "mode=%o"},
	{Opt_err, NULL}
};

struct tagfs_fs_info {
	struct tagfs_mount_opts mount_opts;
};

static int tagfs_parse_options(char *data, struct tagfs_mount_opts *opts)
{
	substring_t args[MAX_OPT_ARGS];
	int option;
	int token;
	char *p;

	opts->mode = TAGFS_DEFAULT_MODE;

	while ((p = strsep(&data, ",")) != NULL) {
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch(token) {
		case Opt_mode:
			if(match_octal(&args[0], &option))
				return -EINVAL;
			opts->mode = option & S_IALLUGO;
			break;
		}
	}
	return 0;
}

int tagfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct tagfs_fs_info *fsi;
	struct inode *inode = NULL;
	struct dentry *root;
	int err;

	save_mount_options(sb, data);

	fsi = kzalloc(sizeof(struct tagfs_fs_info), GFP_KERNEL);
	sb->s_fs_info = fsi;
	if(!fsi) {
		err = -ENOMEM;
		goto fail;
	}

	err = tagfs_parse_options(data, &fsi->mount_opts);
	if(err)
		goto fail;
	
	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= PAGE_CACHE_SIZE;
	sb->s_blocksize_bits	= PAGE_CACHE_SHIFT;
	sb->s_magic			= TAGFS_MAGIC;
	sb->s_op			= &tagfs_ops;
	sb->s_time_gran		= 1;

	inode = tagfs_create_inode(sb, NULL, S_IFDIR | fsi->mount_opts.mode, 0);
	if (!inode) {
		err = -ENOMEM;
		goto fail;
	}

	root = d_alloc_root(inode);
	sb->s_root = root;
	if (!root) {
		err = -ENOMEM;
		goto fail;
	}

	return 0;
fail:
	kfree(fsi);
	sb->s_fs_info = NULL;
	iput(inode);
	return err;
}


static void tagfs_kill_sb(struct super_block *sb)
{
	kfree(sb->s_fs_info);
	kill_litter_super(sb);
}

static struct dentry *tagfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, tagfs_fill_super);
}

static struct file_system_type tagfs_fs_type = {
	.name		= "tagfs",
	.mount		= tagfs_mount,
	.kill_sb 	= tagfs_kill_sb,
};

static int __init init_tagfs_fs(void)
{
	printk("Loading tagfs kernel module\n");
	return register_filesystem(&tagfs_fs_type);
}

static void __exit exit_tagfs_fs(void)
{
	printk("Unloading tagfs kernel module\n");
	unregister_filesystem(&tagfs_fs_type);
}

module_init(init_tagfs_fs)
module_exit(exit_tagfs_fs)

