#ifndef _TABLE_H
#define _TABLE_H

#include "table_element.h"

struct hash_table;

struct hash_table *create_table(void);
struct table_element *get_inodes(struct hash_table *, char *);
int insert(struct hash_table *, const char *, const struct inode_entry *);
void remove(struct hash_table *, const char *, unsigned long);


#endif
