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
	entry->count = 0;
	return entry;
}

static int insert_inode(struct hash_table *table, const char *tag, struct inode_entry *entry) {
	int result;
	result = insert(table, tag, entry);
	if(result != 0) {
		printk("ERROR: Could not insert entry %s with tag %s\n", entry->filename, tag);
		return result;
	}
	return 0;
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
	if(insert_inode(table, "letters", entry) != 0)
		return;
		
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

	printk("Finished test #1\n");
}

static void test2(struct hash_table *table, int verbose) {
	struct inode_entry* entry;
	struct table_element *element1;
	struct table_element *element2;
	struct table_element *result;
	printk("Running test #2: \n");
	
	/* Create & insert inode a with tag letter & first*/
	if(verbose) printk("Creating inode 'a' with number 100\n");
	entry = create_entry("a", 100);
	if(verbose) printk("Tagging inode 100 with 'letter'\n");
	insert_inode(table, "letter", entry);
	if(verbose) printk("Tagging inode 100 with 'a'\n");
	insert_inode(table, "a", entry);
	if(verbose) printk("Tagging inode 100 with 'first'\n");
	insert_inode(table, "first", entry);

	/* Create & insert inode b with tag letter */
	if(verbose) printk("Creating inode 'b' with number 101\n");
	entry = create_entry("b", 101);
	if(verbose) printk("Tagging inode 101 with 'letter', 'b'\n");
	insert_inode(table, "letter", entry);
	insert_inode(table, "b", entry);

	/* Create & insert inode 1 with tag number & first */
	if(verbose) printk("Creating inode '1' with number 201\n");
	entry = create_entry("1", 201);
	if(verbose) printk("Tagging inode 201 with 'number', '1', 'first'\n");
	insert_inode(table, "number", entry);
	insert_inode(table, "1", entry);
	insert_inode(table, "first", entry);

	/* Create & insert inode 2 with tag number */
	if(verbose) printk("Creating inode '2' with number 202\n");
	entry = create_entry("2", 202);
	if(verbose) printk("Tagging inode 202 with 'number', '2'\n");
	insert_inode(table, "number", entry);
	insert_inode(table, "2", entry);

	if(verbose) printk("Getting tag 'letter'\n");	
	element1 = get_inodes(table, "letter");
	if(!element1) {
		printk("ERROR: Unable to find entries for tag 'letter'\n");
	} else {
		if(verbose) printk("Found %i entries with tag 'letter'\n", size(element1));
		if(size(element1) != 2)
			printk("ERROR: Expected 2 entries with tag 'letter', found %i\n", size(element1));
	}

	if(verbose) printk("Getting tag 'number'\n");	
	element1 = get_inodes(table, "number");
	if(!element1) {
		printk("ERROR: Unable to find entries for tag 'number'\n");
	} else {
		if(verbose) printk("Found %i entries with tag 'number'\n", size(element1));
		if(size(element1) != 2)
			printk("ERROR: Expected 2 entries with tag 'number', found %i\n", size(element1));
	}

	if(verbose) printk("Getting tag 'first'\n");	
	element2 = get_inodes(table, "first");
	if(!element2) {
		printk("ERROR: Unable to find entries for tag 'first'\n");
	} else {
		if(verbose) printk("Found %i entries with tag 'first'\n", size(element2));
		if(size(element2) != 2)
			printk("ERROR: Expected 2 entries with tag 'first', found %i\n", size(element2));
	}

	/* Intersection */
	if(verbose) printk("Calculate intersection of number and first\n");	
	result = set_intersect(element1, element2);
	if(!result)
		printk("ERROR: Unable to perform intersection\n");

	if(verbose) printk("Verifying that we got inode #201\n");
	if(size(result) != 1)
		printk("ERROR: Intersection returned too many results.\n");
	else
		printk("Successfully recovered inode #%i\n", (int)set_to_array(result)[0]->ino);

	/* Union */
	if(verbose) printk("Calculate union of number and first\n");	
	result = set_union(element1, element2);
	if(!result)
		printk("ERROR: Unable to perform union\n");

	if(verbose) printk("Verifying that we got 3 results\n");
	if(size(result) != 3)
		printk("ERROR: Untion returned an incorrect number of results. Found %i, expected 3.\n", size(result));
	else
		printk("Successfully recovered 3 results.");

	
	printk("Finished test #2\n");
}



static int __init init_test(void) {
	struct hash_table* table;
	printk("Creating a new tagfs hash table\n");
	table = create_table();

	printk("Initializing unit testing of tagfs\n");
	test1(table, 1);
	test2(table, 1);
	
	kfree(table); 
	return 0;
}

static void  __exit exit_test(void) {
	printk("Unloading testing module of tagfs\n");
}

module_init(init_test)
module_exit(exit_test)
