#include <linux/fs.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/err.h>
#include <asm-generic/fcntl.h>
#include <asm-generic/errno-base.h>
#include <linux/file.h>
#include <linux/fsnotify.h>

#include "syscall.h"
#include "table.h"

char cwt[MAX_TAGEX_LEN+1];
struct expr_tree *tree = NULL;

static char inv[] = {'.', '&', '|', '/'};

int (*prev_opentag)(const char __user *, int);
int (*prev_addtag)(const char __user *, const char __user *);
int (*prev_rmtag)(const char __user *, const char __user *);
int (*prev_chtag)(const char __user *);
int (*prev_mvtag)(const char __user *, const char __user *);
int (*prev_getcwt)(char __user *, unsigned long size);
int (*prev_lstag)(const char __user *, void __user *, unsigned long, int);

void install_syscalls(void) {
	prev_opentag = opentag_ptr;
	prev_addtag = addtag_ptr;
	prev_rmtag = rmtag_ptr;
	prev_chtag = chtag_ptr;
	prev_mvtag = mvtag_ptr;
	prev_getcwt = getcwt_ptr;
	prev_lstag = lstag_ptr;
	opentag_ptr = opentag;
	addtag_ptr = addtag;
	rmtag_ptr = rmtag;
	chtag_ptr = chtag;
	mvtag_ptr = mvtag;
	getcwt_ptr = getcwt;
	lstag_ptr = lstag;
}

void uninstall_syscalls(void) {
	opentag_ptr = prev_opentag;
	addtag_ptr = prev_addtag;
	rmtag_ptr = prev_rmtag;
	chtag_ptr = prev_chtag;
	mvtag_ptr = prev_mvtag;
	getcwt_ptr = prev_getcwt;
	lstag_ptr = prev_lstag;
}

static long do_sys_opentag(const char __user *tagexp, int flags)
{
        char *tmp = getname(tagexp);
        int fd = PTR_ERR(tmp);

        // checks flags
        if ((flags != O_RDONLY) && (flags != O_WRONLY) && (flags != O_RDWR))
                return -EINVAL;

        // gets inode number
        struct expr_tree *e = build_tree(tagexp);
        if (e == NULL)
                return -EINVAL;
        struct table_element *t = parse_tree(e, table);
        if (t == NULL)
                return -EINVAL;
        if (element_size(t) != 1)
                return -EMFILE;  // too many open files

        unsigned long ino = set_to_array(t)[0]->ino;

        if ((!IS_ERR(tmp)) || (ino > 0)) {
                fd = get_unused_fd_flags(flags);
                if (fd >= 0) {
                        struct file *f = do_filp_opentag(ino, flags, 0);
                        if (IS_ERR(f)) {
                                put_unused_fd(fd);
                                fd = PTR_ERR(f);
                        } else {
                                fsnotify_open(f);
                                fd_install(fd, f);
                        }
                }
                putname(tmp);
        }
        return fd;
}

// @flags: O_RDONLY, O_WDONLY, O_RDWR
int opentag(const char __user *tagexp, int flags) {
        long ret;

        //if (force_o_largefile())
                //flags |= O_LARGEFILE;

        ret = do_sys_opentag(tagexp, flags);

        /* avoid REGPARM breakage on x86: */
        asmlinkage_protect(2, ret, tagexp, flags);

        return ret;
}

int addtag(const char __user *filename, const char __user *tag) {
	char *file, *t;
	int *tag_ids = NULL;
	struct table_element *curr, *check;
	struct inode_entry *ent;
	const struct inode_entry **entries;
	struct inode *ino = NULL;
	int i, ret = 0, num_tags = 0, conflict, min, len;

	file = getname(filename);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto fail_file;
	}
	t = getname(tag);
	if (IS_ERR(t)) {
		ret = PTR_ERR(t);
		goto fail_tag;
	}
	len = strlen(t);
	for (i = 0; i < len; i++) {
		int j;
		for (j = 0; sizeof(inv) / sizeof(char); j++) {
			if (t[i] == inv[j]) {
				ret = -EINVAL;
				goto fail_tag;
			}
		}
	}
	//TODO: ino <- Get inode
	//TODO: tag_ids <- Get tags from inode
	curr = get_inodes(table, t);
	//Easy case
	if (!curr) {
		/* TODO: Insert new tag into inode 
		 *       if first tag, need to allocate block
		 *       if block allocationg fails, return failure condition
		 */
		if (num_tags) {
			struct table_element *e = get_inodes(table, get_tag(table, tag_ids[0]));
			ent = find_entry(e, ino->i_ino);
		} else {
			ent = kmalloc(sizeof(struct inode_entry), GFP_KERNEL);
			if (!ent) {
				//TODO: Clean up
				ret = -ENOMEM;
				goto fail;
			}
			ent->ino = ino->i_ino;
			strncpy(ent->filename, file, MAX_FILENAME_LEN);
			ent->count = 0;
		}
		ret = table_insert(table, t, ent);
		if (ret) {
			/*TODO: Clean up, may not be memory error, need 
			  to check return value*/
			ret = -ENOMEM;
			goto fail;
		}
		putname(t);
		putname(file);
		return 0;
	}
	for (i = 0; i < num_tags; i++) {
		if (strncmp(t, get_tag(table, tag_ids[i]), MAX_TAG_LEN) == 0) {
			ret = -EINVAL;
			goto fail;
		}
	}
	min = 0;
	check = NULL;
	for (i = 0; i < num_tags; i++) {
		struct table_element *prev = curr;
		struct table_element *temp = get_inodes(table, get_tag(table, tag_ids[i]));
		curr = set_intersect(prev, temp);
		if (i > 0)
			delete_element(prev);
		if (!curr) {
			//TODO: Clean up
			ret = -ENOMEM;
			goto fail;
		}
		if (!check || element_size(temp) < min) {
			check = temp;
			min = element_size(temp);
		}
	}
	entries = set_to_array(curr);
	conflict = 0;
	for(i = 0; i < element_size(curr); i++) {
		if (entries[i]->count == num_tags + 1 && strncmp(entries[i]->filename, file, MAX_FILENAME_LEN)) {
			conflict = 1;
			break;
		}
	}
	if (num_tags > 0)
		delete_element(curr);
	if (conflict) {
		//TODO: Clean up
		ret = -EINVAL;
		goto fail;
	}
	/* TODO: Insert new tag into inode 
	 *       if first tag, need to allocate block
	 *       if block allocationg fails, return failure condition
	 */
	if (!check) {
		ent = kmalloc(sizeof(struct inode_entry), GFP_KERNEL);
		if (!ent) {
			//TODO: Clean up
			ret = -ENOMEM;
			goto fail;
		}
		ent->ino = ino->i_ino;
		strncpy(ent->filename, file, MAX_FILENAME_LEN);
		ent->count = 0;
	} else {
		ent = find_entry(check, ino->i_ino);
	}
	ret = table_insert(table, t, ent);
	if (ret) {
		ret = -ENOMEM;
		/*TODO: Clean up, may not be memory error, need 
		  to check return value*/
		goto fail;
	}
	putname(t);
	putname(file);
	return 0;

fail:
	//TODO: clean up
	putname(t);
fail_tag:
	putname(file);
fail_file:
	return ret;
}

int rmtag(const char __user *filename, const char __user *tag) {
	char *file, *t;
	int *tag_ids = NULL;
	struct table_element *curr;
	const struct inode_entry **entries;
	struct inode *ino = NULL;
	int i, ret = 0, num_tags = 0, conflict;

	file = getname(filename);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto end;
	}
	t = getname(tag);
	if (IS_ERR(t)) {
		ret = PTR_ERR(t);
		goto clean_file;
	}
	//TODO: ino <- Get inode
	curr = get_inodes(table, t);
	//Easy case, tag doesn't exist;
	if (!curr)
		goto clean_up;
	//TODO: tag_ids <- Get tag ids from block
	if (num_tags > 1) {
		for(i = 0; i < num_tags; i++)
			if (strcmp(get_tag(table, tag_ids[i]), t) == 0) {
				int j;
				for(j = i+1; j < num_tags; j++)
					tag_ids[j-1] = tag_ids[j];
				break;
			}
		curr = get_inodes(table, get_tag(table, tag_ids[0]));
		for(i = 1; i < num_tags - 1; i++) {
			struct table_element *prev = curr;
			struct table_element *temp = get_inodes(table, get_tag(table, tag_ids[i]));
			curr = set_intersect(prev, temp);
			if (i > 1)
				delete_element(prev);
			if (!curr) {
				ret = -ENOMEM;
				goto clean_up;
			}
		}
		entries = set_to_array(curr);
		conflict = 0;
		for(i = 0; i < element_size(curr); i++)
			if (entries[i]->count == num_tags - 1 && strncmp(entries[i]->filename, file, MAX_FILENAME_LEN)) {
				conflict = 1;
				break;
			}
		if (num_tags > 2)
			delete_element(curr);
		if (conflict) {
			ret = -EINVAL;
			goto clean_up;
		}
	}
	table_remove(table, t, ino->i_ino);
	//TODO: remove tags from inode, possibly deallocating the block
clean_up:
	putname(t);
clean_file:
	putname(file);
end:
	return ret;
}

int chtag(const char __user *tagex) {
	char *ktagex = getname(tagex);
	struct expr_tree *new_tree;
	int len, ret = 0;
	if (IS_ERR(ktagex)) {
		ret = -ENOMEM;
		goto end;
	}
	len = strlen(ktagex);
	if (strlen(ktagex) > MAX_TAGEX_LEN) {
		ret = -EINVAL;
		goto clean_up;
	}
	if (len == 0) {
		cwt[0] = '\0';
		free_tree(tree);
		tree = NULL;
		goto clean_up;
	}

	if (!(new_tree = build_tree(tagex))) {
		/* TODO: need some way to check if the expression
		 *       has an error or a memory error occurred
		 */
		ret = -EINVAL;
		goto clean_up;
	}
	strcpy(cwt, ktagex);
	if (tree)
		free_tree(tree);
	tree = new_tree;
clean_up:
	putname(ktagex);
end:
	return ret;
}

int mvtag(const char __user *tag1, const char __user *tag2) {
	char *kt1, *kt2;
	int ret;
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
	int error = -ERANGE;
	unsigned long len = strlen(cwt);
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
	const struct inode_entry **inodes;
	char *kexpr = getname(expr);
	char *full_expr = NULL;
	unsigned int i;
	unsigned int len;
	int error;
	
	error = -ENOMEM;
	if (IS_ERR(kexpr))
		goto end;

	/* If expr starts with a '.' then prepend cwt to expr */
	len = strlen(cwt) + strlen(kexpr);	
	full_expr = kmalloc(sizeof(char) * len, GFP_KERNEL);
	if(!full_expr)
		goto end;
	len = strlcpy(full_expr, cwt, MAX_TAGEX_LEN);
	strlcpy(&full_expr[len], &expr[1], MAX_TAGEX_LEN-len);

	/* Build tree */
	error = -EINVAL;
	tree = build_tree(full_expr);
	if(!tree)
		goto end;
		
	results = parse_tree(tree, table);

	len = element_size(results);
	if(len == 0)
		goto end;

	/* Copy to user space */
	error = -EFAULT;
	inodes = set_to_array(results);
	for(i = offset; i < offset+size && i < len; i++) {
		if(copy_to_user(&((struct inode_entry *)buf)[i-offset], inodes[i], sizeof(struct inode_entry)))
			goto end;
	}
	error = max(i-offset, 0);
end:
	putname(kexpr);
	if(full_expr)
		kfree(full_expr);
	return error;
}

int distag(char __user *filename, char __user *buf, unsigned int size) {
	char *file;
	int ret = 0;

	file = getname(filename);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto fail_file;
	}
	
	putname(file);	
fail_file:
	return ret;
}
