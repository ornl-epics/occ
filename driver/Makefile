ifneq ($(KERNELRELEASE),)
# KBuild portion
#
obj-m := sns-ocb.o
else
# Invoke Kbuild
#
KDIR ?= /lib/modules/`uname -r`/build
VER = $(shell pwd | awk -F'/' '{print $$(NF-1)}' | egrep "[0-9]+-[0-9]+" | sed -e 's/\(.*\)/-\1/')
ifeq ($(VER),)
VER = -current
endif
TARGETDIR = /lib/modules/`uname -r`/extra

default:
	$(MAKE) -C $(KDIR) M=$$PWD

clean:
	@rm -f *.ko *.o *.mod.c modules.order Module.symvers

install:
	install -d -m 0755 /etc/snsocb
	install -m 0755 snsocb0-sample /etc/snsocb
	install -m 0644 udev.rules /etc/udev/rules.d/95-snsocb.rules
	install -m 0755 rhel-load.modules /etc/sysconfig/modules/snsocb.modules
	install -m 0644 sns-ocb.ko $(TARGETDIR)/sns-ocb$(VER).ko
	ln -sf sns-ocb$(VER).ko $(TARGETDIR)/sns-ocb.ko
	depmod

load:
	insmod ./sns-ocb.ko

endif
