/** @file expr.c
 *  @brief Expression evaluation functions for tag expressions.
 *  @author William Wang
 *  @author Tim Shields
 *  @author Ping-Yao Tseng
 *   
 *  Expression is evaluated and converted into an expression tree,
 *  intersection and union operations are then done on the tree.
 *
 */
#include <linux/slab.h>

#include "table.h"
#include "table_element.h"
#include "expr.h"

struct tree_stack {
	int top;
	int size;
	struct expr_tree **tree;
};

struct op_stack {
	int top;
	int size;
	char *op;
};

/* pushes an operator onto the stack */
static struct op_stack *op_push(struct op_stack *stack, char op)
{
	stack->top++;
	if(stack->top >= stack->size) {
		char *c;
		c = krealloc(stack->op, sizeof(char) * (stack->size << 1), GFP_KERNEL);
		if(!c) {
			stack->top--;
			return NULL;
		}
		stack->op = c;
		stack->size <<= 1;
	}
	stack->op[stack->top] = op;
	return stack;
}

/* pops an operator off of the stack */
static char op_pop(struct op_stack *stack) {
	if(stack->top >= 0) {
		return stack->op[stack->top--];
	}
	return '\0';
}

/* pushes an expression tree onto the stack */
static struct tree_stack *tree_push(struct tree_stack *stack, struct expr_tree *tree)
{
	stack->top++;
	if(stack->top >= stack->size) {
		struct expr_tree **t;
		t = krealloc(stack->tree, sizeof(struct expr_tree*) * (stack->size << 1), GFP_KERNEL);
		if(!t) {
			stack->top--;
			return NULL;
		}
		stack->tree = t;
		stack->size <<= 1;
	}
	stack->tree[stack->top] = tree;
	return stack;
}

/* pops an expression tree off of the stack */
static struct expr_tree *tree_pop(struct tree_stack *stack) {
	if(stack->top >= 0)
		return stack->tree[stack->top--];
	return NULL;
}

/* Returns 1 if character is an intersect operator */
static int is_intersect_op(char c) {
	switch(c) {
		case '&':
		case '/':
			return 1;
			break;
		default:
			return 0;
			break;
	}
}

/* Returns 1 if character is a union operator */
static int is_union_op(char c) {
	switch(c) {
		case '|':
		case '+':
			return 1;
			break;
		default:
			return 0;
			break;
	}
}


/* Returns 1 if character is a valid operator, else 0. Doesn't count '(' or ')' */
static int is_op(char c) {
	if(is_intersect_op(c) || is_union_op(c))
		return 1;
	return 0;
}

/* Joins expression trees a & b with the specified operator */
static struct expr_tree *perform_op(struct expr_tree *a, struct expr_tree *b, char op) {
	struct expr_tree *op_node;
	if(!a || !b)
		return NULL;
	op_node = kmalloc(sizeof(struct expr_tree), GFP_KERNEL);
	if(!op_node)
		return NULL;
	op_node->type = OPERATOR;
	op_node->right = a;
	op_node->left = b;
	if(is_intersect_op(op))
		op_node->op = INTERSECTION;
	if(is_union_op(op))
		op_node->op = UNION;
	return op_node;
}

/* Pops operator off the stack and combines it with tags before returning to stack */
static int build_branch(struct tree_stack *sTree, struct op_stack *sOp) {
	char op;
	struct expr_tree *a;
	struct expr_tree *b;
	if(sOp->top == -1)
		return -1;
	op = op_pop(sOp);
	if(sTree->top == -1)
		return -1;
	a = tree_pop(sTree);
	if(sTree->top == -1)
		return -1;
	b = tree_pop(sTree);
	a = perform_op(a, b, op);
	sTree = tree_push(sTree, a);	
	return 1;
}

/* Frees all memory stored in a tree */
void free_tree(struct expr_tree *tree) {
	if(tree->type == TAG) {
		kfree(tree);
	} else {
		if(tree->left)
			free_tree(tree->left);
		if(tree->right)
			free_tree(tree->right);
		kfree(tree);
	}

}

/* Parses expr and builds an expression tree out of it. Returns NULL on error. */
struct expr_tree *build_tree(const char* expr) {
	int index = 0;
	struct tree_stack *sTree;
	struct op_stack *sOp;
	struct expr_tree *tree;
	tree = NULL;
	sOp = kmalloc(sizeof(struct op_stack), GFP_KERNEL);
	
	if(!sOp) 
		return NULL;
	sOp->op = kmalloc(sizeof(char) * 4, GFP_KERNEL);
	if(!sOp->op) {
		kfree(sOp);
		return NULL;
	}
	sOp->size = 4;
	sOp->top = -1;
	
	sTree = kmalloc(sizeof(struct tree_stack), GFP_KERNEL);
	
	if(!sTree) {
		kfree(sOp->op);
		kfree(sOp);
		return NULL;
	}
	sTree->tree = kmalloc(sizeof(struct expr_tree*) * 4, GFP_KERNEL);
	if(!sTree->tree) {
		kfree(sOp->op);
		kfree(sOp);
		kfree(sTree);
		return NULL;
	}
	sTree->size = 4;
	sTree->top = -1;

	while(expr[index] != '\0') {
		/* Ignore whitespace */
		while(expr[index] == ' ') index++;
		
		if(is_op(expr[index])) {
			// Perform priority based on left to right ordering
			if(sOp->top != -1 && sOp->op[sOp->top] != '(')
				build_branch(sTree, sOp);
			op_push(sOp, expr[index]);
			index++;
		} else if(expr[index] == '(') {
			op_push(sOp, '(');
			index++;
		} else if(expr[index] == ')') {
			index++;
			if(sOp->top == -1)
				goto cleanup;
			while(sOp->op[sOp->top] != '(') {
				int result = build_branch(sTree, sOp);
				if(result == -1)
					goto cleanup;
				if(sOp->top == -1)
					goto cleanup;
			}
			// Pop left paren off stack
			op_pop(sOp);
		} else {
			struct expr_tree *node;
			int end;
			node = kmalloc(sizeof(struct expr_tree), GFP_KERNEL);
			if(!node)
				goto cleanup;
			node->type = TAG;
			node->tag[0] = '\0';
			end = index;
			while(	expr[end] != '\0' && 
				expr[end] != ' ' &&
				!is_op(expr[end]) &&
				expr[end] != '(' &&
				expr[end] != ')') end++;
			strlcpy(node->tag, &expr[index], end-index+1); 
			node->tag[end-index] = '\0';
			sTree = tree_push(sTree, node); 	
			index=end;
		}
	}

	while(sOp->top > -1) {
		if(build_branch(sTree, sOp) == -1) {
			goto cleanup;
		}
	}
	if(sTree->top != 0)
		goto cleanup;
	tree = sTree->tree[0];
	kfree(sTree->tree);
	kfree(sTree);
	kfree(sOp->op);
	kfree(sOp);
	return tree;	
cleanup:
	if(sOp) {
		if(sOp->op)
			kfree(sOp->op);
		kfree(sOp);
	}

	if(sTree) {
		if(sTree->tree) {
			while(sTree->top > -1) {
				struct expr_tree *t = tree_pop(sTree);
				free_tree(t);
			}
			kfree(sTree->tree);
		}
		kfree(sTree);
	}
	return NULL;
}

/* Evaluate the expression stored in the tree and return a table_element with the corresponding inodes */
struct table_element* parse_tree(struct expr_tree *tree, struct hash_table *table) {
	struct table_element* result;
	if(tree->type == TAG) {
		printk("Tree of type tag\n");
		printk("Returning inodes for %s\n", tree->tag);
		return get_inodes(table, tree->tag);
	} else if(tree->type == OPERATOR) {
		printk("Tree of type operator\n");
		struct table_element *a =  parse_tree(tree->left, table);
		struct table_element *b =  parse_tree(tree->right, table);
		printk("Acquired childern nodes\n");
		if(!a) {
			result = b;
		} else if(!b) {
			result = a;
		} else {
		
			if(tree->op == INTERSECTION) {
				result = set_intersect(a, b);
			} else {
				result = set_union(a, b);
			}
			if(tree->left->type != TAG)
				delete_element(a);	
			if(tree->right->type != TAG)
				delete_element(b);	
		}
		return result;
	}
	return NULL;
}
