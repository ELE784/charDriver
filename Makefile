# Variables pointing to different paths
KERNEL_DIR	:= /lib/modules/$(shell uname -r)/build
PWD		:= $(shell pwd)

# This is required to compile your ele784-lab1.c module
obj-m             = ele784-lab1.o
ele784-lab1-objs := charDriver.o circularBuffer.o

# We build our module in this section
all:
	@echo "Building the ELE784 Lab1: Ring buffer driver..."
	@make -C $(KERNEL_DIR) M=$(PWD) modules

# It's always a good thing to offer a way to cleanup our development directory
clean:
	-rm -f *.o *.ko .*.cmd .*.flags *.mod.c Module.symvers modules.order
	-rm -rf .tmp_versions
