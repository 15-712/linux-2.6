#ifndef _LINUX_TAGFS_H
#define _LINUX_TAGFS_H

extern const struct file_operations tagfs_file_operations;
extern const struct vm_operations_struct generic_file_vm_ops;
extern int __init init_rootfs(void);

int tagfs_fill_super(struct super_block *sb, void *data, int silent);

#endif
