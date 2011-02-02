ifneq ($(KERNELRELEASE),)

obj-m := gator.o

gator-objs :=	gator_main.o \
		gator_events_armv6.o \
		gator_events_armv7.o \
		gator_events_irq.o \
		gator_events_sched.o \
		gator_events_net.o \
		gator_events_block.o \
		gator_events_meminfo.o

else

all:
	@echo
	@echo "usage:"
	@echo "      make -C <kernel_build_dir> M=\`pwd\` ARCH=arm CROSS_COMPILE=<...> modules"
	@echo
	$(error)

clean:
	rm -f *.o modules.order Module.symvers gator.ko gator.mod.c

endif
