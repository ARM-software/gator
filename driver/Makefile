ifneq ($(KERNELRELEASE),)

obj-m := gator.o

gator-y :=	gator_main.o \
		gator_events_irq.o \
		gator_events_sched.o \
		gator_events_net.o \
		gator_events_block.o \
		gator_events_meminfo.o

gator-y +=	gator_events_mmaped.o

ifneq ($(GATOR_WITH_MALI_SUPPORT),)
gator-y +=	gator_events_mali.o
endif

gator-$(CONFIG_ARM) +=	gator_events_armv6.o \
			gator_events_armv7.o \
			gator_events_pl310.o

$(obj)/gator_main.o: gator_events.h

clean-files := gator_events.h

       chk_events.h = :
 quiet_chk_events.h = echo '  CHK     $@'
silent_chk_events.h = :
gator_events.h: FORCE
	@$($(quiet)chk_events.h)
	$(Q)cd $(obj) ; $(CONFIG_SHELL) $(obj)/gator_events.sh $@

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
