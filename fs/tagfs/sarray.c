/** @file sarray.c
 *  @brief Sorted array implementation of the table element
 *  @author William Wang
 *  @author Tim Shields
 *  @author Ping-Yao Tseng
 *
 *  A sorted array based implementation of the table element.
 *  Same running time as the unsorted implementation for certain
 *  operations but more efficient set operations can be performed.
 */

#include "table_element.h"
#include <linux/slab.h>
#include <linux/string.h>

static const unsigned int StartCapacity = 10;

struct table_element {
	struct inode_entry **entries;
	unsigned int count;
	unsigned int capacity;
	int readonly;
};

struct table_element *new_element() 
{
	struct table_element *e = kmalloc(sizeof(struct table_element), GFP_KERNEL);
	if (!e)
		return e;
	e->entries = kmalloc(sizeof(struct inode_entry *) * StartCapacity, GFP_KERNEL);
	if (!e->entries) {
		kfree(e);
		return NULL;
	}
	e->count = 0;
	e->capacity = StartCapacity;
	e->readonly = 0;
	return e;
}

void delete_element(struct table_element *e) {
  if (e) {
  	kfree(e->entries);
	kfree(e);
  }
}

struct table_element *copy_element(struct table_element *input) {
	struct table_element* copy;
	int i;
	if(!input)
		return NULL;
	copy = new_element();
	if(!copy)
		return NULL;
	for(i = 0; i < input->count; i++) {
		insert_entry(copy, input->entries[i]);
	}
	return copy;
}

/** @brief inserts to the end of the array
 *
 *  Is a helper function for the union and intersect operations, which do
 *  not require the insertion sort.
 */
static int insert_end(struct table_element *e, struct inode_entry *entry) 
{
	if (e->count == e->capacity) {
		struct inode_entry **new_ptr = krealloc(e->entries, sizeof(void *) * e->capacity << 1, GFP_KERNEL);
		if (!new_ptr)
			return NO_MEMORY;
		e->capacity <<= 1;
		e->entries = new_ptr;
	}
	e->entries[e->count++] = entry;
	return 0;
}

int insert_entry(struct table_element *e, struct inode_entry *entry) 
{
	unsigned int i, index;
	if (!e)
		return INVALID_ELEMENT;
	if (e->readonly)
		return READ_ONLY;
	if (e->count == e->capacity) {
		struct inode_entry **new_ptr = krealloc(e->entries, sizeof(void *) * e->capacity << 1, GFP_KERNEL);
		if (!new_ptr)
			return NO_MEMORY;
		e->capacity <<= 1;
		e->entries = new_ptr;
	}
	for (i = 0 ; i < e->count ; ++i) {
		if (entry->ino == e->entries[i]->ino)
			return DUPLICATE;
		if (entry->ino < e->entries[i]->ino)
			break;
	}
	index = i;
	for (i = e->count; i > index; i--)
		e->entries[i] = e->entries[i-1];
	e->entries[index] = entry;
	e->count++;
	entry->count++;
	return 0;
}

int remove_entry(struct table_element *e, unsigned long ino) {
	unsigned int i;
	if (!e)
		return INVALID_ELEMENT;
	if (e->readonly)
		return READ_ONLY;
	for (i = 0; i < e->count; i++) {
		if (e->entries[i]->ino == ino) {
			e->entries[i]->count--;
			if (e->entries[i]->count == 0)
				kfree(e->entries[i]);
			for(; i < e->count - 1; i++)
				e->entries[i] = e->entries[i+1];
			e->count--;
			break;;
		}
	}
	return 0;
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
			for(; j < e2->count; ++j) {
				if (insert_end(result, e2->entries[j]) == NO_MEMORY)
					goto fail;
			}
			break;
		}
		else if (j >= e2->count) {
			for(; i < e1->count; ++i) {
				if (insert_end(result, e1->entries[i]) == NO_MEMORY)
					goto fail;
			}
			break;
		}
		else {
			if (e1->entries[i]->ino < e2->entries[j]->ino) {
				if (insert_end(result, e1->entries[i]) == NO_MEMORY)
					goto fail;
				i++;
			} 
			else if (e1->entries[i]->ino == e2->entries[j]->ino) {
				if (insert_end(result, e1->entries[i]) == NO_MEMORY)
					goto fail;
				i++;
				j++;
			}
			else {
				if (insert_end(result, e2->entries[j]) == NO_MEMORY)
					goto fail;
				j++;
			}
		}
	}
	result->readonly = 1;
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
		return NULL;
	i = j = 0;
	/* Essentially does a merge which only counts duplicates */
	while(i < e1->count && j < e2->count) {
		if (e1->entries[i]->ino < e2->entries[j]->ino)
			i++;
		else if (e1->entries[i]->ino > e2->entries[j]->ino)
			j++;
		else {
			if (insert_end(result, e2->entries[j]) == NO_MEMORY) {
				delete_element(result);
				return NULL;
			}
			i++;
			j++;
		}

	}
	result->readonly = 1;
	return result;
}

struct inode_entry **set_to_array(struct table_element *e) {
	return e->entries;
}

unsigned int element_size(struct table_element *e) {
	return e->count;
}

struct inode_entry *find_entry(const struct table_element *e, unsigned long ino) {
	int lo = 0;
	int hi = e->count - 1;
	int mid = (lo + hi) / 2;
	while(lo <= hi) {
		if (e->entries[mid]->ino == ino)
			return e->entries[mid];
		if (e->entries[mid]->ino > ino)
			hi = mid - 1;
		else
			lo = mid + 1;
		mid = (lo + hi) / 2;
	}
	return NULL;
}
