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
	t = NULL;
	if(head == NULL) {
		t = kmalloc(sizeof(struct tag_name_id), GFP_KERNEL);
		strlcpy(t->tag, tag, MAX_TAG_LEN);
		t->id = 1;
	} else if(head->next == NULL) {
		t = kmalloc(sizeof(struct tag_name_id), GFP_KERNEL);
		strlcpy(t->tag, tag, MAX_TAG_LEN);
		t->id = head->id+1;
		head->next = t;
	} else if(head->next->id > head->id+1) {
		t = kmalloc(sizeof(struct tag_name_id), GFP_KERNEL);
		strlcpy(t->tag, tag, MAX_TAG_LEN);
		t->id = head->id+1;
		t->next = head->next;
		head->next = t;
	}
	return t;
}

/* Searches the hash table for a node with a given tag 
 * Returns NULL if entry does not exist. */
struct tag_node *find_node(struct tag_node **head, const char *tag) 
{
	struct tag_node *node;
	if(!head)
		return NULL;
	node = head[hash_tag(tag)];
	while(node != NULL && strncmp(node->tag, tag, MAX_TAG_LEN) != 0) {
		node = node->next;
	}
	return node;
}

/* Adds a node to the hash table. */
int add_node(struct tag_node **head, struct tag_node *new_node) 
{
	struct tag_node *node;
	if(!head)
		return INVALID_NODE;
	node = head[hash_tag(new_node->tag)];
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
void remove(struct tag_node **head, const char *tag, unsigned long inode_num) {
	struct tag_node *node;
	node = find_node(head, tag);
	if(node) {
		remove_entry(node->e, inode_num);
		/* TODO: Clear this pointer if there are no more table entries.
		*  This could be done by modifying remove_entry to return count. */
	}
}

/* Inserts an inode in the given tag entry. Creates tag if necessary. */
int insert(struct tag_node **head, const char *tag, const struct inode_entry *i)
{
	struct tag_node *node;
	int e;
	node = find_node(head, tag);
	if(!node) {
		/* Create empty table element */
		node = kmalloc(sizeof(struct tag_node), GFP_KERNEL);
		if(!node)
			return -ENOMEM;
		node->next = NULL;
		node->e = new_element();
		strlcpy(node->tag, tag, MAX_TAG_LEN);
		/* TODO: Add tag id */
		if((e = add_node(head, node)) < 0) {
			delete_element(node->e);
			kfree(node->tag);
			kfree(node);
			return e;
		}
	}

	insert_entry(node->e, i);
	return 0;
}

/* Returns the table_element structure of inodes associated with the specified tag.  */
struct table_element * get_inodes(struct tag_node **head, char* tag) 
{
	struct tag_node *n = find_node(head, tag);
	if(!n)
		return NULL;
	return n->e;
}

/* Creates the hash table */
struct tag_node * create_table(void)
{
	struct tag_node *head;
	head = kmalloc(sizeof(head) * (1<<NUM_HASH_BITS), GFP_KERNEL);
	return head;
}

