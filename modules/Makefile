obj-m		+= tx2mon_kmod.o
DIR		:= $(PWD)
KERNELDIR	:= /lib/modules/$(shell uname -r)/build
all:
	make -C $(KERNELDIR) M=$(DIR) modules
clean:
	make -C $(KERNELDIR) M=$(DIR) clean
