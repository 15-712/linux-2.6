#ifndef _TREAP_H
#define _TREAP_H

enum treap_pointers {
	PARENT = 0,
	LEFT,
	RIGHT,
	TYPE_COUNT
};

struct treap_node {
	unsigned long long prio;
	struct inode_entry entry;
	struct treap_node *pointers[TYPE_COUNT];
};

#endif
