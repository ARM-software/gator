
*** Purpose ***

Instructions on setting up ARM Streamline on the target.
The gator driver and gator daemon are required to run on the ARM linux target in order for ARM Streamline to operate.
The driver should be built as a module and the daemon must run with root permissions on the target.

*** Introduction ***

A linux development environment with cross compiling tools is most likely required, depending on what is already created and provided.
-For users, the ideal environment is to be given a BSP with gatord and gator.ko already running on a properly configured kernel. In such a scenario, a development environment is not needed, root permission may or may not be needed (gatord must be executed with root permissions but can be automatically started, see below), and the user can run Streamline and profile the system without any setup.
-The ideal development environment has the kernel source code available to be rebuilt and executed on the target. This environment allows the greatest flexibility in configuring the kernel and building the gator driver module.
-However, it is possible that a user/developer has a kernel but does not have the source code. In this scenario it may or may not be possible to obtain a valid profile.
	-First, check if the kernel has the proper configuration options (see below). Profiling cannot occur using a kernel that is not configured properly, a new kernel must be created.
	-Second, given a properly configured kernel, check if the filesystem contains the kernel source/headers, which can be used to re-create the gator driver.
	-If the kernel is not properly configured or sources/headers are not available, the developer is on their own and kernel creation is beyond the scope of this document. Note: It is possible for a module to work when compiled against a similar kernel source code, though this is not guaranteed to work due to differences in kernel structures, exported symbols and incompatible configuration parameters.

*** Preparing and building the kernel ***

cd into the root source dir of the linux kernel
if your target has never been configured, choose the appropriate configuration for your target
	make ARCH=arm CROSS_COMPILE=${CROSS_TOOLS}/bin/arm-none-linux-gnueabi- <platform_defconfig>
make ARCH=arm CROSS_COMPILE=${CROSS_TOOLS}/bin/arm-none-linux-gnueabi- menuconfig

Required Kernel Changes (depending on the kernel version, the location of these configuration settings within menuconfig may be different)
- General Setup
  - [*] Profiling Support
- Kernel hacking
  - [*] Tracers
    - [*] Trace process context switches and events
- Kernel Features
  - [*] High Resolution Timer Support
  - [*] Use local timer interrupts (only required for SMP)

The "context switches and events" option will not be available if other trace configurations are enabled. Other trace configurations being enabled is sufficient to turn on context switches and events.

Optional Kernel Changes (depending on the kernel version, the location of these configuration settings within menuconfig may be different)
Note: Configurations may not be supported on all targets
- System Type
  - [*] <SoC name> debugging peripherals (enable core performance counters on supported SoCs)  /* kernels before 2.6.35 */

make -j5 ARCH=arm CROSS_COMPILE=${CROSS_TOOLS}/bin/arm-none-linux-gnueabi- uImage

*** Building the gator module ***

To create the gator.ko module,
	cd /ds-5-install-directory/arm/gator/src
	tar xzf gator-driver.tar.gz
	cd gator-driver
	make -C <kernel_build_dir> M=`pwd` ARCH=arm CROSS_COMPILE=<...> modules
for example
	make -C /home/username/kernel_2.6.32/ M=`pwd` ARCH=arm CROSS_COMPILE=/home/username/CodeSourcery/Sourcery_G++_Lite/bin/arm-none-linux-gnueabi- modules
If successful, a gator.ko module should be generated

*** Compiling an application or shared library ***

Recommended compiler settings:
	"-g": Debug symbols needed for best analysis results.
	"-fno-inline": Speed improvement when processing the image files and most accurate analysis results.
	"-fno-omit-frame-pointer": ARM EABI frame pointers (Code Sourcery cross compiler) allow the call stack to be recorded with each sample taken when in ARM state (i.e. not -mthumb).

*** Running gator ***

Load the kernel onto the target and copy gatord and gator.ko into the target's filesystem.
gatord is located in <installdir>/arm/gator/.
Ensure gatord has execute permissions
	chmod +x gatord
gator.ko must be located in the same directory as gatord on the target.
With root privileges, run the daemon
	sudo ./gatord &

*** Profiling the kernel (optional) ***

make ARCH=arm CROSS_COMPILE=$(CROSS_TOOLS}/bin/arm-none-linux-gnueabi- menuconfig
- Kernel Hacking
  - [*] Compile the kernel with debug info

make -j5 ARCH=arm CROSS_COMPILE=${CROSS_TOOLS}/bin/arm-none-linux-gnueabi- uImage
Use vmlinux as the image for debug symbols in Streamline.
Drivers may be profiled using this method by statically linking the driver into the kernel image or adding the module as an image.
Note that the gator driver does not perform kernel call stack recording.

*** Automatically start gator on boot (optional) ***

cd /etc/init.d
vi rungator.sh
	#!/bin/bash
	/path/to/gatord &
update-rc.d rungator.sh defaults

*** GPL License ***

For license information, please see the file LICENSE.
