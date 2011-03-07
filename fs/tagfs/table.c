/* 
* Tag lookup table implementation
*/

//int remove(char* tag, inode_entry *e);

#include "table_element.h"

#define MAX_TAG_LEN	255

struct tag_tree_node {
	char tag[MAX_TAG_LEN];
	struct table_element *e;
	struct tag_tree left;
	struct tag_tree right;
	int tag_id;
}	

enum tag_tree_error {
	INVALID_NODE = -255,
	ALREADY_EXISTS,	
};

struct tag_tree_node *find_node(struct tag_tree_node *node, char *tag) 
{
	if(!node)
		return NULL;
	int compare = strncmp(node->tag, tag, MAX_TAG_LEN);
	if(compare == 0)
		return node;	
	if(compare < 0)
		return find_node(node->left, tag);	
	if(compare > 0)
		return find_node(node->right, tag);	
}

int result add_node(struct tag_tree_node *root, struct tag_tree_node *new_node) 
{
	if(!root)
		return INVALID_NODE;
	int compare = strncmp(root->tag, new_node->tag, MAX_TAG_LEN);
	if(compare == 0)
		return ALREADY_EXISTS;

	/* Add new tag node to the left */
	if(compare < 0) {
		if(root->left == NULL) {
			root->left = new_node;
			return 0;
		} else {
			return add_node(root->left, tag);	
		}
	}

	/* Add new tag node to the right */
	if(compare > 0) {
		if(root->right == NULL) {
			root->right = new_node;
			return 0;
		} else {
			return add_node(root->right, tag);	
		}
	}
}

int insert(struct tag_tree_node *root, const char *tag, const struct inode_entry *i)
{
	struct tag_tree_node *node;
	node = find_node(root, tag);
	if(!node) {
		/* Create empty table element */
		node = kmalloc(sizeof(struct tag_tree_node), GFP_KERNEL);
		node->left = NULL;
		node->right = NULL;
		node->e = new_element();
		strlcpy(node->tag, tag, MAX_TAG_LEN);
		if(add_node(root, node) < 0) {
			delete_element(node->e);
			kfree(node->tag);
			kfree(node);
		}
	}

	/* TODO: Add tag to inode hash */

	insert_entry(node->e, i);
}

struct table_element * get_inodes(struct tag_tree_node *root, char* tag) 
{
	struct tag_tree_node n = find_node(root, tag);
	return n->e;
}
