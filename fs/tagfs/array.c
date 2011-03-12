/** @file array.c
 *  @brief Unsorted array implementation of the table element
 *  @author William Wang
 *  @author Tim Shields
 *  @author Ping-Yao Tseng
 *
 *  This is the basic unsorted array implementation of the table element
 *  All operations essentially require linear time to run and is probably
 *  really slow. Baseline for performance tests
 */


#include "table_element.h"
#include <linux/slab.h>
#include <linux/string.h>

static const unsigned int StartCapacity = 16;

struct table_element {
	struct inode_entry *entries;
	unsigned int count;
	unsigned int capacity;	
};

struct table_element *new_element() 
{
	struct table_element *e = kmalloc(sizeof(struct table_element), GFP_KERNEL);
	if (!e)
		return e;
	e->count = 0;
	e->capacity = StartCapacity;
	e->entries = kmalloc(sizeof(struct inode_entry) * e->capacity, GFP_KERNEL);
	if (!e->entries) {
		kfree(e);
		return NULL;
	}
	return e;
}

void delete_element(struct table_element *e) {
  if (e) {
  	kfree(e->entries);
	kfree(e);
  }
}

int insert_entry(struct table_element *e, const struct inode_entry *entry) 
{
	unsigned int i;
	if (!e)
		return INVALID_ELEMENT;
        /* If full, double the size of the array */
	if (e->count == e->capacity) {
		struct inode_entry *new_ptr = krealloc(e->entries, e->capacity << 1, GFP_KERNEL);
		if (!new_ptr)
			return NO_MEMORY;
		e->capacity <<= 1;
	}
	/* Check for duplicate entries */
	for (i = 0 ; i < e->count ; i++) {
		if (entry->ino->i_ino == e->entries[i].ino->i_ino)
			return DUPLICATE;
	}
	memcpy(&e->entries[e->count++], entry, sizeof(struct inode_entry));
	return 0;
}

void remove_entry(struct table_element *e, unsigned long ino) {
	unsigned int i;
	if (!e)
		return;
	for (i = 0; i < e->count; i++) {
		if (e->entries[i].ino->i_ino == ino) {
			for(; i < e->count - 1; i++)
				memcpy(&e->entries[i], &e->entries[i+1], sizeof(struct inode_entry));
			e->count--;
			return;
		}
	}
}

struct table_element *set_union(struct table_element *e1, struct table_element *e2) {
	unsigned int i;
	struct table_element *result = new_element();
	if (!result)
		return result;
	for (i = 0; i < e1->count; i++)  {
		if (insert_entry(result, &e1->entries[i]) == NO_MEMORY) {
			delete_element(result);
			return NULL;
		}
	}
	for (i = 0; i < e2->count; i++)  {
		if (insert_entry(result, &e2->entries[i]) == NO_MEMORY) {
			delete_element(result);
			return NULL;
		}
	}
	return result;
}

struct table_element *set_intersect(struct table_element *e1, struct table_element *e2) {
	unsigned int i;
	struct table_element *result = new_element();
	if (!result)
		return result;
	for (i = 0; i < e1->count; i++) {
		unsigned int j;
		for (j = 0; j < e2->count; j++) {
			if (e1->entries[i].ino->i_ino == e2->entries[j].ino->i_ino) {
				if (insert_entry(result, &e1->entries[i]) == NO_MEMORY) {
					delete_element(result);
					return NULL;
				}
			}
		}
	}
	return result;
}

const struct inode_entry *set_to_array(struct table_element *e) {
	return e->entries;
}

unsigned int size(struct table_element *e) {
	return e->count;
}
