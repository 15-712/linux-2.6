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

#include "table_element.h"
#include <linux/hash.h>
#include <linux/slab.h>

#define MAX_TAG_LEN	255
#define NUM_HASH_BITS	12

struct hash_table {
	struct tag_node *table[1<<NUM_HASH_BITS];
	struct tag_name_id *head;
	unsigned int num_tags;
};

/* Struct for the buckets of the hash table.
 * collisions handled by linked list */
struct tag_node {
	char tag[MAX_TAG_LEN];
	struct table_element *e;
	int tag_id;
	struct tag_node *next;
};

/* Linked list that allows tags to be looked up by id */
struct tag_name_id {
	int id;
	char tag[MAX_TAG_LEN];
	struct tag_name_id *next;
};

enum tag_node_error {
	INVALID_NODE = -255,
	TABLE_ELEMENT_ERROR,
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

/* Assigns and ID to a tag and creates a new entry in the lookup table */
struct tag_name_id *create_new_id_lookup(struct tag_name_id *head, const char *tag)
{
	struct tag_name_id *t; 
	t = kmalloc(sizeof(struct tag_name_id), GFP_KERNEL);
	if (!t)
		return t;
	if(head == NULL) {
		strlcpy(t->tag, tag, MAX_TAG_LEN);
		t->id = 1;
	} else if(head->next == NULL) {
		strlcpy(t->tag, tag, MAX_TAG_LEN);
		t->id = head->id+1;
		head->next = t;
	} else if(head->next->id > head->id+1) {
		strlcpy(t->tag, tag, MAX_TAG_LEN);
		t->id = head->id+1;
		t->next = head->next;
		head->next = t;
	}
	return t;
}

/* Searches the hash table for a node with a given tag 
 * Returns NULL if entry does not exist. */
struct tag_node *find_node(struct hash_table *table, const char *tag) 
{
	struct tag_node *node;
	if(!table)
		return NULL;
	node = table->table[hash_tag(tag)];
	while(node != NULL && strncmp(node->tag, tag, MAX_TAG_LEN) != 0) {
		node = node->next;
	}
	return node;
}

/* Adds a node to the hash table. */
int add_node(struct hash_table *table, struct tag_node *new_node) 
{
	struct tag_node *node;
	if(!table)
		return INVALID_NODE;
	node = table->table[hash_tag(new_node->tag)];
	if(likely(node == NULL)) {
		node = new_node;
	} else {
		while(node->next != NULL) {
			node = node->next;
		}
		node->next = new_node;
	}
	return 0;
	
}

/* Removes an inode from the specified tag. This is not fully implemented. */
void remove(struct hash_table *table, const char *tag, unsigned long inode_num) {
	struct tag_node *node;
	node = find_node(table, tag);
	if(node) {
		remove_entry(node->e, inode_num);
		if(size(node->e) == 0) {
			delete_element(node->e);
			kfree(node);
			table->num_tags--;
		}
	}
}

/* Inserts an inode in the given tag entry. Creates tag if necessary. */
int insert(struct hash_table *table, const char *tag, const struct inode_entry *i)
{
	struct tag_node *node;
	struct tag_name_id *tag_id;
	int e;
	node = find_node(table, tag);
	if(!node) {
		/* Create empty table element */
		node = kmalloc(sizeof(struct tag_node), GFP_KERNEL);
		if(!node) {
			e = -ENOMEM;
			goto fail;
		}
		node->next = NULL;
		node->e = new_element();
		if (!node->e) {
			e = -ENOMEM;
			goto fail;
		}
		strlcpy(node->tag, tag, MAX_TAG_LEN);
		tag_id = create_new_id_lookup(table->head, tag);
		if((e = add_node(table, node)) < 0) {
			goto fail;
		}
		table->num_tags++;
	}
	if (insert_entry(node->e, i) < 0)
		return -ENOMEM;
	return 0;
fail:
	if (node->e)
		delete_element(node->e);
	if (node)
		kfree(node);
	return -ENOMEM; 
}

/* Returns the table_element structure of inodes associated with the specified tag.  */
struct table_element * get_inodes(struct hash_table *table, char* tag) 
{
	struct tag_node *n = find_node(table, tag);
	if(!n)
		return NULL;
	return n->e;
}

/* Creates the hash table */
struct hash_table * create_table(void)
{
	struct hash_table *head;
	head = kmalloc(sizeof(struct hash_table),  GFP_KERNEL);
	return head;
}

