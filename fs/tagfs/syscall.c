#include <linux/fs.h>
#include <linux/slab.h>

#include "syscall.h"
#include "table.h"


int addtag(const char __user *filename, const char __user *tag) {
	char *file, *t;
	int *tag_ids = NULL;
	struct table_element *curr, *check;
	struct inode_entry *ent, **entries;
	struct inode *ino = NULL;
	int i, ret, num_tags = 0, conflict, min;

	file = getname(filename);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto fail_file;
	}
	t = getname(tag);
	if (IS_ERR(t)) {
		putname(file);
		ret = PTR_ERR(t);
		goto fail_tag;
	}
	//TODO: ino <- Get inode
	//TODO: tag_ids <- Get tags from inode
	curr = get_inodes(table, t);
	//Easy case
	if (!curr) {
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
		// TODO: Insert new tag into inode
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
		if (!curr) {
			//TODO: Clean up
			ret = -ENOMEM;
			goto fail;
		}
		if (i > 0)
			kfree(temp);
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
	if (conflict) {
		//TODO: Clean up
		ret = -EINVAL;
		goto fail;
	}
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
