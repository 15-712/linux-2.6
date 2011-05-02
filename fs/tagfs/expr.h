#ifndef _EXPR_H
#define _EXPR_H

#include "table_element.h"
#include "table.h"
enum op_type {
	UNION,
	INTERSECTION,
};

enum node_type {
	OPERATOR,
	TAG,
};

struct expr_tree {
	enum node_type type;
	unsigned int num_ops;
	union {
		char tag[MAX_TAG_LEN];
		struct {
			struct expr_tree *left;
			struct expr_tree *right;
			enum op_type op;
		};
	};
};

/* Evaluate the expression stored in the tree and return a table_element with the corresponding inodes */
struct table_element* parse_tree(struct expr_tree *, struct hash_table *);

/* Parses expr and builds an expression tree out of it. Returns NULL on error. */
struct expr_tree * build_tree(const char*);

/* Frees all memory stored in a tree */
void free_tree(struct expr_tree*);

#endif
