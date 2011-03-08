/** @file treap.c
 *  @brief A treap implementation of the table element
 *
 *
 *  Relatively faster approach for table elements, with most
 *  operations taking logarithmic time.
 *
 *  Note: probably not working at the moment, don't use
 *
 */


#include "table_element.h"
#include "treap.h"
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/random.h>


struct table_element {
	struct treap_node *root;
	unsigned int count;
	int read_only;
};

struct table_element *new_element() {
	struct table_element *e = kmalloc(sizeof(struct table_element), GFP_KERNEL);
	if (!e)
		return e;
	e->root = NULL;
	e->count = 0;
	e->read_only = 0;
	return e;
}

/* Frees all the memory associated with the treap */
static void delete_tree(struct treap_node *root) {
	if (!root)
		return;
	delete_tree(root->pointers[LEFT]);
	delete_tree(root->pointers[RIGHT]);
	kfree(root);
}

/* Deep copies the tree */
struct treap_node *copy_tree(struct treap_node *root, int *status) {
	struct treap_node *copy, *left, *right;
	if (!root)
		return NULL;
	copy = kmalloc(sizeof(struct treap_node), GFP_KERNEL);
	if (!copy) {
		*status = NO_MEMORY;
		goto fail;
	}
	memcpy(copy, root, sizeof(struct treap_node));
	copy->pointers[LEFT] = copy->pointers[RIGHT] = copy->pointers[PARENT] = NULL;
	left = copy_tree(root->pointers[LEFT], status);
	if (*status == NO_MEMORY)
		goto fail;
	right = copy_tree(root->pointers[LEFT], status);
	if (*status == NO_MEMORY)
		goto fail;
	copy->pointers[LEFT] = left;
	copy->pointers[RIGHT] = right;
	left->pointers[PARENT] = copy;
	right->pointers[PARENT] = copy;
	return copy;
fail:
	delete_tree(copy);
	return NULL;
}

void delete_element(struct table_element *e) {
	delete_tree(e->root);
	kfree(e);
}

/* Does an inorder traversal of the treap to get a sorted array of the inodes */
static void tree_to_array(struct inode_entry *entries, struct treap_node *root, unsigned int *index) {
	if (!root)
		return;
	tree_to_array(entries, root->pointers[LEFT], index);
	memcpy(&entries[*index++], &root->entry, sizeof(struct inode_entry));
	tree_to_array(entries, root->pointers[RIGHT], index);
}

const struct inode_entry *set_to_array(struct table_element *e) {
	unsigned int index = 0;
	struct inode_entry *entries = kmalloc(sizeof(struct inode_entry) * e->count, GFP_KERNEL);
	if (!entries)
		return NULL;
	tree_to_array(entries, e->root, &index);
	return entries;
}

int insert_entry(struct table_element *e, const struct inode_entry *entry){
	struct treap_node *n;
	struct treap_node **curr, *prev;
	if (e->read_only)
		return READ_ONLY;

	n = kcalloc(1, sizeof(struct treap_node), GFP_KERNEL);
	if (!n)
		return NO_MEMORY;
	get_random_bytes(&n->prio, sizeof(long long));
	memcpy(&n->entry, entry, sizeof(struct inode_entry));
	prev = NULL;
	curr = &e->root;
	/* Normal binary tree insertion */
	while(*curr) {
		if ((*curr)->entry.ino->i_ino == entry->ino->i_ino)
			return DUPLICATE;
		prev = *curr;
		if ((*curr)->entry.ino->i_ino < entry->ino->i_ino)
			curr = &((*curr)->pointers[RIGHT]);
		else
			curr = &((*curr)->pointers[LEFT]);
	}
	n->pointers[PARENT] = prev;
	*curr = n;
	/* Maintain heap ordering */
	while(n->pointers[PARENT] && n->pointers[PARENT]->prio > n->prio) {
		struct treap_node *parent = n->pointers[PARENT];
		if (n->entry.ino->i_ino < parent->entry.ino->i_ino) {
			parent->pointers[LEFT] = n->pointers[RIGHT];
			n->pointers[RIGHT] = parent;
		} 
		else {
			parent->pointers[RIGHT] = n->pointers[LEFT];
			n->pointers[LEFT] = parent;
		}
		n->pointers[PARENT] = parent->pointers[PARENT];
		parent->pointers[PARENT] = n;
		if (!n->pointers[PARENT])
			e->root = n;
	}
	e->count++;
	return 0;
}

void remove_entry(struct table_element *e, unsigned long ino) {
	struct treap_node *curr = e->root;
	/* Find the entry */
	while(curr) {
		if (curr->entry.ino->i_ino < ino)
			curr = curr->pointers[RIGHT];
		else if (curr->entry.ino->i_ino > ino)
			curr = curr->pointers[LEFT];
		else {
			/* Rotate entry down to leaf while maintaining heap order */
			while(curr->pointers[LEFT] || curr->pointers[RIGHT]) {
				struct treap_node *left = curr->pointers[LEFT];
				struct treap_node *right = curr->pointers[RIGHT];
				if (left->prio < right->prio) {
					curr->pointers[LEFT] = left->pointers[RIGHT];
					left->pointers[RIGHT] = curr;
					left->pointers[PARENT] = curr->pointers[PARENT];
					curr->pointers[PARENT]= left;
					if(!left->pointers[PARENT])
						e->root = left;
				} 
				else {
					curr->pointers[RIGHT] = right->pointers[LEFT];
					right->pointers[LEFT] = curr;
					right->pointers[PARENT] = curr->pointers[PARENT];
					curr->pointers[PARENT]= right;
					if(!right->pointers[PARENT])
						e->root = right;
				}
			}
			if (curr->pointers[PARENT]->entry.ino->i_ino > curr->entry.ino->i_ino)
				curr->pointers[PARENT]->pointers[LEFT] = NULL;
			else
				curr->pointers[PARENT]->pointers[RIGHT] = NULL;
			e->count--;
			kfree(curr);
			break;
		}
	}
}

/* Performs a join operation on two treaps and returns a deep copy of the joined treap */
static struct treap_node *join (struct treap_node *r1, struct treap_node *r2, int *status) {
	struct treap_node *root;
	if (!r1) 
		return r2;
	if (!r2) 
		return r1;
	root = kmalloc(sizeof(struct treap_node), GFP_KERNEL);
	if (!root) {
		*status = NO_MEMORY;
		goto fail;
	}
	if (r1->prio < r2->prio) {
		memcpy(root, r1, sizeof(struct treap_node));
		root->pointers[LEFT] = copy_tree(r1->pointers[LEFT], status);
		if (*status == NO_MEMORY)
			goto fail;
		root->pointers[LEFT]->pointers[PARENT] = root;
		root->pointers[RIGHT] = join(r1->pointers[RIGHT], r2, status);
		if (*status == NO_MEMORY)
			goto fail;
	}
	else {
		memcpy(root, r2, sizeof(struct treap_node));
		root->pointers[RIGHT] = copy_tree(r1->pointers[RIGHT], status);
		if (*status == NO_MEMORY)
			goto fail;
		root->pointers[RIGHT]->pointers[PARENT] = root;
		root->pointers[LEFT] = join(r1, r2->pointers[LEFT], status);
		if (*status == NO_MEMORY)
			goto fail;
	}
	return root;
fail:
	delete_tree(root);
	return NULL;
}

/* Performs a split operation on a treap with ino as the split point, returns deep copied versions of the two treaps of resulting split */
static struct treap_node *split(struct treap_node **less, struct treap_node **gtr, struct treap_node *r, unsigned long ino, int *status) {
	struct treap_node *root, *ret;
	if (r == NULL) {
		*less = *gtr = NULL;
		return NULL;
	}
	root = kmalloc(sizeof(struct treap_node), GFP_KERNEL);
	if (!root) {
		*status = NO_MEMORY;
		goto fail;
	}
	memcpy(root, r, sizeof(struct treap_node));
	root->pointers[LEFT] = root->pointers[RIGHT] = root->pointers[PARENT] = NULL;
	if (r->entry.ino->i_ino < ino) {
		*less = root;
		ret = split(&(root->pointers[RIGHT]), gtr, r->pointers[RIGHT], ino, status);
		if (*status == NO_MEMORY)
			goto fail;
		root->pointers[RIGHT]->pointers[PARENT] = root;
		return ret;
	} else if (r->entry.ino->i_ino > ino) {
		*gtr = root;
		ret = split(less, &(root->pointers[LEFT]), r->pointers[LEFT], ino, status);
		if (*status == NO_MEMORY)
			goto fail;
		root->pointers[LEFT]->pointers[PARENT] = root;
		return ret;
	} else {
		*less = copy_tree(r->pointers[LEFT], status);
		if (*status == NO_MEMORY)
			goto fail;
		*gtr = copy_tree(r->pointers[RIGHT], status);
		if (*status == NO_MEMORY)
			goto fail;
		return root;
	}
fail:
	delete_tree(root);
	return NULL;
}

/* Performs a union operation on the treap and returns a deep copy of the resulting treap*/
static struct treap_node *treap_union(struct treap_node *r1, struct treap_node *r2, int *status) {
	struct treap_node *root = NULL, *less = NULL, *gtr = NULL, *duplicate = NULL;
	if (r1 == NULL)
		return r2;
	if (r2 == NULL)
		return r1;
	
	if (r1->prio < r2->prio)
		return treap_union(r2, r1, status);
	
	duplicate = split(&less, &gtr, r2, r1->entry.ino->i_ino, status);
	if (*status == NO_MEMORY)
		goto fail;
	if (duplicate)
		kfree(duplicate);
	root = kmalloc(sizeof(struct treap_node), GFP_KERNEL);
	if (!root) {
		*status = NO_MEMORY;
		goto fail;
	}
	root->pointers[LEFT] = treap_union(r1->pointers[LEFT], less, status);
	if (*status == NO_MEMORY)
		goto fail;
	root->pointers[RIGHT] = treap_union(r1->pointers[RIGHT], gtr, status);
	if (*status == NO_MEMORY)
		goto fail;
	root->pointers[LEFT]->pointers[PARENT] = root;
	root->pointers[RIGHT]->pointers[PARENT] = root;
	delete_tree(less);
	delete_tree(gtr);
	return root;
fail:
	delete_tree(root);
	delete_tree(less);
	delete_tree(gtr);
	delete_tree(duplicate);
	return NULL;
}

/* Performs an intersection operation on two treaps and returns a deep copy of the resulting treap */
static struct treap_node *treap_intersect(struct treap_node *r1, struct treap_node *r2, int *status) {
	struct treap_node *root = NULL, *less = NULL, *gtr = NULL, *left = NULL, *right = NULL, *duplicate = NULL;
	if (r1 == NULL)
		return r2;
	if (r2 == NULL)
		return r1;
	
	if (r1->prio < r2->prio)
		return treap_union(r2, r1, status);
	
	duplicate = split(&less, &gtr, r2, r1->entry.ino->i_ino, status);
	if (*status == NO_MEMORY)
		goto fail;
	left = treap_intersect(r1->pointers[LEFT], less, status);
	if (*status == NO_MEMORY)
		goto fail;
	right = treap_intersect(r1->pointers[RIGHT], gtr, status);
	if (*status == NO_MEMORY)
		goto fail;
	
	delete_tree(less);
	delete_tree(gtr);
	if (duplicate == NULL) {
		return join(left, right, status);
	} 
	else {
		root = duplicate;
		root->pointers[LEFT] = left;
		root->pointers[RIGHT] = right;
		root->pointers[LEFT]->pointers[PARENT] = root;
		root->pointers[RIGHT]->pointers[PARENT] = root;
		return root;
	}
fail:
	delete_tree(root);
	delete_tree(less);
	delete_tree(gtr);
	delete_tree(left);
	delete_tree(right);
	delete_tree(duplicate);
	return NULL;
}

/* Counts the number of nodes in the treap */
static unsigned int count(struct treap_node *root) {
	if (!root)
		return 0;
	return 1 + count(root->pointers[LEFT]) + count(root->pointers[RIGHT]);
}

struct table_element *set_union(struct table_element * e1, struct table_element *e2) {
	int status;
	struct table_element *result = kmalloc(sizeof(struct table_element), GFP_KERNEL);
	if (!result)
		goto fail;
	result->read_only = 1;
	status = 0;
	result->root = treap_union(e1->root, e2->root, &status);
	if (status == NO_MEMORY)
		goto fail;
	result->count = count(result->root);
	return result;
fail:
	delete_element(result);
	return NULL;
}
struct table_element *set_intersect(struct table_element * e1, struct table_element *e2) {
	int status;
	struct table_element *result = kmalloc(sizeof(struct table_element), GFP_KERNEL);
	result->read_only = 1;
	if (!result)
		goto fail;
	status = 0;
	result->root = treap_intersect(e1->root, e2->root, &status);
	if (status == NO_MEMORY)
		goto fail;
	result->count = count(result->root);
	return result;
fail:
	delete_element(result);
	return NULL;
}
