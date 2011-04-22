#include <linux/slab.h>

#include "block.h"

struct block {
	unsigned long ino;
	int tag[MAX_NUM_TAGS];
	int num_tags;
	struct block *next;
};

struct block *head = NULL;

int allocate_block(unsigned long ino) {
	struct block *new;
	new = kmalloc(sizeof(struct block), GFP_KERNEL);
	if (!new)
		return -1;
	new->ino = ino;
	new->num_tags = 0;
	new->next = head;
	head = new;
	return 0;
}

int *get_tagids(unsigned long ino, int *num_tags) {
	struct block *curr = head;
	while(curr) {
		if (curr->ino == ino) {
			*num_tags = curr->num_tags;
			return curr->tag;
		}
		curr = curr->next;
	}
	*num_tags = 0;
	return NULL;
}

int add_tagid(unsigned long ino, int tag) {
	struct block *curr = head;
	while(curr) {
		if (curr->ino == ino) {
			if (curr->num_tags >= MAX_NUM_TAGS)
				return -1;
			curr->tag[curr->num_tags++] = tag;
			return 0;
		}
		curr = curr->next;
	}
	return -1;
}

void remove_tagid(unsigned long ino, int tag) {
	struct block *curr = head;
	while(curr) {
		if (curr->ino == ino) {
			int i;
			for (i = 0; i < curr->num_tags; i++) {
				if (curr->tag[i] == tag) {
					int j;
					for(j = i; j < curr->num_tags - 1; j++)
						curr->tag[j] = curr->tag[j+1];
					curr->num_tags--;
				}
			}
		}
		curr = curr->next;
	}
}

void deallocate_block(unsigned long ino) {
	struct block *prev, *curr;
	prev = NULL;
	curr = head;
	while(curr) {
		if (curr->ino == ino) {
			if (prev)
				prev->next = curr->next;
			else
				head = curr->next;
			kfree(curr);
			return;
		}
		prev = curr;
		curr = curr->next;
	}
}

void deallocate_all() {
	while(head) {
		struct block *temp = head;
		head = head->next;
		kfree(temp);
	}
}
