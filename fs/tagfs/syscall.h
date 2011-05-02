#ifndef _TAGFS_SYSCALL_H_
#define _TAGFS_SYSCALL_H

#define MAX_TAGEX_LEN 255

#include "expr.h"

extern char cwt[MAX_TAGEX_LEN+1];
extern struct expr_tree *tree;

int opentag(const char __user *, int);
int addtag(const char __user *, const char __user **, unsigned int);
int rmtag(const char __user *, const char __user *);
int chtag(const char __user *);
int mvtag(const char __user *, const char __user *);
int getcwt(char __user *, unsigned long);
int lstag(const char __user *, void __user *, unsigned long, int);
int distag(unsigned long, char __user *, unsigned long, unsigned long); 
void install_syscalls(void);
void uninstall_syscalls(void);
#endif
