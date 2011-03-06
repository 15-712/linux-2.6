#ifndef _TABLE_ELEMENT_H
#define _TABLE_ELEMENT_H

#include <linux/fs.h>

#define MAX_FILENAME_LEN  255

struct table_element;

struct inode_entry {
	struct inode *ino;
	char filename[MAX_FILENAME_LEN];
	unsigned long long hash;
};


int insert_entry(struct table_element *, const struct inode_entry *);
void remove_entry(struct table_element *, unsigned long);
struct table_element *set_union(struct table_element *, struct table_element *);
struct table_element *set_intersect(struct table_element *, struct table_element *);
struct table_element *new_element(void);
void delete_element(struct table_element *);
const struct inode_entry *set_to_array(struct table_element *);
unsigned int size(struct table_element *);

enum table_element_error {
	DUPLICATE = -255,
	NO_MEMORY,
	INVALID_ELEMENT,
};
#endif 
