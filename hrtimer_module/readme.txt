The source for the kernel is required
From the module directory,
make -C /path/to/kernel/source M=`pwd` modules
a .ko file should be produced

to run the module,
insmod hrtimer_module.ko

to stop the module,
rmmod hrtimer_module.ko

