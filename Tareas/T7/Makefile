ccflags-y := -Wall -std=gnu99 -Wno-declaration-after-statement

obj-m := disco.o
disco-objs := kmutex.o disco-impl.o

KDIR  := /lib/modules/$(shell uname -r)/build
PWD   := $(shell pwd)

#include $(KDIR)/.config

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

disco-impl.o kmutex.o: kmutex.h

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
