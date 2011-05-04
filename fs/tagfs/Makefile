#
# Makefile for tagfs
#
ifneq (${KERNELRELEASE},)
obj-m += tagfs.o
tagfs-objs += balloc.o dir.o file.o ialloc.o inode.o ioctl.o namei.o super.o symlink.o sarray.o table.o syscall.o expr.o block.o xattr.o xattr_user.o xattr_trusted.o
else
#KERNEL_SOURCE := /lib/modules/$(shell uname -r)/build
KERNEL_SOURCE := ../..
PWD := $(shell pwd)

default:
	$(MAKE) -C ${KERNEL_SOURCE} SUBDIRS=$(PWD) modules
clean :
	rm -rf *.o *.ko
endif
