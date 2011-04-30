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

#include "syscall.h"
#include "table.h"
#include "block.h"

char cwt[MAX_TAGEX_LEN+1];
struct expr_tree *tree = NULL;
struct hash_table *table;

static char inv[] = {'.', '&', '|', '/'};

struct userspace_inode_entry {
	unsigned long ino;
	char filename[MAX_FILENAME_LEN+1];
	unsigned int count;
};

int (*prev_opentag)(const char __user *, int);
int (*prev_addtag)(const char __user *, const char __user *);
int (*prev_rmtag)(const char __user *, const char __user *);
int (*prev_chtag)(const char __user *);
int (*prev_mvtag)(const char __user *, const char __user *);
int (*prev_getcwt)(char __user *, unsigned long size);
int (*prev_lstag)(const char __user *, void __user *, unsigned long, int);
int (*prev_distag)(const char __user *, char __user *, unsigned long); 

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
	printk("@do_sys_opentag\n");
        char *tmp = getname(tagexp);
        int fd = PTR_ERR(tmp);
	struct expr_tree *e;
	struct table_element *t;
	unsigned long ino;

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
        if (element_size(t) != 1)
                return -EMFILE;  // too many open files

        ino = set_to_array(t)[0]->ino;
	printk(KERN_ALERT "ino=%lu\n", ino);

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

	printk("opentag system call\n");
	printk("@opentag\n");
        //if (force_o_largefile())
                //flags |= O_LARGEFILE;

        ret = do_sys_opentag(tagexp, flags);

        /* avoid REGPARM breakage on x86: */
        asmlinkage_protect(2, ret, tagexp, flags);

        return ret;
}

int addtag(const char __user *filename, const char __user *tag) {
	char *file, *t, *name;
	int *tag_ids = NULL;
	struct table_element *curr, *check;
	struct inode_entry *ent;
	struct inode_entry **entries;
	unsigned long ino = 0;
	int i, ret = 0, num_tags = 0, conflict, min, len;

	printk("addtag system call\n");
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
	printk("checking for invalid characters\n");
	for (i = 0; i < len; i++) {
		int j;
		for (j = 0; j < sizeof(inv) / sizeof(char); j++) {
			if (t[i] == inv[j]) {
				printk("Invalid tag character: %c\n", inv[j]);
				ret = -EINVAL;
				goto fail_tag;
			}
		}
	}
	len = strlen(file);
	i = len - 1;
	printk("making sure file is not a directory\n");
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
	//TODO: tag_ids <- Get tags from inode
	tag_ids = get_tagids(ino, &num_tags);
	if (num_tags >= MAX_NUM_TAGS) {
		printk("File has too many tags.\n");
		ret = -EINVAL;
		goto fail;
	}
	curr = get_inodes(table, t);
	//Easy case
	if (!curr) {
		printk("creating new tag\n");
		/* TODO: Insert new tag into inode 
		 *       if first tag, need to allocate block
		 *       if block allocation fails, return failure condition
		 */
		if (num_tags) {
			printk("looking up inode_entry\n");
			struct table_element *e = get_inodes(table, get_tag(table, tag_ids[0]));
			ent = find_entry(e, ino);
		} else {
			printk("creating inode_entry\n");
			ent = kmalloc(sizeof(struct inode_entry), GFP_KERNEL);
			if (!ent) {
				//TODO: Clean up
				ret = -ENOMEM;
				goto fail;
			}
			ent->ino = ino;
			strncpy(ent->filename, name, MAX_FILENAME_LEN);
			ent->count = 0;
		}
		printk("Inserting tag into table\n");
		ret = table_insert(table, t, ent);
		if (ret) {
			/*TODO: Clean up, may not be memory error, need 
			  to check return value*/
			ret = -ENOMEM;
			goto fail;
		}
		//TODO: Make persistent
		if (num_tags < 1 && allocate_block(ino)) {
			table_remove(table, t, ino);
			ret = -ENOMEM;
			goto fail;
		}
		printk("Adding tag id\n");
		add_tagid(ino, get_tagid(table, t));
		putname(t);
		putname(file);
		return 0;
	}
	for (i = 0; i < num_tags; i++) {
		if (strncmp(t, get_tag(table, tag_ids[i]), MAX_TAG_LEN) == 0) {
			printk("File already has that tag.\n");
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
		if (entries[i]->count == num_tags + 1 && strncmp(name, entries[i]->filename, MAX_FILENAME_LEN) == 0) {
			conflict = 1;
			break;
		}
	}
	if (num_tags > 0)
		delete_element(curr);
	if (conflict) {
		//TODO: Clean up
		printk("Conflict.\n");
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
		ent->ino = ino;
		strncpy(ent->filename, name, MAX_FILENAME_LEN);
		ent->count = 0;
	} else {
		ent = find_entry(check, ino);
	}
	ret = table_insert(table, t, ent);
	if (ret) {
		ret = -ENOMEM;
		/*TODO: Clean up, may not be memory error, need 
		  to check return value*/
		goto fail;
	}
	//TODO: make persistent
	if (num_tags < 1 && allocate_block(ino)) {
		table_remove(table, t, ino);
		ret = -ENOMEM;
		goto fail;
	}
	add_tagid(ino, get_tagid(table, t));
	putname(t);
	putname(file);
	return 0;

	printk("Finished addtag\n");
fail:
	//TODO: clean up
	putname(t);
fail_tag:
	putname(file);
fail_file:
	return ret;
}

int rmtag(const char __user *filename, const char __user *tag) {
	char *file, *t, *name;
	int *tag_ids = NULL;
	struct table_element *curr;
	struct inode_entry **entries;
	unsigned long ino = 0;
	int i, ret = 0, num_tags = 0, conflict, len;
	int t_id = 0;

	printk("rmtag system call\n");

	file = getname(filename);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto end;
	}
	len = strlen(file);
	i = len - 1;
	printk("making sure file is not a directory\n");
	while(i >= 0) {
		if (file[i] == '/')
			break;
		i--;
	}
	if (i == len - 1) {
		printk("Cannot tag a directory.\n");
		ret = -EINVAL;
		goto clean_file;
	}
	name = file + i + 1;
	t = getname(tag);
	if (IS_ERR(t)) {
		ret = PTR_ERR(t);
		goto clean_file;
	}
	//TODO: ino <- Get inode
	ino = ino_by_name(filename);
	if (!ino) {
		ret = -ENOMEM;
		goto clean_up;
	}
	curr = get_inodes(table, t);
	//Easy case, tag doesn't exist;
	if (!curr)
		goto clean_up;
	//TODO: tag_ids <- Get tag ids from block
	tag_ids = get_tagids(ino, &num_tags);
	if (num_tags > 1) {
		
		for(i = 0; i < num_tags; i++)
			if (strcmp(get_tag(table, tag_ids[i]), t) == 0) {
				t_id = tag_ids[i];
				break;
			}
			
		curr = get_inodes(table, get_tag(table, tag_ids[0]));
		for(i = 1; i < num_tags; i++) {
			struct table_element *prev;
			struct table_element *temp;
			if(tag_ids[i] == t_id) 
				continue;
			prev = curr;
			temp = get_inodes(table, get_tag(table, tag_ids[i]));
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
			if (entries[i]->count == num_tags - 1 && strncmp(name, entries[i]->filename, MAX_FILENAME_LEN) == 0) {
				printk("Removing tag introduces a conflict\n");
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
	remove_tagid(ino, get_tagid(table, t));
	if (num_tags == 1)
		deallocate_block(ino);
	table_remove(table, t, ino);
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
	printk("chtag system call\n");
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
	printk("mvtag system call\n");
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
	printk("getcwt system call\n");
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
	
	printk("lstag system call\n");
	error = -ENOMEM;
	if (IS_ERR(kexpr)) {
		error = PTR_ERR(kexpr);
		printk("IS_ERR(kexpr)\n");
		goto end2;
	}
	/* If expr starts with a '.' then prepend cwt to expr */
	len = strlen(kexpr);
	if(len == 0) {
		printk("full_expr = cwt\n");
		full_expr = cwt;
	} else if(kexpr[0] == '.') {
		printk("full_expr = cwt & input\n");
		len += strlen(cwt);
		full_expr = kmalloc(sizeof(char) * len, GFP_KERNEL);
		if(!full_expr)
			goto end;
		len = strlcpy(full_expr, cwt, MAX_TAGEX_LEN);
		strlcpy(&full_expr[len], &expr[1], MAX_TAGEX_LEN-len);
	} else {
		printk("full_expr = input\n");
		full_expr = kmalloc(sizeof(char) * len, GFP_KERNEL);
		if(!full_expr)
			goto end;
		strlcpy(full_expr, expr, MAX_TAGEX_LEN);
	}

	printk("full_expr = '%s'\n", full_expr);
	/* Build tree */
	error = -EINVAL;
	tree = build_tree(full_expr);
	printk("Tree has been built.\n");
	if(!tree)
		goto end;
		
	results = parse_tree(tree, table);
	printk("Tree has been parsed.\n");

	if(!results) {
		printk("Found no results\n");
		error = -ENOENT;
		goto end;
	}
	len = element_size(results);
	printk("Found %d results\n", len);
	if(len == 0)
		goto end;

	/* Copy to user space */
	error = -EFAULT;
	inodes = set_to_array(results);
	for(i = offset; i < offset+size && i < len; i++) {
		// Note: We are copying from the kernel struct into the user struct, but these structs are NOT the same size! 
		if(copy_to_user(&(((struct userspace_inode_entry *)buf)[i-offset]), inodes[i], sizeof(struct userspace_inode_entry)))
			goto end;
	}

	printk("Cleaning up results\n");
	delete_element(results);
	error = max(i-offset, 0);
end:
	putname(kexpr);
end2:
	if(full_expr)
		kfree(full_expr);
	return error;
}

int distag(const char __user *filename, char __user *buf, unsigned long size) {
	char *file;
	char tag_list[MAX_TAG_LEN*7];
	const char *tag;
	int ret = 0;
	int i, ino, len, num_tags, offset;
	int *tag_ids = NULL;
	printk("distag system call\n");

	file = getname(filename);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto fail_file;
	}
	
	printk("filename: '%s'\n", file);
	ino = ino_by_name(filename);
	printk("ino: %d\n", ino);
	tag_ids = get_tagids(ino, &num_tags);
	printk("num_tags: %d\n", num_tags);
	offset = 0;
	for(i = 0; i < num_tags; i++) {
		tag = get_tag(table, tag_ids[i]);
		printk("tag: %s\n", tag);
		len = strlen(tag);
		if(offset + len + 1 > MAX_TAG_LEN*7)
			break;
		strncpy(&tag_list[offset], tag, MAX_TAG_LEN);
		offset += len+1;
		if(i != num_tags - 1)
			tag_list[offset-1] = ',';
	}
	
	if(copy_to_user(buf, tag_list, size))
		ret = -EFAULT;
	putname(file);	
fail_file:
	return ret;
}

