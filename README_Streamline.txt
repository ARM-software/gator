
*** Purpose ***

Instructions on setting up ARM Streamline on the target.
The gator driver and gator daemon are required to run on the ARM linux target in order for ARM Streamline to operate.
The driver should be built as a module and the daemon must run with root permissions on the target.

*** Introduction ***

A linux development environment with cross compiling tools is most likely required, depending on what is already created and provided.
-For users, the ideal environment is to be given a BSP with gatord and gator.ko already running on a properly configured kernel. In such a scenario, a development environment is not needed, root permission may or may not be needed (gatord must be executed with root permissions but can be automatically started, see below), and the user can run Streamline and profile the system without any setup.
-The ideal development environment has the kernel source code available to be rebuilt, usually by cross-compiling on a host machine. This environment allows the greatest flexibility in configuring the kernel and building the gator driver module.
-However, it is possible that a user/developer has a kernel but does not have the source code. In this scenario it may or may not be possible to obtain a valid profile.
	-First, check if the kernel has the proper configuration options (see below). Profiling cannot occur using a kernel that is not configured properly, a new kernel must be created. See if /proc/config.gz exists on the target.
	-Second, given a properly configured kernel, check if the filesystem contains the kernel source/headers, which can be used to re-create the gator driver. These files may be located in different areas, but common locations are /lib/modules/ and /usr/src.
	-If the kernel is not properly configured or sources/headers are not available, the developer is on their own and kernel creation is beyond the scope of this document. Note: It is possible for a module to work when compiled against a similar kernel source code, though this is not guaranteed to work due to differences in kernel structures, exported symbols and incompatible configuration parameters.

*** Kernel configuration ***

menuconfig options (depending on the kernel version, the location of these configuration settings within menuconfig may differ)
- General Setup
  - Kernel Performance Events And Counters
    - [*] Kernel performance events and counters (enables CONFIG_PERF_EVENTS)
  - [*] Profiling Support (enables CONFIG_PROFILING)
- Kernel Features
  - [*] High Resolution Timer Support (enables CONFIG_HIGH_RES_TIMERS)
  - [*] Use local timer interrupts (only required for SMP, enables CONFIG_LOCAL_TIMERS)
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

*** Checking the gator requirements ***

(optional) Use the hrtimer_module utility to validate the kernel High Resolution Timer requirement.

*** Building the gator module ***

To create the gator.ko module,
	cd /path/to/gator/driver-src
	tar xzf gator-driver.tar.gz
	cd gator-driver
	make -C <kernel_build_dir> M=`pwd` ARCH=arm CROSS_COMPILE=<...> modules
for example when using the linaro-toolchain-binaries
	make -C /home/username/kernel_2.6.32/ M=`pwd` ARCH=arm CROSS_COMPILE=/home/username/gcc-linaro-arm-linux-gnueabihf-4.7-2013.01-20130125_linux/bin/arm-linux-gnueabihf- modules
If successful, a gator.ko module should be generated

*** Building the gator daemon ***

cd /path/to/gator/daemon-src
tar -xzf gator-daemon.tar.gz (may need to issue with 'sudo')
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

*** Running gator ***

Load the kernel onto the target and copy gatord and gator.ko into the target's filesystem.
Ensure gatord has execute permissions
	chmod +x gatord
gator.ko must be located in the same directory as gatord on the target or the location specified with the -m option or already insmod'ed.
With root privileges, run the daemon
	sudo ./gatord &
Note: gatord requires libstdc++.so.6 which is usually supplied by the Linux distribution on the target. A copy of libstdc++.so.6 is available in the DS-5 Linux example distribution.

*** Customizing the l2c-310 Counter ***

The l2c-310 counter in gator_events_l2c-310.c contains hard coded offsets where the L2 cache counter registers are located.  This offset can also be configured via a module parameter specified when gator.ko is loaded, ex:
	insmod gator.ko l2c310_addr=<offset>
Further, the l2c-310 counter can be disabled by providing an offset of zero, ex:
	insmod gator.ko l2c310_addr=0

*** Compiling an application or shared library ***

Recommended compiler settings:
	"-g": Debug information, such as line numbers, needed for best analysis results.
	"-fno-inline": Speed improvement when processing the image files and most accurate analysis results.
	"-fno-omit-frame-pointer": ARM EABI frame pointers (Code Sourcery cross compiler) allow recording of the call stack with each sample taken when in ARM state (i.e. not -mthumb).
	"-marm": This option is required if your compiler is configured with --with-mode=thumb, otherwise call stack unwinding will not work.

*** Hardfloat EABI ***
Binary applications built for the soft or softfp ABI are not compatible on a hardfloat system. All soft/softfp applications need to be rebuilt for hardfloat. To see if your ARM compiler supports hardfloat, run "gcc -v" and look for --with-float=hard.
To compile for non-hardfloat targets it is necessary to add options '-marm -march=armv4t -mfloat-abi=soft'. It may also be necessary to provide a softfloat filesystem by adding the option --sysroot, ex: '--sysroot=../DS-5Examples/distribution/filesystem/armv5t_mtx'. The gatord makefile will do this when run as 'make SOFTFLOAT=1 SYSROOT=/path/to/sysroot'
The armv5t_mtx filesystem is provided as part of the "DS-5 Linux Example Distribution" package which can be downloaded from the DS-5 Downloads page.
Attempting to run an incompatible binary often results in the confusing error message "No such file or directory" when clearly the file exists.

*** Bugs ***

There is a bug in some Linux kernels where perf misidentifies the CPU type. To see if you are affected by this, run ls /sys/bus/event_source/devices/ and verify the listed processor type matches what is expected. For example, an A9 should show the following.

# ls /sys/bus/event_source/devices/
ARMv7_Cortex_A9  breakpoint  software  tracepoint

To work around the issue try upgrading to a later kernel or comment out the gator_events_perf_pmu_cpu_init(gator_cpu, type); call in gator_events_perf_pmu.c

*** Profiling the kernel (optional) ***

CONFIG_DEBUG_INFO must be enabled, see "Kernel configuration" section above.
Use vmlinux as the image for debug symbols in Streamline.
Drivers may be profiled using this method by statically linking the driver into the kernel image or adding the driver as an image to Streamline.
To perform kernel stack unwinding and module unwinding, edit the Makefile to enable GATOR_KERNEL_STACK_UNWINDING and rebuild gator.ko.

*** Automatically start gator on boot (optional) ***

cd /etc/init.d
vi rungator.sh
	#!/bin/bash
	/path/to/gatord &
update-rc.d rungator.sh defaults

*** GPL License ***

For license information, please see the file LICENSE after unzipping driver-src/gator-driver.tar.gz.

