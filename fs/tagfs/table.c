/** @file table.c
 *  @brief Hash table for the tags. 
 *  @author William Wang
 *  @author Tim Shields
 *  @author Ping-Yao Tseng
 *   
 *  Tag lookup table implementation. Each bucket contains a list of inodes
 *  stored in a table_element data structure.
 *
 */

#include "table.h"
#include <linux/hash.h>
#include <linux/slab.h>

#define NUM_HASH_BITS	12
#define INITIAL_TAG_CAPACITY	1024
#define INT_TO_CHAR(x, c, i) \
	do { \
		for (i = 0 ; i < sizeof(x); i++) \
			c[i] = (char)((x) >> (i * 8)); \
	} while(0)

#define CHAR_TO_INT(c, x, i) \
	do { \
		for (i = 0; i < sizeof(x); i++) \
			i |= c[i] << (i * 8); \
	} while(0)

struct hash_table {
	struct tag_node *table[1<<NUM_HASH_BITS];
	struct tag_lookup_array *lookup_table;
	unsigned int num_tags;
};


struct tag_lookup_array {
	char *tag;
	unsigned int capacity;
	struct free_list_entry *free_list;
};

struct free_list_entry {
	int free_index;
	struct free_list_entry *next;
};

/* Struct for the buckets of the hash table.
 * collisions handled by linked list */
struct tag_node {
	char tag[MAX_TAG_LEN];
	struct table_element *e;
	int tag_id;
	struct tag_node *next;
};

enum tag_node_error {
	INVALID_TABLE = -255,
	INVALID_NODE,
};

/* 
*  Function used to hash tags
*  String hashing function taken from linux/sunrpc/avcauth.h
*/
static inline unsigned long hash_tag(const char *name)
{
	unsigned long hash = 0;
	unsigned long l = 0;
	int len = 0;
	unsigned char c;
	do {
		if (unlikely(!(c = *name++))) {
			c = (char)len; len = -1;
		}
		l = (l << 8) | c;
		len++;
		if ((len & (BITS_PER_LONG/8-1))==0)
			hash = hash_long(hash^l, BITS_PER_LONG);
	} while (len);
	return hash >> (BITS_PER_LONG - NUM_HASH_BITS);
}

/* Removes a tag from the lookup table */
unsigned int remove_tag(struct hash_table *table, int index) {
	struct free_list_entry *free_list;
	struct free_list_entry *new_entry;
	//printk("Removing tag index %d\n", index);
	table->lookup_table->tag[index*MAX_TAG_LEN] = '\0';

	free_list = table->lookup_table->free_list;
	new_entry = kmalloc(sizeof(struct free_list_entry), GFP_KERNEL);
	if(!new_entry) {
		return NO_MEMORY;
	}
	new_entry->free_index = index;
	new_entry->next = free_list;
	table->lookup_table->free_list = new_entry;
	/*if(!free_list) {
		table->lookup_table->free_list = new_entry;
	} else {
		while(free_list->next) {
			free_list = free_list->next;
		}
		free_list->next = new_entry;
	}*/
	return 0;
}

/* Assigns and ID to a tag and creates a new entry in the lookup table */
unsigned int create_new_tag(struct hash_table *table, const char *tag)
{
	struct tag_lookup_array *t; 
	struct free_list_entry *free_list;
	unsigned int id;
	t = table->lookup_table;
	free_list = t->free_list;
	if(free_list) {
		id = free_list->free_index;
		t->free_list = free_list->next;
		kfree(free_list);
	} else if(table->num_tags == t->capacity) {
		char *new_tag_array;
		new_tag_array = krealloc(t->tag, (t->capacity << 1) * MAX_TAG_LEN, GFP_KERNEL);		
		if(!new_tag_array)
			return NO_MEMORY; 
		t->tag = new_tag_array;
		t->capacity <<= 1;
		id = table->num_tags;
	} else {
		id = table->num_tags;
	}
	strlcpy(&(t->tag[id*MAX_TAG_LEN]), tag, MAX_TAG_LEN);
	table->num_tags++;
	return id;
}

/* Searches the hash table for a node with a given tag 
 * Returns NULL if entry does not exist. */
struct tag_node *find_node(struct hash_table *table, const char *tag) 
{
	struct tag_node *node;
	//printk("finding node with tag %s\n", tag);
	if(!table)
		return NULL;
	node = table->table[hash_tag(tag)];
	//printk("Head node address %p\n", node);
	while(node != NULL && strncmp(node->tag, tag, MAX_TAG_LEN) != 0) {
		node = node->next;
		//printk("Node address %p\n", node);
	}
	/*if (node)
		printk("Found node %p with tag %s\n", node, node->tag);*/
	return node;
}

/* Adds a node to the hash table. */
int add_node(struct hash_table *table, struct tag_node *new_node, unsigned int hash) 
{
	struct tag_node *node;
	if(!table)
		return INVALID_TABLE;
	node = table->table[hash];
	if(likely(node == NULL)) {
		table->table[hash] = new_node;
	} else {
		while(node->next != NULL) {
			node = node->next;
		}
		node->next = new_node;
	}
	return 0;
	
}

int remove_node(struct hash_table *table, const char *tag) {
	struct tag_node *head;
	struct tag_node *node;
	int id;
	if(!table)
		return INVALID_TABLE;
	id = hash_tag(tag);
	head = table->table[id];
	if(!head) {
		return 0;
	}
	/* no collision in hash table */
	if(!head->next) {
		kfree(head);
		table->table[id] = NULL;
		return 0;

	}

	/* remove head */
	if(strncmp(head->tag, tag, MAX_TAG_LEN) == 0) {
		node = head;
		table->table[id] = head->next;
		kfree(node);
		return 0;
	}

	/* collision in hash table */
	while(head->next && strncmp(head->next->tag, tag, MAX_TAG_LEN) != 0) {
		head = head->next;
	}

	node = head->next;	
	head->next = node->next;
	kfree(node);
	return 0;

}

/* Removes an inode from the specified tag. */
int table_remove(struct hash_table *table, const char *tag, unsigned long inode_num) {
	struct tag_node *node;
	node = find_node(table, tag);
	if(node) {
		//printk("Removing inode %lu from %s\n", inode_num, tag);
		remove_entry(node->e, inode_num);
		if(element_size(node->e) == 0) {
			//printk("No more files with this tag, deleting tag from table\n");

			/* decrement tag count */
			table->num_tags--;

			/* remove tag from hash table */
			delete_element(node->e);
			remove_node(table, tag);
			/* remove tag from lookup table */
			remove_tag(table, node->tag_id);
		}

	}
	return 0;
}

/* Inserts an inode in the given tag entry. Creates tag if necessary. */
int table_insert(struct hash_table *table, const char *tag, struct inode_entry *i)
{
	struct tag_node *node;
	unsigned int tag_id;
	int e;
	//printk("Adding tag %s to inode\n", tag);
	node = find_node(table, tag);
	if(!node) {
		/* Create empty table element */
		node = kcalloc(sizeof(struct tag_node), 1, GFP_KERNEL);
		if(!node)
			return NO_MEMORY;
		node->next = NULL;
		tag_id = create_new_tag(table, tag);
		if(tag_id == NO_MEMORY) {
			kfree(node);
			return NO_MEMORY;
		}
		//printk("Created new tag '%s' with id %d\n", tag, tag_id);
		node->e = new_element();
		if (!node->e) {
	   		remove_tag(table, tag_id);	
			kfree(node);
			return NO_MEMORY;
		}

		if((e = add_node(table, node, hash_tag(tag))) < 0) {
			delete_element(node->e);
	   		remove_tag(table, node->tag_id);	
			kfree(node);
			return e;
		}
		strlcpy(node->tag, tag, MAX_TAG_LEN);
		node->tag_id = tag_id;
	}

	//printk("Finished successfully\n");
	e = insert_entry(node->e, i);
	return e;
}
/* Returns the table_element structure of inodes associated with the specified tag.  */
struct table_element * get_inodes(struct hash_table *table, const char* tag) 
{
	struct tag_node *n = find_node(table, tag);
	if(!n)
		return NULL;
	//printk("Element contains %d inodes\n", element_size(n->e));
	return n->e;
}

/* Creates the hash table */
struct hash_table * create_table(void)
{
	struct hash_table *head;
	struct tag_lookup_array *lookup;
	char *tags;
	int i;

	/* Allocate hash table */
	head = kmalloc(sizeof(struct hash_table),  GFP_KERNEL);
	if(!head)
		return NULL;

	/* Allocate tag block */
	tags = kmalloc(sizeof(char)*MAX_TAG_LEN*INITIAL_TAG_CAPACITY, GFP_KERNEL);
	if(!tags) {
		kfree(head);
		return NULL;
	}

	/* Allocate lookup table */
	lookup = kmalloc(sizeof(struct tag_lookup_array), GFP_KERNEL);
	if(!lookup) {
		kfree(head);
		kfree(tags);
		return NULL;
	}

	/* Initialize lookup */
	lookup->capacity = INITIAL_TAG_CAPACITY;
	lookup->free_list = NULL;
	lookup->tag = tags;

	/* Initialize tag names */
	for(i=0; i < lookup->capacity; i++) {
		lookup->tag[i*MAX_TAG_LEN] = '\0';
	}

	/* Initialize head */
	head->lookup_table = lookup;
	head->num_tags = 0;
	for(i=0; i < 1<<NUM_HASH_BITS; i++) {
		head->table[i] = NULL;
	}

	return head;
}

void destroy_table(struct hash_table *table) {
	int i, count;
	struct free_list_entry *curr;
	struct inode_entry *head = NULL;
	if (!table)
		return;
	count = 0;
	for (i = 0; i < table->lookup_table->capacity && count < table->num_tags; i++) {
		char *tag = &(table->lookup_table->tag[i * MAX_TAG_LEN]);
		if (*tag != '\0') {
			int j;
			struct table_element *e = get_inodes(table, tag);
			struct inode_entry **entries = set_to_array(get_inodes(table, tag));
			int size = element_size(e);	
			for (j = 0; j < size; j++) {
				struct inode_entry *curr = head;
				int found = 0;
				while(curr) {
					if (curr->ino == entries[j]->ino) {
						found = 1;
						break;
					}
					curr = curr->next;
				}
				if (found)
					continue;
				entries[j]->next = head;
				head = entries[j]->next;
			}
			count++;
		}
	}
	while(head) {
		struct inode_entry *temp = head;
		head = head->next;
		kfree(temp);
	}
	curr = table->lookup_table->free_list;
	while(curr) {
		struct free_list_entry *temp = curr;
		curr = curr->next;
		kfree(temp);
	}
	kfree(table->lookup_table->tag);
	count = 0;
	for(i = 0; i < 1 << NUM_HASH_BITS && count < table->num_tags; i++) {
		struct tag_node *curr = table->table[i];
		while(curr) {
			struct tag_node *temp = curr;
			curr = curr->next;
			delete_element(temp->e);
			kfree(temp);
			count++;
		}
	}
	kfree(table->lookup_table);
	kfree(table);
}

unsigned int get_num_tags(struct hash_table * table) {
	return table->num_tags;
}

const char *get_tag(struct hash_table *table, int id) {
	return table->lookup_table->tag + id * MAX_TAG_LEN;
}

int get_tagid(struct hash_table *table, const char *tag) {
	struct tag_node *n = find_node(table, tag);
	if (!n)
		return -1;
	return n->tag_id;
}

int change_tag(struct hash_table *table, char *tag1, char *tag2) {
	struct tag_node *node, *prev;
	unsigned long hash1, hash2;
	if (find_node(table, tag2))
		return -EINVAL;
	hash1 = hash_tag(tag1);
	prev = NULL;
	node = table->table[hash1];
	while(node != NULL && strncmp(node->tag, tag1, MAX_TAG_LEN) != 0) {
		prev = node;
		node = node->next;
	}
	if (!node)
		return -EINVAL;
	if (prev)
		prev->next = node->next;
	else
		table->table[hash1] = node->next;
	hash2 = hash_tag(tag2);
	node->next = table->table[hash2];
	strlcpy(node->tag, tag2, MAX_TAG_LEN);
	strlcpy(&(table->lookup_table->tag[node->tag_id*MAX_TAG_LEN]), tag2, MAX_TAG_LEN);
	table->table[hash2] = node;
	return 0;
}

struct file *file_open(const char *path, int flags, int rights) {
	struct file* filp = NULL;
	mm_segment_t oldfs;

	oldfs = get_fs();
	set_fs(get_ds());
	filp = filp_open(path, flags, rights);
	set_fs(oldfs);
	if (IS_ERR(filp)) {
		return NULL;
	}
	return filp;
}

void file_close(struct file *file) {
	filp_close(file, NULL);
}

int file_read(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size) {
	mm_segment_t oldfs;
	int ret;

	oldfs = get_fs();
	set_fs(get_ds());

	ret = vfs_read(file, data, size, &offset);

	set_fs(oldfs);
	return ret;
}

int file_write(struct file* file, unsigned long long offset, unsigned char* data, unsigned int size) {
	mm_segment_t oldfs;
	int ret;

	oldfs = get_fs();
	set_fs(get_ds());

	ret = vfs_write(file, data, size, &offset);

	set_fs(oldfs);
	return ret;
}

int file_sync(struct file* file) {
	vfs_fsync(file, 0);
	return 0;
}

void write_table(struct hash_table *table, char *filename) {
	int i, count, entry_count, offset;
	char cint[sizeof(int)];
	char clong[sizeof(long)];
	struct inode_entry *head = NULL;
	struct inode_entry *curr;
	struct file *file = file_open(filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (!file) {
		//Panic
	}
	count = entry_count = 0;
	// Get all entries and then write them.
	for (i = 0; i < table->lookup_table->capacity && count < table->num_tags; i++) {
		char *tag = &(table->lookup_table->tag[i * MAX_TAG_LEN]);
		if (*tag != '\0') {
			int j;
			struct table_element *e = get_inodes(table, tag);
			struct inode_entry **entries = set_to_array(get_inodes(table, tag));
			int size = element_size(e);	
			for (j = 0; j < size; j++) {
				int found = 0;
				curr = head;
				while(curr) {
					if (curr->ino == entries[j]->ino) {
						found = 1;
						break;
					}
					curr = curr->next;
				}
				if (found)
					continue;
				entries[j]->next = head;
				head = entries[j]->next;
				entry_count++;
			}
			count++;
		}
	}
	//Write entries to file
	//TODO: 1. Write number of entries
	//      2. Go through list, write index + entry info, i.e. ino and filename
	offset = 0;
	INT_TO_CHAR(entry_count, cint, i);
	file_write(file, offset, cint, sizeof(int));
	offset += sizeof(int);
	curr = head;
	while(curr) {
		INT_TO_CHAR(curr->ino, clong, i);
		file_write(file, offset, clong, sizeof(unsigned long));
		offset += sizeof(unsigned long);
		file_write(file, offset, curr->filename, MAX_FILENAME_LEN);
		offset += MAX_FILENAME_LEN;
		curr = curr->next;
	}

	//Write capacity + number of tags
	INT_TO_CHAR(table->lookup_table->capacity, cint, i);
	file_write(file, offset, cint, sizeof(int));
	offset += sizeof(int);
	INT_TO_CHAR(table->num_tags, cint, i);
	file_write(file, offset, cint, sizeof(int));
	offset += sizeof(int);
	for (i = 0; i < table->lookup_table->capacity && count < table->num_tags; i++) {
		char *tag = &(table->lookup_table->tag[i * MAX_TAG_LEN]);
		int k;
		file_write(file, offset, tag, MAX_TAG_LEN);
		offset += MAX_TAG_LEN;
		//TODO: 1. Write tag id
		//      2. Write tag name
		//	3. Write number of files with that tag
		//	4. Write entry indices
		if (*tag != '\0') {
			int j;
			struct table_element *e = get_inodes(table, tag);
			struct inode_entry **entries = set_to_array(get_inodes(table, tag));
			int size = element_size(e);
			INT_TO_CHAR(size, cint, k);
			file_write(file, offset, cint, sizeof(int));
			offset += sizeof(int);
			for (j = 0; j < size; j++) {
				int index = 0;
				curr = head;
				while(curr) {
					if (curr->ino == entries[j]->ino) {
						INT_TO_CHAR(entry_count - index, cint, k);
						file_write(file, offset, cint, sizeof(int));
						offset += sizeof(int);
						break;
					}
					index++;
					curr = curr->next;
				}
			}
		} else {
			INT_TO_CHAR(0, cint, k);
			file_write(file, offset, cint, sizeof(int));
			offset += sizeof(int);
		}
	}
	file_sync(file);
	file_close(file);
}

int read_table(struct hash_table *table, char *filename) {
	int i, entry_count = 0, offset, ret = 0, capacity = 0, num_tags = 0;
	struct inode_entry *head = NULL, *curr;
	char cint[sizeof(int)], clong[sizeof(long)];
	struct file *file = file_open(filename, O_RDONLY, 0);
	if (!file) {
		return -ENOMEM;
	}
	offset = 0;
	file_read(file,	offset, cint, sizeof(int));
	offset += sizeof(int);
	CHAR_TO_INT(cint, entry_count, i);
	// Get the entries
	for(i = 0; i < entry_count; i++) {
		int j;
		struct inode_entry *entry = kmalloc(sizeof(struct inode_entry), GFP_KERNEL);
		if (!entry) {
			ret = -ENOMEM;
			goto fail;
		}
		file_read(file, offset, clong, sizeof(long));
		offset += sizeof(long);
		CHAR_TO_INT(clong, entry->ino, j);
		file_read(file, offset, entry->filename, MAX_FILENAME_LEN);
		offset += MAX_FILENAME_LEN;
		entry->filename[MAX_FILENAME_LEN] = '\0';
		entry->next = head;
		entry->count = 0;
		head = entry;
	}
	file_read(file, offset, cint, sizeof(int));
	offset += sizeof(int);
	CHAR_TO_INT(cint, capacity, i);
	file_read(file, offset, cint, sizeof(int));
	offset += sizeof(int);
	CHAR_TO_INT(cint, num_tags, i);
	table->lookup_table = kmalloc(sizeof(struct tag_lookup_array), GFP_KERNEL);
	if (!table->lookup_table)
		goto fail;
	table->lookup_table->free_list = NULL;
	table->lookup_table->capacity = capacity;
	table->lookup_table->tag = kmalloc(MAX_TAG_LEN * capacity, GFP_KERNEL);
	if (!table->lookup_table->tag)
		goto fail;
	table->num_tags = 0;
	for(i = 0; i < capacity && table->num_tags < num_tags; i++) {
		int size, j;
		unsigned long hash;
		struct tag_node *node;
		file_read(file, offset, &(table->lookup_table->tag[i * MAX_TAG_LEN]), MAX_TAG_LEN);
		offset += MAX_TAG_LEN;
		file_read(file, offset, cint, sizeof(int));
		offset += sizeof(int);
		CHAR_TO_INT(cint, size, j);
		if (table->lookup_table->tag[i * MAX_TAG_LEN] == '\0') {
			struct free_list_entry *f = kmalloc(sizeof(struct free_list_entry), GFP_KERNEL);
			if (!f)
				goto fail;
			f->free_index = i;
			f->next = table->lookup_table->free_list;
			table->lookup_table->free_list = f;
			continue;	
		}
		node = kmalloc(sizeof(struct tag_node), GFP_KERNEL);
		if (!node)
			goto fail;
		node->e = new_element();
		if (!node->e)
			goto fail;
		node->tag_id = i;
		table->num_tags++;
		strlcpy(node->tag, &(table->lookup_table->tag[i * MAX_TAG_LEN]), MAX_TAG_LEN);
		hash = hash_tag(node->tag);
		node->next = table->table[hash];
		table->table[hash] = node;
		for (j = 0; j < size; j++) {
			int index, k;
			file_read(file, offset, cint, sizeof(int));
			offset += sizeof(int);
			curr = head;
			while(index > k) {
				curr = curr->next;
				k++;
			}
			if (insert_entry(node->e, curr))
				goto fail;
		}
		
	}
	file_close(file);
	return 0;
fail:
	if (head) {
		while(head) {
			curr = head;
			head = head->next;
			kfree(curr);
		}
	}
	if (table->lookup_table) {
		if (table->lookup_table->tag)
			kfree(table->lookup_table->tag);
		while(table->lookup_table->free_list) {
			struct free_list_entry *ftemp = table->lookup_table->free_list;
			ftemp = table->lookup_table->free_list;
			table->lookup_table->free_list = table->lookup_table->free_list->next;
			kfree(ftemp);
		}
		kfree(table->lookup_table);
	}
	if (table->num_tags > 0) {
		int tag_count = 0;
		for(i = 0; i < 1 << NUM_HASH_BITS && tag_count < table->num_tags; i++) {
			struct tag_node *tag_curr = table->table[i];
			while(tag_curr) {
				struct tag_node *temp = tag_curr;
				tag_curr = tag_curr->next;
				delete_element(temp->e);
				kfree(temp);
				tag_count++;
			}
		}
	}
	file_close(file);
	return -1;
}
