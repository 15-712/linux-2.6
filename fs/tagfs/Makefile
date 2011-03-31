#
# Makefile for tagfs
#
ifneq (${KERNELRELEASE},)
obj-m += tagfs.o
tagfs-objs += inode.o sarray.o table.o syscall.o expr.o
obj-m += testfs.o
testfs-objs += test.o sarray.o table.o
else
#KERNEL_SOURCE := /lib/modules/$(shell uname -r)/build
KERNEL_SOURCE := ../..
PWD := $(shell pwd)

default:
	$(MAKE) -C ${KERNEL_SOURCE} SUBDIRS=$(PWD) modules
clean :
	rm *.o *.ko
endif
