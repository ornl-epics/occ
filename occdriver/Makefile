ifneq ($(KERNELRELEASE),)
# KBuild portion
#
obj-m := sns-ocb.o
else
# Invoke Kbuild
#
KDIR ?= /lib/modules/`uname -r`/build

default:
	$(MAKE) -C $(KDIR) M=$$PWD

clean:
	@rm -f *.ko *.o *.mod.c modules.order Module.symvers

endif
