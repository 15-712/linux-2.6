#include <linux/fs.h>
#include <linux/slab.h>

#include "syscall.h"
#include "table.h"

char cwt[MAX_TAGEX_LEN+1];
struct expr_tree *tree = NULL;

int (*prev_addtag)(const char __user *, const char __user *);
int (*prev_rmtag)(const char __user *, const char __user *);
int (*prev_chtag)(const char __user *);
int (*prev_mvtag)(const char __user *, const char __user *);
int (*prev_getcwt)(const char __user *, unsigned long size);
int (*prev_lstag)(const char __user *, char __user *, unsigned long, unsigned long);

int addtag(const char __user *filename, const char __user *tag) {
	char *file, *t;
	int *tag_ids = NULL;
	struct table_element *curr, *check;
	struct inode_entry *ent;
	const struct inode_entry **entries;
	struct inode *ino = NULL;
	int i, ret = 0, num_tags = 0, conflict, min;

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
		if (!check || size(temp) < min) {
			check = temp;
			min = size(temp);
		}
	}
	entries = set_to_array(curr);
	conflict = 0;
	for(i = 0; i < size(curr); i++) {
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
		for(i = 0; i < size(curr); i++)
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

int lstag(const char __user *expr, struct inode_entry __user *buf, unsigned long size, unsigned long offset) {
	struct expr_tree *tree;
	struct table_element *results;
	char *kexpr = getname(expr);
	unsigned int i;
	
	if (IS_ERR(kexpr))
		return -ENOMEM;
	tree = build_tree(expr);
	if(!tree)
		return -EINVAL;
	results = parse_tree(tree);
	/* Do some parsing of the results and format it for the buffer */
	int len = size(results);
	if(len == 0)
		return -EINVAL;
	struct inode_entry **inodes = set_to_array(results);
	for(i = offset; i < offset+size && i < len; i++) {
		copy_to_user(&buf[i-offset], inodes[i], sizeof(struct inode_entry));
	}
	return max(i-offset, 0);
}

