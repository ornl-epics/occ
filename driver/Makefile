ifneq ($(KERNELRELEASE),)
# KBuild portion
#
obj-m := sns-ocb.o
else
# Invoke Kbuild
#
KDIR ?= /lib/modules/`uname -r`/build
VER_MAJ := $(shell grep OCB_VER_MAJ sns-ocb.h | awk '{print $$3}')
VER_MIN := $(shell grep OCB_VER_MIN sns-ocb.h | awk '{print $$3}')
VER = $(join -,$(join $(VER_MAJ), $(join .,$(VER_MIN))))
TARGETDIR = /lib/modules/`uname -r`/extra

default:
	$(MAKE) -C $(KDIR) M=$$PWD

clean:
	@rm -f *.ko *.o *.mod.c modules.order Module.symvers

install:
	install -d -m 0755 /etc/snsocb
	install -m 0755 snsocb0-sample /etc/snsocb
	install -m 0644 udev.rules /etc/udev/rules.d/95-snsocb.rules.sample
	install -m 0755 rhel-load.modules /etc/sysconfig/modules/snsocb.modules
	install -m 0644 sns-ocb.ko $(TARGETDIR)/sns-ocb$(VER).ko
	ln -sf sns-ocb$(VER).ko $(TARGETDIR)/sns-ocb.ko
	depmod

load:
	insmod ./sns-ocb.ko

unload:
	rmmod sns-ocb

endif
