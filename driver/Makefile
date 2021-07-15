ifneq ($(KERNELRELEASE),)
# KBuild portion
#
obj-m := sns-occ.o
else
# Invoke Kbuild
#
KDIR ?= /lib/modules/`uname -r`/build
VER_MAJ := $(shell grep OCC_VER_MAJ sns-occ.h | awk '{print $$3}')
VER_MIN := $(shell grep OCC_VER_MIN sns-occ.h | awk '{print $$3}')
VER = $(join -,$(join $(VER_MAJ), $(join .,$(VER_MIN))))
TARGETDIR = /lib/modules/`uname -r`/extra

default:
	$(MAKE) -C $(KDIR) M=$$PWD

clean:
	@rm -f *.ko *.o *.mod.c modules.order Module.symvers

install:
	install -d -m 0755 /etc/snsocc
	install -m 0755 snsocc0-sample /etc/snsocc
	install -m 0644 udev.rules /etc/udev/rules.d/95-snsocc.rules.sample
	install -m 0755 rhel-load.modules /etc/sysconfig/modules/snsocc.modules
	install -m 0644 sns-occ.ko $(TARGETDIR)/sns-occ$(VER).ko
	ln -sf sns-occ$(VER).ko $(TARGETDIR)/sns-occ.ko
	depmod

load:
	insmod ./sns-occ.ko

unload:
	rmmod sns-occ

endif
