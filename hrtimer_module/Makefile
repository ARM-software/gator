ifneq ($(KERNELRELEASE),)

obj-m := hrtimer_module.o

else

all:
	@echo
	@echo "usage:"
	@echo "      make -C <kernel_build_dir> M=\`pwd\` ARCH=arm CROSS_COMPILE=<...> modules"
	@echo
	$(error)

clean:
	rm -f *.o .*.cmd modules.order Module.symvers hrtimer_module.ko hrtimer_module.mod.c
	rm -rf .tmp_versions

endif
