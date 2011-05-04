#include <linux/slab.h>

#include "block.h"
#define DISK_TAG

#ifdef DISK_TAG
#define add_tagid2 add_tagid
#define get_tagids2 get_tagids
#define remove_tagid2 remove_tagid

#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/xattr.h>
#include <linux/mount.h>

static struct dentry *get_dentry(unsigned long ino) {
        #define NAME_LEN 10
        char name[NAME_LEN];
        memset(name, '\0', NAME_LEN);
        name[0] = '/';
        snprintf(name + 1, NAME_LEN - 1, "%lu", ino);

        struct qstr this;
        unsigned int c;
        unsigned long hash;
        const char *ptr = name;

        this.name = name;
        c = *(const unsigned char *)name;

        hash = init_name_hash();
        do {
                ptr++;
                hash = partial_name_hash(c, hash);
                c = *(const unsigned char *)ptr;
        } while (c && (c != '/'));
        this.len = ptr - (const char *) this.name;
        this.hash = end_name_hash(hash);

        struct dentry *dentry;
        //dentry = __d_lookuptag(tagfs_root, &this);
        dentry = d_lookup(tagfs_root, &this);

        if (likely(!dentry)) {
                dentry = d_alloc_and_lookuptag(tagfs_root, &this, ino);
                if (IS_ERR(dentry))
                        return NULL;
        }
	//printk("@get_dentry: succeed\n");
        return dentry;
}

int remove_tagid2(unsigned long ino, int id) {
	//printk("@remove_tagid2: ino=%lu, id=%d\n", id);
	struct dentry *dentry = get_dentry(ino);
	if (!dentry)
		return -1;
	dput(dentry);

        char tagid[NAME_LEN];
        memset(tagid, '\0', NAME_LEN);
        snprintf(tagid, NAME_LEN, "user.%d", id);
        //printk("tagid=%s\n", tagid);

        int error = vfs_removexattr(dentry, tagid);
        if (error) {
                printk("error=%d\n", error);
        }
	return 0;
}

int *get_tagids2(unsigned long ino, int *num)
{
        //printk("@get_tagids2: ino=%lu\n", ino);
        struct dentry *dentry = get_dentry(ino);
        if (!dentry)
                return NULL;
	dput(dentry);

        char *klist = NULL;
        #define SIZE 100
        klist = kmalloc(SIZE, GFP_KERNEL);
        if (!klist)
                return NULL;
        int error = vfs_listxattr(dentry, klist, SIZE);
        if (error < 0) {
		printk("error=%d\n", error);
                return NULL;
	}
        //printk("expected size=%d\n", error);
	int size, total_size, tag;
	const char *read = klist;
	int *write = (int *)klist;
	*num = 0;
	for (size = total_size = strlen(read) + 1; total_size <= error; ) {
		tag = simple_strtoul(read + 5, NULL, 10);
		//printk("tag=%d\n", tag);
		write[*num] = tag;
		(*num)++;
		if (total_size == error)
			break;
		read += size;
		size = strlen(read) + 1;
		total_size += size;
	}
        //error = vfs_listxattr(dentry, NULL, 0);
	//printk("list size=%d\n", error);
        //kfree(klist);
	
        return (int *)klist;
}

void deallocate_all() {
}

int remove_all_tagids2(unsigned long ino) {
	//printk("@remove_all_tagid2: ino=%lu\n", ino);

        struct dentry *dentry = get_dentry(ino);
        if (!dentry)
                return -1;
        dput(dentry);

        char *klist = NULL;
        klist = kmalloc(SIZE, GFP_KERNEL);
        if (!klist)
                return -1;
        int error = vfs_listxattr(dentry, klist, SIZE);
        if (error < 0) {
                printk("error=%d\n", error);
                return -1;
        }
        //printk("expected size=%d\n", error);
        int size, total_size;
        const char *read = klist;
        int num = 0;
        for (size = total_size = strlen(read) + 1; total_size <= error; ) {
		printk("remove tag=%s\n", read);
		int error = vfs_removexattr(dentry, read);
        	if (error) {
                	printk("error=%d\n", error);
		}
                num++;
                if (total_size == error)
                        break;
                read += size;
                size = strlen(read) + 1;
                total_size += size;
        }
        kfree(klist);
	return 0;
}

int add_tagid2(unsigned long ino, int id)
{
        //printk("@add_tagid2: ino=%lu, id=%d\n", ino, id);
        struct dentry *dentry;
        dentry = get_dentry(ino);
        if (!dentry)
                return -1;
	dput(dentry);

        char tagid[NAME_LEN];
        memset(tagid, '\0', NAME_LEN);
        snprintf(tagid, NAME_LEN, "user.%d", id);
	//printk("tagid=%s\n", tagid);

        int error = vfs_setxattr(dentry, tagid, "1", 1, 0);
	if (error) {
        	printk("error=%d\n", error);
	}
        return error;
}


int allocate_block(unsigned long ino) {
	return 0;
}

void deallocate_block(unsigned long ino) {
}

#else
struct block {
	unsigned long ino;
	int tag[MAX_NUM_TAGS];
	int num_tags;
	struct block *next;
};

struct block *head = NULL;

int allocate_block(unsigned long ino) {
	struct block *new;
	new = kmalloc(sizeof(struct block), GFP_KERNEL);
	if (!new)
		return -1;
	new->ino = ino;
	new->num_tags = 0;
	new->next = head;
	head = new;
	return 0;
}

int *get_tagids(unsigned long ino, int *num_tags) {
	struct block *curr = head;
	while(curr) {
		if (curr->ino == ino) {
			*num_tags = curr->num_tags;
			return curr->tag;
		}
		curr = curr->next;
	}
	*num_tags = 0;
	return NULL;
}

int add_tagid(unsigned long ino, int tag) {
	struct block *curr = head;
	while(curr) {
		if (curr->ino == ino) {
			if (curr->num_tags >= MAX_NUM_TAGS)
				return -1;
			curr->tag[curr->num_tags++] = tag;
add_tagid2(ino, tag);
int i, num = 0;
int *list = NULL;
list = get_tagids2(ino, &num);
printk("#tag=%d\n", num);
for (i = 0; i < num; i++) {
	printk("tag=%d\n", list[i]);
}
kfree(list);
//remove_tagid2(ino, 1);
remove_all_tagids2(ino);
			return 0;
		}
		curr = curr->next;
	}
	return -1;
}

int remove_tagid(unsigned long ino, int tag) {
	struct block *curr = head;
	while(curr) {
		if (curr->ino == ino) {
			int i;
			for (i = 0; i < curr->num_tags; i++) {
				if (curr->tag[i] == tag) {
					int j;
					for(j = i; j < curr->num_tags - 1; j++)
						curr->tag[j] = curr->tag[j+1];
					curr->num_tags--;
					break;
				}
			}
		}
		curr = curr->next;
	}
	return 0;
}

void mv_block(unsigned long ino1, unsigned long ino2)
{
	struct block *curr = head;
	while (curr) 
	{
		if (curr->ino == ino1) {
			curr->ino = ino2;
			return;
		}

	}
}

void deallocate_block(unsigned long ino) {
	struct block *prev, *curr;
	prev = NULL;
	curr = head;
	while(curr) {
		if (curr->ino == ino) {
			if (prev)
				prev->next = curr->next;
			else
				head = curr->next;
			kfree(curr);
			return;
		}
		prev = curr;
		curr = curr->next;
	}
}

void deallocate_all() {
	while(head) {
		struct block *temp = head;
		head = head->next;
		kfree(temp);
	}
}
#endif
