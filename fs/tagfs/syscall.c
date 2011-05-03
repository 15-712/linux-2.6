#include <linux/fs.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/err.h>
#include <asm/segment.h>
#include <asm-generic/fcntl.h>
#include <asm-generic/errno-base.h>
#include <linux/buffer_head.h>
#include <linux/file.h>
#include <linux/fsnotify.h>
#include <asm-generic/bug.h>
#include <linux/module.h>

#include "syscall.h"
#include "table.h"
#include "block.h"

char cwt[MAX_TAGEX_LEN+1];
//struct expr_tree *tree = NULL;
struct hash_table *table;

static char inv[] = {'.', '&', '|', '/'};

struct userspace_inode_entry {
	unsigned long ino;
	char filename[MAX_FILENAME_LEN+1];
	unsigned int count;
};

int (*prev_opentag)(const char __user *, int);
int (*prev_addtag)(const char __user *, const char __user **, unsigned int);
int (*prev_rmtag)(const char __user *, const char __user **, unsigned int);
int (*prev_chtag)(const char __user *);
int (*prev_mvtag)(const char __user *, const char __user *);
int (*prev_getcwt)(char __user *, unsigned long size);
int (*prev_lstag)(const char __user *, void __user *, unsigned long, int);
int (*prev_distag)(unsigned long, char __user **, unsigned long, unsigned long); 

void install_syscalls(void) {
	printk("Installing tag syscalls\n");
	prev_opentag = opentag_ptr;
	prev_addtag = addtag_ptr;
	prev_rmtag = rmtag_ptr;
	prev_chtag = chtag_ptr;
	prev_mvtag = mvtag_ptr;
	prev_getcwt = getcwt_ptr;
	prev_lstag = lstag_ptr;
	prev_distag = distag_ptr;
	opentag_ptr = opentag;
	addtag_ptr = addtag;
	rmtag_ptr = rmtag;
	chtag_ptr = chtag;
	mvtag_ptr = mvtag;
	getcwt_ptr = getcwt;
	lstag_ptr = lstag;
	distag_ptr = distag;
}

void uninstall_syscalls(void) {
	opentag_ptr = prev_opentag;
	addtag_ptr = prev_addtag;
	rmtag_ptr = prev_rmtag;
	chtag_ptr = prev_chtag;
	mvtag_ptr = prev_mvtag;
	getcwt_ptr = prev_getcwt;
	lstag_ptr = prev_lstag;
	distag_ptr = prev_distag;
}

static long do_sys_opentag(const char __user *tagexp, int flags)
{
	//printk("@do_sys_opentag\n");
        char *tmp = getname(tagexp);
        int fd = PTR_ERR(tmp);
	struct expr_tree *e;
	struct table_element *t;
	struct inode_entry **inode_array;
	unsigned long ino;
	int size, i;
	int num_tags = 0;

	//struct file_system_type *file_system = get_fs_type("tagfs");
        //struct list_head *list = file_system->fs_supers.next;
        //struct super_block *super_block = list_entry(list, struct super_block, s_instances);
        //put_filesystem(file_system);
	//module_put(file_system->owner);
	//int before = super_block->s_root->d_count;
	//printk("before=%d\n", before);
	/*if (before != 0) {
		printk("before=%d\n", before);
		BUG();
	}*/

        // checks flags
        if ((flags != O_RDONLY) && (flags != O_WRONLY) && (flags != O_RDWR))
                return -EINVAL;

        // gets inode number
        e = build_tree(tagexp);
        if (e == NULL)
                return -EINVAL;
        t = parse_tree(e, table);
        if (t == NULL)
                return -EINVAL;
	size = element_size(t);
	inode_array = set_to_array(t);
	//printk("Found %d possible inodes.\n", size);
	if (size == 1) {
		// Use this inode regardless of if it is fully specified or not
		ino = inode_array[0]->ino;
	} else if (size > 1) { // We found more than 1 file
		// See if one of the files is fully specified by the given tags
		i = 0;

		do {
			get_tagids(inode_array[i]->ino, &num_tags);
			//printk("num_tags [%d] = %d\n", i, num_tags);
			i++;
		} while(i < size && num_tags != e->num_ops + 1);


		if(i == size)
			return -EMFILE; // We can't determine which file to open

		ino = inode_array[i-1]->ino;
		//printk("opening inode #%ld\n", ino);
	} else {
		return -EMFILE; // No files found
	}

	//printk(KERN_ALERT "ino=%lu\n", ino);

        if ((!IS_ERR(tmp)) || (ino > 0)) {
                fd = get_unused_fd_flags(flags);
                if (fd >= 0) {
                        struct file *f = do_filp_opentag(ino, flags, 0);
                        if (IS_ERR(f)) {
				printk("fd error\n");
                                put_unused_fd(fd);
                                fd = PTR_ERR(f);
                        } else {
				printk("fd install\n");
                                fsnotify_open(f);
                                fd_install(fd, f);
                        }
                }
                putname(tmp);
        }
	printk("return from do_sys_open_tag: fd=%d\n", fd);
	//int after = super_block->s_root->d_count;
	//printk("after=%d\n", after);
	//if (after != before) {
		//super_block->s_root->d_count--;
		//BUG();
	//}
        return fd;
}

// @flags: O_RDONLY, O_WDONLY, O_RDWR
int opentag(const char __user *tagexp, int flags) {
        long ret;

	//printk("opentag system call\n");
	//printk("@opentag\n");
        //if (force_o_largefile())
                //flags |= O_LARGEFILE;

        ret = do_sys_opentag(tagexp, flags);

        /* avoid REGPARM breakage on x86: */
        asmlinkage_protect(2, ret, tagexp, flags);

        return ret;
}

int add_single_tag(unsigned long ino, const char *tag, char *name) {
	struct table_element *curr, *check, *e;
	struct inode_entry *ent;
	int len, ret, i, j, num_tags;
	int *tag_ids = NULL;
	//printk("add_single_tag '%s' to inode %lu\n", tag, ino);	
	len = strlen(tag);
	//printk("checking for invalid characters\n");
	for (i = 0; i < len; i++) {
		for (j = 0; j < sizeof(inv) / sizeof(char); j++) {
			if (tag[i] == inv[j]) {
				printk("Invalid tag character: %c\n", inv[j]);
				return -EINVAL;
			}
		}
	}
	tag_ids = get_tagids(ino, &num_tags);
	//TODO: tag_ids <- Get tags from inode
	curr = get_inodes(table, tag);
	//Easy case
	if (!curr) {
		//printk("creating new tag\n");
		/* TODO: Insert new tag into inode 
		 *       if first tag, need to allocate block
		 *       if block allocation fails, return failure condition
		 */
		if (num_tags) {
			//printk("looking up inode_entry\n");
			e = get_inodes(table, get_tag(table, tag_ids[0]));
			ent = find_entry(e, ino);
		} else {
			//printk("creating inode_entry\n");
			ent = kmalloc(sizeof(struct inode_entry), GFP_KERNEL);
			if (!ent) {
				//TODO: Clean up
				return -ENOMEM;
			}
			ent->ino = ino;
			strncpy(ent->filename, name, MAX_FILENAME_LEN);
			ent->count = 0;
		}
		//printk("Inserting tag into table\n");
		ret = table_insert(table, tag, ent);
		if (ret) {
			/*TODO: Clean up, may not be memory error, need 
			  to check return value*/
			return -ENOMEM;
		}
		//TODO: Make persistent
		if (num_tags < 1 && allocate_block(ino)) {
			table_remove(table, tag, ino);
			return -ENOMEM;
		}
		//printk("Adding tag id\n");
		add_tagid(ino, get_tagid(table, tag));
		return 0;
	}
	check = NULL;
	if (num_tags > 0)
		check = get_inodes(table, get_tag(table, tag_ids[0]));
	/* TODO: Insert new tag into inode 
	 *       if first tag, need to allocate block
	 *       if block allocationg fails, return failure condition
	 */
	if (!check) {
		ent = kmalloc(sizeof(struct inode_entry), GFP_KERNEL);
		if (!ent) {
			//TODO: Clean up
			return -ENOMEM;
		}
		ent->ino = ino;
		strncpy(ent->filename, name, MAX_FILENAME_LEN);
		ent->count = 0;
	} else {
		ent = find_entry(check, ino);
	}
	ret = table_insert(table, tag, ent);
	if (ret) {
		/*TODO: Clean up, may not be memory error, need 
		  to check return value*/
		return -ENOMEM;
	}
	//TODO: make persistent
	if (num_tags < 1 && allocate_block(ino)) {
		table_remove(table, tag, ino);
		return -ENOMEM;
	}
	add_tagid(ino, get_tagid(table, tag));
	return 0;
}

int addtag(const char __user *filename, const char __user **tag, unsigned int size) {
	char *file, *t, *name;
	int *tag_ids = NULL;
	unsigned long ino = 0;
	int i, j, ret = 0, num_tags = 0, len, duplicate = 0;

	//printk("addtag system call\n");
	file = getname(filename);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto fail_file;
	}

	len = strlen(file);
	i = len - 1;
	//printk("making sure file is not a directory\n");
	while(i >= 0) {
		if (file[i] == '/')
			break;
		i--;
	}
	if (i == len - 1) {
		printk("Cannot tag a directory.\n");
		ret = -EINVAL;
		goto fail;
	}
	name = file + i + 1;

	ino = ino_by_name(filename);
	if(ino < 0) {
		printk("Invalid inode for addtag\n");
		ret = ino;
		goto fail;
	}

	tag_ids = get_tagids(ino, &num_tags);

	/* Check size */
	if (num_tags > MAX_NUM_TAGS + size) {
		printk("File has too many tags.\n");
		ret = -EINVAL;
		goto fail;
	}

	/* add tags */
	for(i = 0; i < size; i++) {
		duplicate = 0;
		t = getname(tag[i]);
		if (IS_ERR(t)) {
			ret = PTR_ERR(t);
			goto fail_tag;
		}
		for (j = 0; j < num_tags; j++) {
			if (strncmp(t, get_tag(table, tag_ids[j]), MAX_TAG_LEN) == 0) {
				printk("File already has tag %s.\n", t);
				duplicate = 1;
				// Clear this tag so we don't clean it up on error
				t[0] = '\0';
				break;
			}
		}
		if(duplicate == 0) {
			ret = add_single_tag(ino, t, name);
			tag_ids = get_tagids(ino, &num_tags);
		}
		putname(t);
		if(ret) 
			goto fail_tag;
	}

	putname(file);


        //after = super_block->s_root->d_count;
	//printk("after2=%d\n", after);
	return 0;

	//printk("Finished addtag\n");
fail_tag:
	printk("Failed to add tags\n");
	/* undo added tags */
	for(j = i-1; j >= 0; j--) {
		rmtag(filename, &tag[j], 1);
	}

fail:
	putname(file);
fail_file:
        //after = super_block->s_root->d_count;
	//printk("after3=%d\n", after);
	return ret;
}

int rm_single_tag(unsigned long ino, const char *tag) {
	struct table_element *curr;
	int num_tags = 0;
	int *tag_ids = NULL;

	curr = get_inodes(table, tag);
	//Easy case, tag doesn't exist;
	if (!curr)
		return 0;
		
	tag_ids = get_tagids(ino, &num_tags);
	remove_tagid(ino, get_tagid(table, tag));
	if (num_tags == 1)
		deallocate_block(ino);
	table_remove(table, tag, ino);
	return 0;
}

int rmtag(const char __user *filename, const char __user **tag, unsigned int size) {
	char *file, *t;
	unsigned long ino = 0;
	int i, ret = 0, len;

	printk("rmtag system call\n");

	file = getname(filename);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto end;
	}
	len = strlen(file);
	i = len - 1;
	//printk("making sure file is not a directory\n");
	while(i >= 0) {
		if (file[i] == '/')
			break;
		i--;
	}
	if (i == len - 1) {
		printk("Cannot rmtag a directory.\n");
		ret = -EINVAL;
		goto clean_up;
	}
	ino = ino_by_name(filename);
	if (!ino) {
		ret = -ENOMEM;
		goto clean_up;
	}

	for(i = 0; i < size; i++) {
		t = getname(tag[i]);
		if (IS_ERR(t)) {
			ret = PTR_ERR(t);
			goto clean_up;
		}
		rm_single_tag(ino, t);
		if(ret)
			goto clean_up;
		putname(t);
	}

clean_up:
	putname(file);
end:
	return ret;
}

int chtag(const char __user *tagex) {
	char *ktagex = getname(tagex);
	//struct expr_tree *new_tree;
	int len, ret = 0;
	//printk("chtag system call\n");
	if (IS_ERR(ktagex)) {
		ret = PTR_ERR(ktagex);
		goto end;
	}
	len = strlen(ktagex);
	if (strlen(ktagex) > MAX_TAGEX_LEN) {
		ret = -EINVAL;
		goto clean_up;
	}
	if (len == 0) {
		cwt[0] = '\0';
		//free_tree(tree);
		//tree = NULL;
		goto clean_up;
	}

	/*if (!(new_tree = build_tree(tagex))) {
		TODO: need some way to check if the expression
		 *       has an error or a memory error occurred
		
		ret = -EINVAL;
		goto clean_up;
	}*/
	strcpy(cwt, ktagex);
	/*if (tree)
		free_tree(tree);
	tree = new_tree;*/
clean_up:
	putname(ktagex);
end:
	return ret;
}

int mvtag(const char __user *tag1, const char __user *tag2) {
	char *kt1, *kt2;
	int ret;
	//printk("mvtag system call\n");
	kt1= getname(tag1);
	if (IS_ERR(kt1))
		return -ENOMEM;
	kt2 = getname(tag2);
	if (IS_ERR(kt2)) {
		putname(kt1);
		return -ENOMEM;
	}
	ret = change_tag(table, kt1, kt2);
	putname(kt2);
	putname(kt1);
	return ret;
}

int getcwt(char __user *buf, unsigned long size) {
	/* Why does getcwd (fs/dcache.c:2767) seem so complicated? */
	int error;
	unsigned long len;
	//printk("getcwt system call\n");
	error = -ERANGE;
	len = strlen(cwt);
	if (len <= size) {
		error = len;
		if(copy_to_user(buf, cwt, len))
			error = -EFAULT;
	}

	return error;
}

int lstag(const char __user *expr, void __user *buf, unsigned long size, int offset) {
	struct expr_tree *tree;
	struct table_element *results;
	struct inode_entry **inodes;
	char *kexpr = getname(expr);
	char *full_expr = NULL;
	int i;
	unsigned int len;
	int error;
	
	//printk("lstag system call\n");
	error = -ENOMEM;
	if (IS_ERR(kexpr)) {
		error = PTR_ERR(kexpr);
		printk("IS_ERR(kexpr)\n");
		goto end2;
	}
	/* If expr starts with a '.' then prepend cwt to expr */
	len = strlen(kexpr);
	if(len == 0) {
		//printk("full_expr = cwt\n");
		full_expr = cwt;
	} else if(kexpr[0] == '.') {
		//printk("full_expr = cwt & input\n");
		len += strlen(cwt);
		full_expr = kmalloc(sizeof(char) * len, GFP_KERNEL);
		if(!full_expr)
			goto end;
		len = strlcpy(full_expr, cwt, MAX_TAGEX_LEN);
		strlcpy(&full_expr[len], &expr[1], MAX_TAGEX_LEN-len);
	} else {
		//printk("full_expr = input\n");
		full_expr = kmalloc(sizeof(char) * len, GFP_KERNEL);
		if(!full_expr)
			goto end;
		strlcpy(full_expr, expr, MAX_TAGEX_LEN);
	}

	//printk("full_expr = '%s'\n", full_expr);
	/* Build tree */
	error = -EINVAL;
	tree = build_tree(full_expr);
	//printk("Tree has been built.\n");
	if(!tree)
		goto end;
		
	results = parse_tree(tree, table);
	//printk("Tree has been parsed.\n");

	if(!results) {
		//printk("Found no results\n");
		error = -ENOENT;
		goto end;
	}
	len = element_size(results);
	//printk("Found %d results\n", len);
	if(len == 0) {
		error = -ENOENT;
		goto end;
	}

	/* Copy to user space */
	error = -EFAULT;
	inodes = set_to_array(results);
	for(i = offset; i < offset+size && i < len; i++) {
		// Note: We are copying from the kernel struct into the user struct, but these structs are NOT the same size! 
		if(copy_to_user(&(((struct userspace_inode_entry *)buf)[i-offset]), inodes[i], sizeof(struct userspace_inode_entry)))
			goto end;
	}

	//printk("Cleaning up results\n");
	delete_element(results);
	error = max(i-offset, 0);
end:
	putname(kexpr);
end2:
	if(full_expr)
		kfree(full_expr);
	//printk("lstag returning %d\n", error);
	return error;
}

int distag(unsigned long ino, char __user **buf, unsigned long size, unsigned long tag_offset) {
	const char *tag;
	int ret = 0;
	int i, num_tags, offset;
	int *tag_ids = NULL;
	//printk("distag system call\n");

	printk("@distag ino: %lu\n", ino);
	tag_ids = get_tagids(ino, &num_tags);
	printk("@distag tag_ids: %p\n", tag_ids);
	if(!tag_ids) {
		ret = -ENOENT;
		goto fail_file;
	}
	//printk("num_tags: %d tag_offset: %lu\n", num_tags, tag_offset);
	offset = 0;
	for(i = tag_offset; i < num_tags && i < size; i++) {
		tag = get_tag(table, tag_ids[i]);
		//printk("tag: %s\n", tag);
		if(copy_to_user(buf[i], tag, MAX_TAG_LEN))
			ret = -EFAULT;
	}
	ret = num_tags;
	
fail_file:
	return ret;
}

