#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include "table.h"

/* Creates a fake inode entry with the specified filename and inode number */
static struct inode_entry* create_entry(const char *name, unsigned long ino) {
	struct inode_entry *entry;
	struct inode *inode;
	entry = kmalloc(sizeof(struct inode_entry), GFP_KERNEL); 
	if(!entry) {
		printk("Unable to create inode entry\n");
		return NULL;
	}
	inode = kmalloc(sizeof(struct inode), GFP_KERNEL);
	if(!inode) {
		printk("Unable to create inode\n");
		kfree(entry);
		return NULL;
	}

	inode->i_ino = ino;
	entry->ino = inode;
	strlcpy(entry->filename, name, MAX_FILENAME_LEN);
	entry->hash = 1;
	return entry;
}

/* Initial test that simply adds a single file, ensures that it exists, and then removes it. */
static void test1(struct hash_table *table) {
	struct inode_entry* entry;
	struct table_element* element;
	int result;
	printk("Running test #1: insert, retrieve, remove\n");

	printk("Creating a new inode with name 'a' and #100\n");
	entry = create_entry("a", 100);

	printk("inserting inode with name 'a' with tag 'letters'\n");
	result = insert(table, "letters", entry);
	if(result != 0) {
		printk("ERROR: Insert returned an error: %d\n", result);
		return;
	}
		
	printk("Retreiving table element entries for tag 'letters'\n");
	element = get_inodes(table, "letters");
	if(!element) {
		printk("ERROR: Unable to find any entries\n");

	} else {
		printk("Found %i entries with tag 'letters'\n", size(element));
	}

	printk("Removing inode 'a' from table\n");
	result = remove(table, "letters", 100);
	if(result != 0) {
		printk("ERROR: Remove returned an error: %d\n", result);
		return;
	}

	printk("verifying that all inodes with tag 'letters' have been removed.\n");
	element = get_inodes(table, "letters");
	if(element) {
		printk("ERROR: Found %i entries with tag 'letters'. Expected none.\n", size(element));
	} else {
		printk("Element has been successfully removed.\n");
	}
}

static int __init init_test(void) {
	struct hash_table* table;
	printk("Creating a new tagfs hash table\n");
	 table = create_table();

	printk("Initializing unit testing of tagfs\n");
	test1(table);
	
	kfree(table); 
	return 0;
}

static void  __exit exit_test(void) {
	printk("Unloading testing module of tagfs\n");
}

module_init(init_test)
module_exit(exit_test)
