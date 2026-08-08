obj-m += xnic_e1000.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
ccflags-y += -Wall -Wformat=2 -Wshadow -Wundef -Werror=implicit-function-declaration

.PHONY: all clean
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
