#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include "table.h"

/* Creates a fake inode entry with the specified filename and inode number */
static struct inode_entry* create_entry(const char *name, unsigned long ino) {
	struct inode_entry *entry;
	entry = kmalloc(sizeof(struct inode_entry), GFP_KERNEL); 
	if(!entry) {
		printk("Unable to create inode entry\n");
		return NULL;
	}

	entry->ino = ino;
	strlcpy(entry->filename, name, MAX_FILENAME_LEN);
	entry->hash = 1;
	return entry;
}

/* Initial test that simply adds a single file, ensures that it exists, and then removes it. */
static void test1(struct hash_table *table, int verbose) {
	struct inode_entry* entry;
	struct table_element* element;
	int result;
	printk("Running test #1: insert, retrieve, remove\n");
    
	if(verbose) printk("Creating a new inode with name 'a' and #100\n");
	entry = create_entry("a", 100);

	if(verbose) printk("inserting inode with name 'a' with tag 'letters'\n");
	result = insert(table, "letters", entry);
	if(result != 0) {
		printk("ERROR: Insert returned an error: %d\n", result);
		return;
	}
		
	if(verbose) printk("Retreiving table element entries for tag 'letters'\n");
	element = get_inodes(table, "letters");
	if(!element) {
		printk("ERROR: Unable to find any entries\n");
	} else {
		if(verbose) printk("Found %i entries with tag 'letters'\n", size(element));
	}

	if(verbose) printk("Removing inode 'a' from table\n");
	result = remove(table, "letters", 100);
	if(result != 0) {
		printk("ERROR: Remove returned an error: %d\n", result);
		return;
	}

	if(verbose) printk("verifying that all inodes with tag 'letters' have been removed.\n");
	element = get_inodes(table, "letters");
	if(element) {
		printk("ERROR: Found %i entries with tag 'letters'. Expected none.\n", size(element));
	} else {
        	if(verbose) printk("Element has been successfully removed.\n");
	}
}

static void test2(struct hash_table *table, int verbose) {
	printk("Running test #2: \n");
	if(verbose) printk("Creating inode 'a' with number 100\n");
	if(verbose) printk("Creating inode 'b' with number 101\n");
	if(verbose) printk("Creating inode 'c' with number 102\n");
	if(verbose) printk("Creating inode '1' with number 201\n");
	if(verbose) printk("Creating inode '2' with number 202\n");
	if(verbose) printk("Creating inode '3' with number 203\n");

	if(verbose) printk("Getting tag 'letter/a'\n");	
	if(verbose) printk("Adding tag 'first'\n");	

	if(verbose) printk("Getting tag 'number/1'\n");	
	if(verbose) printk("Adding tag 'first'\n");	

	if(verbose) printk("get inodes tagged 'letter'");
	if(verbose) printk("get inodes tagged 'first'");
	if(verbose) printk("verify that we got inode #100");


}

static int __init init_test(void) {
	struct hash_table* table;
	printk("Creating a new tagfs hash table\n");
	table = create_table();

	printk("Initializing unit testing of tagfs\n");
	test1(table, 1);
	
	kfree(table); 
	return 0;
}

static void  __exit exit_test(void) {
	printk("Unloading testing module of tagfs\n");
}

module_init(init_test)
module_exit(exit_test)
