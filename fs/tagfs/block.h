#ifndef BLOCK_H
#define BLOCK_H

#define MAX_NUM_TAGS 32

int allocate_block(unsigned long);
int *get_tagids(unsigned long, int *);
int add_tagid(unsigned long, int);
void remove_tagid(unsigned long, int);
void deallocate_block(unsigned long);
void mv_block(unsigned long, unsigned long);
void deallocate_all(void);

#endif /* BLOCK_H */
