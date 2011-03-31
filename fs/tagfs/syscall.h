#ifndef _TAGFS_SYSCALL_H_
#define _TAGFS_SYSCALL_H

#define MAX_TAGEX_LEN 255

#include "expr.h"

extern char cwt[MAX_TAGEX_LEN+1];
extern struct expr_tree *tree;
extern int (*prev_addtag)(const char __user *, const char __user *);
extern int (*prev_rmtag)(const char __user *, const char __user *);
extern int (*prev_chtag)(const char __user *);
extern int (*prev_mvtag)(const char __user *, const char __user *);

int addtag(const char __user *, const char __user *);
#endif
