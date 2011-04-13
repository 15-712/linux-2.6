#ifndef _TABLE_H
#define _TABLE_H

#include "table_element.h"

#define MAX_TAG_LEN 255

extern struct hash_table *table;
struct hash_table;

struct hash_table *create_table(void);
void destroy_table(struct hash_table *);
struct table_element *get_inodes(struct hash_table *, const char *);
const char *get_tag(struct hash_table *, int);
int get_tagid(struct hash_table *, const char *);
int change_tag(struct hash_table *, char *, char *);
int table_insert(struct hash_table *, const char *, struct inode_entry *);
int table_remove(struct hash_table *, const char *, unsigned long);


#endif
