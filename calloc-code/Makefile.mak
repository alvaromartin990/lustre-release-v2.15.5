obj-m += cxl_alloc_test_module.o
cxl_alloc_test_module-objs := cxl_alloc_test_module.o cxl_alloc.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
