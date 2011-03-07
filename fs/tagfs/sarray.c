/** @file sarray.c
 *  @brief Sorted array implementation of the table element
 *  @author William Wang
 *  @author Tim Shields
 *  @author Ping-Yao Tseng
 *
 *  A more efficient array based implementation of the table element.
 *  Same running time as the unsorted implementation for certain
 *  operations.  More efficient union and set operations can be performed.
 */

#include "table_element.h"
#include <linux/slab.h>
#include <linux/string.h>

static const unsigned int StartCapacity = 10;

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

/** @brief inserts to the end of the array
 *
 *  Is a helper function for the union and intersect operations, which do
 *  not require the insertion sort.
 */
static int insert_end(struct table_element *e, const struct inode_entry *entry) 
{
	if (e->count == e->capacity) {
		struct inode_entry *new_ptr = krealloc(e->entries, e->capacity << 1, GFP_KERNEL);
		if (!new_ptr)
			return NO_MEMORY;
		e->capacity <<= 1;
	}
	memcpy(&e->entries[e->count++], entry, sizeof(struct inode_entry));
	return 0;
}

int insert_entry(struct table_element *e, const struct inode_entry *entry) 
{
	unsigned int i, index;
	if (!e)
		return INVALID_ELEMENT;
	if (e->count == e->capacity) {
		struct inode_entry *new_ptr = krealloc(e->entries, e->capacity << 1, GFP_KERNEL);
		if (!new_ptr)
			return NO_MEMORY;
		e->capacity <<= 1;
	}
	for (i = 0 ; i < e->count ; i++) {
		if (entry->ino->i_ino == e->entries[i].ino->i_ino)
			return DUPLICATE;
		if (entry->ino->i_ino < e->entries[i].ino->i_ino)
			break;
	}
	index = i;
	for (i = e->count; i > index; i--)
		memcpy(&e->entries[i], &e->entries[i-1], sizeof(struct inode_entry));
	memcpy(&e->entries[index], entry, sizeof(struct inode_entry));
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
	unsigned int i,j;
	struct table_element *result = NULL;
	if (e1 == NULL || e2 == NULL)
		goto fail;
	result = new_element();
	if (!result)
		goto fail;
	i = j = 0;
	/* Essentially does a merge while removing duplicates */
	while(1) {
		if (i >= e1->count && j >= e2->count)
			break;
		else if (i >= e1->count) {
			for(; j < e2->count; j++) {
				if (insert_end(result, &e2->entries[j]) == NO_MEMORY)
					goto fail;
			}
			break;
		}
		else if (j >= e2->count) {
			for(; i < e1->count; i++) {
				if (insert_end(result, &e1->entries[i]) == NO_MEMORY)
					goto fail;
			}
			break;
		}
		else {
			if (e1->entries[i].ino->i_ino < e2->entries[j].ino->i_ino) {
				if (insert_end(result, &e1->entries[i]) == NO_MEMORY)
					goto fail;
				i++;
			} 
			else if (e1->entries[i].ino->i_ino == e2->entries[j].ino->i_ino) {
				if (insert_end(result, &e1->entries[i]) == NO_MEMORY)
					goto fail;
				i++;
				j++;
			}
			else {
				if (insert_end(result, &e2->entries[j]) == NO_MEMORY)
					goto fail;
				j++;
			}
		}
	}
	return result;
fail:
	if (result)
		delete_element(result);
	return NULL;
}

struct table_element *set_intersect(struct table_element *e1, struct table_element *e2) {
	unsigned int i, j;
	struct table_element *result = new_element();
	if (!result)
		return result;
	i = j = 0;
	/* Essentially does a merge which only counts duplicates */
	while(i < e1->count && j < e2->count) {
		if (e1->entries[i].ino->i_ino < e2->entries[j].ino->i_ino)
			i++;
		else if (e1->entries[i].ino->i_ino > e2->entries[j].ino->i_ino)
			j++;
		else {
			if (insert_end(result, &e2->entries[j]) == NO_MEMORY) {
				delete_element(result);
				return NULL;
			}
			i++;
			j++;
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
