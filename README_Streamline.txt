
*** Purpose ***

Instructions on setting up ARM Streamline on the target.
The gator driver and gator daemon are required to run on the ARM Linux target in order for ARM Streamline to operate. A new early access feature allows the gator daemon can run without the gator driver by using userspace APIs with reduced functionality when using Linux 3.4 or later.
The driver should be built as a module and the daemon must run with root permissions on the target.

*** Introduction ***

A Linux development environment with cross compiling tools is most likely required, depending on what is already created and provided.
-For users, the ideal environment is to be given a BSP with gatord and gator.ko already running on a properly configured kernel. In such a scenario, a development environment is not needed, root permission may or may not be needed (gatord must be executed with root permissions but can be automatically started, see below), and the user can run Streamline and profile the system without any setup.
-The ideal development environment has the kernel source code available to be rebuilt, usually by cross-compiling on a host machine. This environment allows the greatest flexibility in configuring the kernel and building the gator driver module.
-However, it is possible that a user/developer has a kernel but does not have the source code. In this scenario it may or may not be possible to obtain a valid profile.
	-First, check if the kernel has the proper configuration options (see below). Profiling cannot occur using a kernel that is not configured properly, a new kernel must be created. See if /proc/config.gz exists on the target.
	-Second, given a properly configured kernel, check if the filesystem contains the kernel source/headers, which can be used to re-create the gator driver. These files may be located in different areas, but common locations are /lib/modules/ and /usr/src.
	-If the kernel is not properly configured or sources/headers are not available, the developer is on their own and kernel creation is beyond the scope of this document. Note: It is possible for a module to work when compiled against a similar kernel source code, though this is not guaranteed to work due to differences in kernel structures, exported symbols and incompatible configuration parameters.
	-If the target is running Linux 3.4 or later the kernel driver is not required and userspace APIs will be used instead.

*** Kernel configuration ***

menuconfig options (depending on the kernel version, the location of these configuration settings within menuconfig may differ)
- General Setup
  - Kernel Performance Events And Counters
    - [*] Kernel performance events and counters (enables CONFIG_PERF_EVENTS)
  - [*] Profiling Support (enables CONFIG_PROFILING)
- Kernel Features
  - [*] High Resolution Timer Support (enables CONFIG_HIGH_RES_TIMERS)
  - [*] Use local timer interrupts (only required for SMP and for version before Linux 3.12, enables CONFIG_LOCAL_TIMERS)
  - [*] Enable hardware performance counter support for perf events (enables CONFIG_HW_PERF_EVENTS)
- CPU Power Management
  - CPU Frequency scaling
    - [*] CPU Frequency scaling (enables CONFIG_CPU_FREQ)
- Kernel hacking
  - [*] Compile the kernel with debug info (optional, enables CONFIG_DEBUG_INFO)
  - [*] Tracers
    - [*] Trace process context switches and events (#)

(#) The "Trace process context switches and events" is not the only option that enables tracing (CONFIG_GENERIC_TRACER or CONFIG_TRACING) and may not be visible in menuconfig as an option if other trace configurations are enabled. Other trace configurations being enabled is sufficient to turn on tracing.

The configuration options:
CONFIG_GENERIC_TRACER or CONFIG_TRACING
CONFIG_PROFILING
CONFIG_HIGH_RES_TIMERS
CONFIG_LOCAL_TIMERS (for SMP systems)
CONFIG_PERF_EVENTS and CONFIG_HW_PERF_EVENTS (kernel versions 3.0 and greater)
CONFIG_DEBUG_INFO (optional, used for analyzing the kernel)
CONFIG_CPU_FREQ (optional, provides frequency setting of the CPU)

These may be verified on a running system using /proc/config.gz (if this file exists) by running 'zcat /proc/config.gz | grep <option>'. For example, confirming that CONFIG_PROFILING is enabled
	> zcat /proc/config.gz | grep CONFIG_PROFILING
	CONFIG_PROFILING=y

If a device tree is used it must include the pmu bindings, see Documentation/devicetree/bindings/arm/pmu.txt for details.

*** Checking the gator requirements ***

(optional) Use the hrtimer_module utility to validate the kernel High Resolution Timer requirement.

*** Building the gator module ***

To create the gator.ko module,
	tar xzf /path/to/DS-5/arm/gator/driver-src/gator-driver.tar.gz
	cd gator-driver
	make -C <kernel_build_dir> M=`pwd` ARCH=arm CROSS_COMPILE=<...> modules
for example when using the linaro-toolchain-binaries
	make -C /home/username/kernel_2.6.32/ M=`pwd` ARCH=arm CROSS_COMPILE=/home/username/gcc-linaro-arm-linux-gnueabihf-4.7-2013.01-20130125_linux/bin/arm-linux-gnueabihf- modules
If successful, a gator.ko module should be generated

It is also possible to integrate the gator.ko module into the kernel build system
	cd /path/to/kernel/build/dir
	cd drivers
	mkdir gator
	cp -r /path/to/gator/driver-src/* gator
Edit Makefile in the kernel drivers folder and add this to the end
	obj-$(CONFIG_GATOR)		+= gator/
Edit Kconfig in the kernel drivers folder and add this before the last endmenu
	source "drivers/gator/Kconfig"
You can now select gator when using menuconfig while configuring the kernel and rebuild as directed

*** Use the prebuilt gator daemon ***

A prebuilt gator daemon is provided at /path/to/DS-5/arm/gator/gatord. This gator daemon should work in most cases so building the gator daemon is only required if the prebuilt gator daemon doesn't work.
To improve portablility gatord is statically compiled against musl libc from http://www.musl-libc.org/releases/musl-1.0.2.tar.gz instead of glibc. The gator daemon will work correctly with either glibc or musl.

*** Building the gator daemon ***

tar -xzf /path/to/DS-5/arm/gator/daemon-src/gator-daemon.tar.gz
For Linux targets,
	cd gator-daemon
	make CROSS_COMPILE=<...> # For ARMv7 targets
	make -f Makefile_aarch64 CROSS_COMPILE=<...> # For ARMv8 targets
	gatord should now be created
For Android targets (install the android ndk, see developer.android.com)
	mv gator-daemon jni
	ndk-build
		or execute /path/to/ndk/ndk-build if the ndk is not on your path
	gatord should now be created and located in libs/armeabi
	If you get an error like the following, upgrade to a more recent version of the android ndk
		jni/PerfGroup.cpp: In function 'int sys_perf_event_open(perf_event_attr*, pid_t, int, int, long unsigned int)':
		jni/PerfGroup.cpp:36:17: error: '__NR_perf_event_open' was not declared in this scope

*** Running gator ***

Load the kernel onto the target and copy gatord and gator.ko into the target's filesystem.
Ensure gatord has execute permissions
	chmod +x gatord
gator.ko must be located in the same directory as gatord on the target or the location specified with the -m option or already insmod'ed.
With root privileges, run the daemon
	sudo ./gatord &
Note: gatord requires libstdc++.so.6 which is usually supplied by the Linux distribution on the target. A copy of libstdc++.so.6 is available in the DS-5 Linux example distribution.
If gator.ko is not loaded and is not in the same directory as gatord when using Linux 3.4 or later, gatord can run without gator.ko by using userspace APIs. Not all features are supported by userspace gator. If /dev/gator/version does not exist after starting gatord it is running userspace gator.

*** Customizing the l2c-310 Counter ***

The l2c-310 counter in gator_events_l2c-310.c contains hard coded offsets where the L2 cache counter registers are located.  This offset can also be configured via a module parameter specified when gator.ko is loaded, ex:
	insmod gator.ko l2c310_addr=<offset>
Further, the l2c-310 counter can be disabled by providing an offset of zero, ex:
	insmod gator.ko l2c310_addr=0

*** CCN-504 ***

CCN-504 is disabled by default. To enable CCN-504, insmod gator module with the ccn504_addr=<addr> parameter where addr is the base address of the CCN-504 configuration register space (PERIPHBASE), ex: insmod gator.ko ccn504_addr=0x2E000000.

*** Compiling an application or shared library ***

Recommended compiler settings:
	"-g": Debug information, such as line numbers, needed for best analysis results.
	"-fno-inline": Speed improvement when processing the image files and most accurate analysis results.
	"-fno-omit-frame-pointer": ARM EABI frame pointers allow recording of the call stack with each sample taken when in ARM state (i.e. not -mthumb).
	"-marm": This option is required if your compiler is configured with --with-mode=thumb, otherwise call stack unwinding will not work.

*** Hardfloat EABI ***
Binary applications built for the soft or softfp ABI are not compatible on a hardfloat system. All soft/softfp applications need to be rebuilt for hardfloat. To see if your ARM compiler supports hardfloat, run "gcc -v" and look for --with-float=hard.
To compile for non-hardfloat targets it is necessary to add options '-marm -march=armv4t -mfloat-abi=soft'. It may also be necessary to provide a softfloat filesystem by adding the option --sysroot, ex: '--sysroot=../DS-5Examples/distribution/filesystem/armv5t_mtx'. The gatord makefile will do this when run as 'make SOFTFLOAT=1 SYSROOT=/path/to/sysroot'
The armv5t_mtx filesystem is provided as part of the "DS-5 Linux Example Distribution" package which can be downloaded from the DS-5 Downloads page.
Attempting to run an incompatible binary often results in the confusing error message "No such file or directory" when clearly the file exists.

*** Mali GPU ***

Streamline supports Mali-400, 450, T6xx, and T7xx series GPUs with hardware activity charts, hardware & software counters and an optional 'film strip' showing periodic framebuffer snapshots. Support is chosen at build time and only one type of GPU (and version of driver) is supported at once. For best results build gator in-tree at .../drivers/gator and use the menuconfig options. Details of what these mean or how to build out of tree below.

Mali-4xx:
  ___To add Mali-4xx support to gator___
  GATOR_WITH_MALI_SUPPORT=MALI_4xx                                               # Set by CONFIG_GATOR_MALI_4XXMP
  CONFIG_GATOR_MALI_PATH=".../path/to/Mali_DDK_kernel_files/src/devicedrv/mali"  # gator source needs to #include "linux/mali_linux_trace.h"
  GATOR_MALI_INTERFACE_STYLE=<3|4>                                               # 3=Mali-400 DDK >= r3p0-04rel0 and < r3p2-01rel3
                                                                                 # 4=Mali-400 DDK >= r3p2-01rel3
                                                                                 # (default of 4 set in gator-driver/gator_events_mali_4xx.c)
  ___To add the corresponding support to Mali___
  Userspace needs MALI_TIMELINE_PROFILING_ENABLED=1 MALI_FRAMEBUFFER_DUMP_ENABLED=1 MALI_SW_COUNTERS_ENABLED=1
  Kernel driver needs USING_PROFILING=1                                          # Sets CONFIG_MALI400_PROFILING=y
  See the DDK integration guide for more details (the above are the default in later driver versions)

Mali-T6xx/T7xx:
  ___To add Mali-T6xx support to gator___
  GATOR_WITH_MALI_SUPPORT=MALI_T6xx                                              # Set by CONFIG_GATOR_MALI_T6XX
  DDK_DIR=".../path/to/Mali_DDK_kernel_files"                                    # gator source needs access to headers under .../kernel/drivers/gpu/arm/...
                                                                                 # (default of . suitable for in-tree builds)
  ___To add the corresponding support to Mali___
  Userspace (scons) needs gator=1
  Kernel driver needs CONFIG_MALI_GATOR_SUPPORT=y
  See the DDK integration guide for more details

*** Polling /dev, /sys and /proc files ***
Gator supports reading arbitrary /dev, /sys and /proc files 10 times a second. It will either interpret the file contents as a number or use a POSIX extended regex to extract the number, see events-Filesystem.xml for examples.

*** Bugs ***

There is a bug in some Linux kernels where perf misidentifies the CPU type. To see if you are affected by this, run ls /sys/bus/event_source/devices/ and verify the listed processor type matches what is expected. For example, an A9 should show the following.
	# ls /sys/bus/event_source/devices/
	ARMv7_Cortex_A9  breakpoint  software  tracepoint
To work around the issue try upgrading to a later kernel or comment out the gator_events_perf_pmu_cpu_init(gator_cpu, type); call in gator_events_perf_pmu.c

There is a bug in some Linux kernels where an Oops may occur when using userspace gator and a core is offlined. The fix was merged into mainline in 3.14-rc5, see http://git.kernel.org/tip/e3703f8cdfcf39c25c4338c3ad8e68891cca3731, and as been backported to older kernels.

If you see this error when using SELinux, ex: Android 4.4 or later
	# ./gatord
	Unable to load (insmod) gator.ko driver:
	  >>> gator.ko must be built against the current kernel version & configuration
	  >>> See dmesg for more details
	# dmesg
	...
	<7>[ 6745.475110] SELinux: initialized (dev gatorfs, type gatorfs), not configured for labeling
	<5>[ 6745.477434] type=1400 audit(1393005053.336:10): avc:  denied  { mount } for  pid=1996 comm="gatord-main" name="/" dev="gatorfs" ino=8733 scontext=u:r:shell:s0 tcontext=u:object_r:unlabeled:s0 tclass=filesystem
disable SELinux so that gatorfs can be mounted by running
	# setenforce 0
Once gator is started, SELinux can be reenabled

*** Profiling the kernel (optional) ***

CONFIG_DEBUG_INFO must be enabled, see "Kernel configuration" section above.
Use vmlinux as the image for debug symbols in Streamline.
Drivers may be profiled using this method by statically linking the driver into the kernel image or adding the driver as an image to Streamline.
To perform kernel stack unwinding and module unwinding, edit the Makefile to enable GATOR_KERNEL_STACK_UNWINDING and rebuild gator.ko or run "echo 1 > /sys/module/gator/parameters/kernel_stack_unwinding" as root on the target after gatord is started.

*** Automatically start gator on boot (optional) ***

cd /etc/init.d
vi rungator.sh
	#!/bin/bash
	/path/to/gatord &
update-rc.d rungator.sh defaults

*** GPL License ***

For license information, please see the file LICENSE after unzipping driver-src/gator-driver.tar.gz.
The prebuilt gatord uses musl from http://www.musl-libc.org/releases/musl-1.0.2.tar.gz for musl license information see the COPYRIGHT file in the musl tar file.
