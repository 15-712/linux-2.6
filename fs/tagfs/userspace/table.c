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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define GFP_KERNEL 0
#define kmalloc(S, O) malloc(S)
#define kfree(P) free(P)
#define krealloc(P, S, O) realloc(P, S)
#define unlikely(X) X
#define likely(X) X
#define strlcpy(X, Y, S) strncpy(X, Y, S)

#define GOLDEN_RATIO_PRIME_32 0x9e370001UL
#define GOLDEN_RATIO_PRIME GOLDEN_RATIO_PRIME_32
#define BITS_PER_LONG 32

#define MAX_TAG_LEN	255
#define NUM_HASH_BITS	1
#define INITIAL_TAG_CAPACITY	2

static inline unsigned int hash_long(unsigned int val, unsigned int bits)
{
	/* On some cpus multiply is faster, on others gcc will do shifts */
	unsigned int hash = val * GOLDEN_RATIO_PRIME_32;

	/* High bits are more random, so use them. */
	return hash >> (32 - bits);
}


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
	table->lookup_table->tag[index*MAX_TAG_LEN] = '\0';

	free_list = table->lookup_table->free_list;
	new_entry = kmalloc(sizeof(struct free_list_entry), GFP_KERNEL);
	if(!new_entry) {
		return NO_MEMORY;
	}
	new_entry->free_index = index;
	new_entry->next = NULL;
	if(!free_list) {
		table->lookup_table->free_list = new_entry;
	} else {
		while(free_list->next) {
			free_list = free_list->next;
		}
		free_list->next = new_entry;
	}
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
	if(!table)
		return NULL;
	node = table->table[hash_tag(tag)];
	while(node != NULL && strncmp(node->tag, tag, MAX_TAG_LEN) != 0) {
		node = node->next;
	}
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
		remove_entry(node->e, inode_num);
		if(size(node->e) == 0) {

			/* decrement tag count */
			table->num_tags--;

			/* remove tag from lookup table */
			remove_tag(table, node->tag_id);

			/* remove tag from hash table */
			delete_element(node->e);
			remove_node(table, tag);
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
	node = find_node(table, tag);
	if(!node) {
		/* Create empty table element */
		node = kmalloc(sizeof(struct tag_node), GFP_KERNEL);
		if(!node)
			return NO_MEMORY;
		node->next = NULL;
		tag_id = create_new_tag(table, tag);
		if(tag_id == NO_MEMORY) {
			kfree(node);
			return NO_MEMORY;
		}
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

	return insert_entry(node->e, i);
}

/* Returns the table_element structure of inodes associated with the specified tag.  */
struct table_element * get_inodes(struct hash_table *table, const char* tag) 
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
	int i;
	struct free_list_entry *curr;
	if (!table)
		return;
	curr = table->lookup_table->free_list;
	while(curr) {
		struct free_list_entry *temp = curr;
		curr = curr->next;
		kfree(temp);
	}
	kfree(table->lookup_table->tag);
	for(i = 0; i < 1 << NUM_HASH_BITS; i++) {
		struct tag_node *curr = table->table[i];
		while(curr) {
			struct tag_node *temp = curr;
			curr = curr->next;
			delete_element(temp->e);
			kfree(temp);
		}
	}
	kfree(table->lookup_table);
	kfree(table);
}

const char *get_tag(struct hash_table *table, int id) {
	return table->lookup_table->tag + id * MAX_TAG_LEN;
}
