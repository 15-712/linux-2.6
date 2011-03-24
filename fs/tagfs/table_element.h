/** @file table_element.h
 *  @brief prototypes for the table_element functions
 *
 *  Used by the hash table to store inodes efficiently such
 *  that one can perform set operations in order to find files
 *  based on tags.
 * 
 */


#ifndef _TABLE_ELEMENT_H
#define _TABLE_ELEMENT_H

#include <linux/fs.h>

#define MAX_FILENAME_LEN  255

struct table_element;

struct inode_entry {
	unsigned long ino;
	char filename[MAX_FILENAME_LEN+1];
	unsigned int count;
};


/* Insert an inode entry into the table_element */
int insert_entry(struct table_element *, struct inode_entry *);
/* Remove an entry from a table_element based on inode number */
void remove_entry(struct table_element *, unsigned long);
/* Returns a table_element representing the union of the entries of two table_elements */
struct table_element *set_union(struct table_element *, struct table_element *);
/* Returns a table element representing the intersection of the entries of two table elements */
struct table_element *set_intersect(struct table_element *, struct table_element *);
/* Creates a new table_element and initializes it*/
struct table_element *new_element(void);
/* Frees all memory associated with the table_element */
void delete_element(struct table_element *);
/* Returns the set of entries as an array */
struct inode_entry **set_to_array(struct table_element *);
/* Returns the number of entrices in the table element */
unsigned int size(struct table_element *);

struct inode_entry *find_entry(const struct table_element *, unsigned long);

/* The set of table_element_error values */
enum table_element_error {
	DUPLICATE = -255,
	NO_MEMORY,
	INVALID_ELEMENT,
	READ_ONLY,
};
#endif 
