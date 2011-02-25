#
# Makefile for tagfs
#
ifneq (${KERNELRELEASE},)
obj-m += inode.o
else
KERNEL_SOURCE := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C ${KERNEL_SOURCE} SUBDIRS=$(PWD) modules
clean :
	rm *.o *.ko
endif
