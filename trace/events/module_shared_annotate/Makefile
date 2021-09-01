INCLUDES = -I. -I$(KDIR)/include
KBUILD_CFLAGS += -g -fno-pie
CFLAGS_EXTRA += -fno-pie
EXTRA_CFLAGS += $(CFLAGS_EXTRA)

obj-m += gator_annotate.o

KDIR   := /lib/modules/$(shell uname -r)/build
PWD    := $(shell pwd)

MODULES_CONF_PATH := /etc/modules-load.d/modules.conf
MODULE_NAME   := gator_annotate
CFLAGS_gator_annotate.o := -I$(src) -fstack-protector -fno-pie

%.ko: %.o
	$(MAKE) -C $(KDIR)  M=$(PWD) $@

kernel_modules:
	$(MAKE) -C $(KDIR)  M=$(PWD) modules

all: kernel_modules

modules_install: all
	if ! grep -q $(MODULE_NAME) '$(MODULES_CONF_PATH)'; then \
		sed -i -e '$$a$(MODULE_NAME)' $(MODULES_CONF_PATH); \
	fi
	install -m 644 $(MODULE_NAME).ko /lib/modules/`uname -r`/kernel/drivers/$(MODULE_NAME).ko
	/sbin/depmod -a

install:    modules_install

clean:
	$(RM) *.o *~ *.ko *.mod.c *.mod .*.o.cmd .*.ko.cmd modules.order Module.symvers
	$(RM) -r .tmp_versions

