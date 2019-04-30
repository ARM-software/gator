# Gator daemon, driver and related tools

The source code for `gatord`, `gator.ko` `gator.py` and related tools.

## License

* `daemon`, `driver` `python`, `hrtimer_module`, `notify` and `setup` are provided under GPL-2.0-only. See [daemon/COPYING], [driver/COPYING], [python/COPYING], [hrtimer_module/COPYING], [notify/COPYING] and [setup/COPYING] respectively.
* `annotate` is provided under the BSD-3-Clause license. See [annotate/LICENSE].

This project contains code from other projects listed below. The original license text is included in those source files.

* `libsensors` source code in [daemon/libsensors] licensed under LGPL-2.1-or-later
* `mxml` source code in [daemon/mxml] licensed under LGPL-2.0 WITH Mini-XML-exception
* `perf_event.h` from Linux userspace kernel headers in [daemon/k] licensed under GPL-2.0-only WITH Linux-syscall-note

The pre-built `gatord` shipped with Streamline uses [musl]. For musl license information see the COPYRIGHT file shipped with Streamline, or <https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT>

## Contributing

Contributions are accepted under the same license as the associated subproject with developer sign-off as described in [Contributing].

## Purpose

Instructions on setting up Arm Streamline on the target.

A target agent (gator) is required to run on the Arm Linux target in order for Arm Streamline to operate. Gator may run in kernel space or user space mode, though user space gator requires Linux 3.4 or later.

The driver should be built as a module and the daemon must run with root permissions on the target.

## Introduction

A Linux development environment with cross compiling tools is most likely required, depending on what is already created and provided.
- For users, the ideal environment is to be given a BSP with gatord and gator.ko already running on a properly configured kernel. In such a scenario, a development environment is not needed, root permission may or may not be needed (gatord must be executed with root permissions but can be automatically started, see below), and the user can run Streamline and profile the system without any setup.
- The ideal development environment has the kernel source code available to be rebuilt, usually by cross-compiling on a host machine. This environment allows the greatest flexibility in configuring the kernel and building the gator driver module.
- However, it is possible that a user/developer has a kernel but does not have the source code. In this scenario it may or may not be possible to obtain a valid profile.
    - First, check if the kernel has the proper configuration options (see below). Profiling cannot occur using a kernel that is not configured properly, a new kernel must be created. See if `/proc/config.gz` exists on the target.
    - Second, given a properly configured kernel, check if the filesystem contains the kernel source/headers, which can be used to re-create the gator driver. These files may be located in different areas, but common locations are `/lib/modules/` and `/usr/src/`.
    - If the kernel is not properly configured or sources/headers are not available, the developer is on their own and kernel creation is beyond the scope of this document. Note: It is possible for a module to work when compiled against a similar kernel source code, though this is not guaranteed to work due to differences in kernel structures, exported symbols and incompatible configuration parameters.
    - If the target is running Linux 3.4 or later the kernel driver is not required and userspace APIs will be used instead.

## Kernel configuration

menuconfig options (depending on the kernel version, the location of these configuration settings within menuconfig may differ)
- General Setup
  - Timers subsystem
    - [*] High Resolution Timer Support (enables CONFIG_HIGH_RES_TIMERS)
  - Kernel Performance Events And Counters
    - [*] Kernel performance events and counters (enables CONFIG_PERF_EVENTS)
  - [*] Profiling Support (enables CONFIG_PROFILING)
- [*] Enable loadable module support (enables CONFIG_MODULES, needed unless the gator driver is built into the kernel)
  - [*] Module unloading (enables MODULE_UNLOAD)
- Kernel Features
  - [*] Use local timer interrupts (only required for SMP and for version before Linux 3.12, enables CONFIG_LOCAL_TIMERS)
  - [*] Enable hardware performance counter support for perf events (enables CONFIG_HW_PERF_EVENTS)
- CPU Power Management
  - CPU Frequency scaling
    - [*] CPU Frequency scaling (enables CONFIG_CPU_FREQ)
- Device Drivers
  - Graphics support
    - Arm GPU Configuration
      - Mali Midgard series support
        - [*] Streamline Debug support (enables CONFIG_MALI_GATOR_SUPPORT needed as part of Mali Midgard support)
- Kernel hacking
  - [*] Compile the kernel with debug info (optional, enables CONFIG_DEBUG_INFO)
  - [*] Tracers
    - [*] Trace process context switches and events (#)

(#) The "Trace process context switches and events" is not the only option that enables tracing (CONFIG_GENERIC_TRACER or CONFIG_TRACING as well as CONFIG_CONTEXT_SWITCH_TRACER) and may not be visible in menuconfig as an option if other trace configurations are enabled. Other trace configurations being enabled is sufficient to turn on tracing.

The configuration options:
- CONFIG_MODULES and MODULE_UNLOAD (not needed if the gator driver is built into the kernel)
- CONFIG_GENERIC_TRACER or CONFIG_TRACING
- CONFIG_CONTEXT_SWITCH_TRACER
- CONFIG_PROFILING
- CONFIG_HIGH_RES_TIMERS
- CONFIG_LOCAL_TIMERS (for SMP systems and kernel versions before 3.12)
- CONFIG_PERF_EVENTS and CONFIG_HW_PERF_EVENTS (kernel versions 3.0 and greater)
- CONFIG_DEBUG_INFO (optional, used for analyzing the kernel)
- CONFIG_CPU_FREQ (optional, provides frequency setting of the CPU)
- CONFIG_MALI_GATOR_SUPPORT (needed as part of Mali Midgard support)

These may be verified on a running system using `/proc/config.gz` (if this file exists) by running `zcat /proc/config.gz | grep <option>`. For example, confirming that CONFIG_PROFILING is enabled
```
> zcat /proc/config.gz | grep CONFIG_PROFILING
CONFIG_PROFILING=y
```

If a device tree is used it must include the pmu bindings, see Documentation/devicetree/bindings/arm/pmu.txt for details.

## Checking the gator requirements

(optional) Use the hrtimer_module utility to validate the kernel High Resolution Timer requirement.

## Building the gator module

To create the gator.ko module,
```
cp -r /path/to/streamline/gator/driver .
cd driver
make -C <kernel_build_dir> M=`pwd` ARCH=arm CROSS_COMPILE=<...> modules
```
whenever possible, use the same toolchain the kernel was built with when building gator.ko

for example when using the linaro-toolchain-binaries
```
make -C /home/username/kernel_2.6.32/ M=`pwd` ARCH=arm CROSS_COMPILE=/home/username/gcc-linaro-arm-linux-gnueabihf-4.7-2013.01-20130125_linux/bin/arm-linux-gnueabihf- modules
```
If successful, a gator.ko module should be generated

It is also possible to integrate the gator.ko module into the kernel build system
```
cd /path/to/kernel/build/dir
cd drivers
mkdir gator
cp -r /path/to/gator/driver-src/* gator
```
Edit Makefile in the kernel drivers folder and add this to the end
```
obj-$(CONFIG_GATOR)     += gator/
```
Edit Kconfig in the kernel drivers folder and add this before the last endmenu
```
source "drivers/gator/Kconfig"
```
You can now select gator when using menuconfig while configuring the kernel and rebuild as directed

## Use the pre-built gator daemon

The Streamline Setup Target tool will automatically install a pre-built gator daemon. This gator daemon should work in most cases so building the gator daemon is only required if the pre-built gator daemon doesn't work.

To improve portablility gatord is statically compiled against musl libc from http://www.musl-libc.org/download.html instead of glibc. The gator daemon will work correctly with either glibc or musl.

## Building the gator daemon

Building gatord has the following requirements:

- C++11 supporting compiler; GCC-4.7 is the minimum required
- Linux based build system


```
cp -r /path/to/streamline/gator/daemon .
```
For Linux targets,
```
cd daemon
make CROSS_COMPILE=<...>
```
gatord should now be created.

For Android targets (install the Android NDK appropriate for your target (ndk32 for 32-bit targets and ndk64 for 64-bit targets), see developer.android.com)
```
mv daemon jni
ndk-build
```
or execute `/path/to/ndk/ndk-build` if the ndk is not on your path.

gatord should now be created and located in libs/armeabi.

If you get an error like the following, upgrade to a more recent version of the android ndk
```
jni/PerfGroup.cpp: In function 'int sys_perf_event_open(perf_event_attr*, pid_t, int, int, long unsigned int)':
jni/PerfGroup.cpp:36:17: error: '__NR_perf_event_open' was not declared in this scope
```
To build gatord for aarch64 edit `jni/Application.mk` and replace `armeabi-v7a` with `arm64-v8a`. To build for ARM11 `jni/Application.mk` and replace `armeabi-v7a` with `armeabi`.


## Running gator

### With the kernel module gator.ko

- Copy gatord and gator.ko into the target's filesystem.
- Ensure gatord has execute permissions:
  `chmod +x gatord`
- gator.ko must be located in the same directory as gatord on the target or the location specified with the -m option or it must already be insmod'ed.
- The daemon must be run with root privileges:
  `sudo ./gatord &`

### With the Linux perf API

- Copy gatord into the target's filesystem.
- Ensure gatord has execute permissions:
  `chmod +x gatord`
- The daemon must be run with root privileges:
  `sudo ./gatord &`

This configuration requires Linux 3.4 or later with a correctly configured kernel. Not all features are supported by userspace gator.

### As a non-root user

- Copy gatord into the target's filesystem.
- Ensure gatord has execute permissions:
  `chmod +x gatord`
- Run the daemon:
  `./gatord &`

This configuration provides a reduced set of software only CPU counters such as CPU utilization and process statistics, as well as Mali hardware counters on supported Mali platforms.

## Customizing the l2c-310 Counter

The l2c-310 counter in `gator_events_l2c-310.c` contains hard coded offsets where the L2 cache counter registers are located. This offset can also be configured via a module parameter specified when gator.ko is loaded, ex:
`insmod gator.ko l2c310_addr=<offset>`
Further, the l2c-310 counter can be disabled by providing an offset of zero, ex:
`insmod gator.ko l2c310_addr=0`

## Perf PMU support

To check the perf PMUs support by your kernel, run
`ls /sys/bus/event_source/devices/`
If you see something like `ARMv7_Cortex_A##` this indicates A## support. If you see `CCI_400` this indicates CCI-400 support. If you see `ccn`, it indicates CCN support.

## CCN

CCN requires a perf driver to work. The necessary perf driver has been merged into Linux 3.17 but can be backported to previous versions (see https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/diff/?id=a33b0daab73a0e08cc04459dd44b0121a8e8f81b and later bugfixes)

## Compiling an application or shared library

Recommended compiler settings:
- `-g`: Debug information, such as line numbers, needed for best analysis results.
- `-fno-inline`: Speed improvement when processing the image files and most accurate analysis results.
- `-fno-omit-frame-pointer`: Arm EABI frame pointers allow recording of the call stack with each sample taken when in Arm state (i.e. not `-mthumb`).
- `-marm`: This option is required for ARMv7 and earlier if your compiler is configured with `--with-mode=thumb`, otherwise call stack unwinding will not work.

For Android ART, passing `--no-strip-symbols` to dex2oat will result in function names but not line numbers to be included in the dex files. This can be done by running `setprop dalvik.vm.dex2oat-flags --no-strip-symbols` on the device and then regenerating the dex files.

## Mali GPU

Streamline supports Mali-400, 450, T6xx, T7xx, and T8xx series GPUs with hardware activity charts, hardware & software counters and an optional Filmstrip showing periodic framebuffer snapshots. Support is chosen at build time and only one type of GPU (and version of driver) is supported at once. For best results build gator in-tree at `.../drivers/gator` and use the menuconfig options. Details of what the menuconfig options mean or how to build out of tree, if you choose, is described below.

### Mali-4xx
```
  GATOR_WITH_MALI_SUPPORT=MALI_4xx                                               # Set by CONFIG_GATOR_MALI_4XXMP
  CONFIG_GATOR_MALI_PATH=".../path/to/Mali_DDK_kernel_files/src/devicedrv/mali"  # gator source needs to #include "linux/mali_linux_trace.h"
  GATOR_MALI_INTERFACE_STYLE=<3|4>                                               # 3=Mali-400 DDK >= r3p0-04rel0 and < r3p2-01rel3
                                                                                 # 4=Mali-400 DDK >= r3p2-01rel3
                                                                                 # (default of 4 set in streamline/gator/driver/gator_events_mali_4xx.c)
```
To add the corresponding support to Mali:
```
  Userspace needs MALI_TIMELINE_PROFILING_ENABLED=1 MALI_FRAMEBUFFER_DUMP_ENABLED=1 MALI_SW_COUNTERS_ENABLED=1
  Kernel driver needs USING_PROFILING=1                                          # Sets CONFIG_MALI400_PROFILING=y
  See the DDK integration guide for more details (the above are the default in later driver versions)
```

### Mali-T6xx/T7xx/T8xx (Midgard)
```
  GATOR_WITH_MALI_SUPPORT=MALI_MIDGARD                                           # Set by CONFIG_GATOR_MALI_MIDGARD
  DDK_DIR=".../path/to/Mali_DDK_kernel_files"                                    # gator source needs access to headers under .../kernel/drivers/gpu/arm/...
                                                                                 # (default of . suitable for in-tree builds)
```
To add the corresponding support to Mali:
```
  Userspace (scons) needs gator=1
  Kernel driver needs CONFIG_MALI_GATOR_SUPPORT=y
  See the DDK integration guide for more details
```

## Polling /dev, /sys and /proc files

Gator supports reading arbitrary `/dev`, `/sys` and `/proc` files 10 times a second. It will either interpret the file contents as a number or use a POSIX extended regex to extract the number, see `events-Filesystem.xml` for examples.

## Bugs

Kernels with `CONFIG_CPU_PM` enabled may produce invalid results on kernel versions prior to 4.6. The problem manifests as counters not showing any data, large spikes and non-sensible values for counters (e.g. Cycle Counter reading as *very* high).
The problem is visible with both gator.ko and user space gator. This issue stems from the fact that the kernel PMU driver does not save/restore state when the CPU is powered down/up. This issue is fixed in 4.6 so to resolve the issue either upgrade to a later kernel, or apply the fix to an older kernel.
The patch for 4.6 that resolves the issue is found here https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=da4e4f18afe0f3729d68f3785c5802f786d36e34 - this patch has been tested as applying cleanly to 4.4 kernel and it may be possible to back port it to other versions as well.
Users of this patch may also need to apply https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=cbcc72e037b8a3eb1fad3c1ae22021df21c97a51 as well.

User space gator is in beta release with known issues. Please note that based on the kernel version and target configuration, the data presented may be incorrect and unexpected behavior can occur including crashing the target kernel. If you experience any of these issues, please use kernel space gator.

There is a bug in some Linux kernels where an Oops may occur when a core is offlined (user space gator only). The fix was merged into mainline in 3.14-rc5, see http://git.kernel.org/tip/e3703f8cdfcf39c25c4338c3ad8e68891cca3731, and has been backported to older kernels (3.4.83, 3.10.33, 3.12.14 and 3.13.6).

`CPU PMU: CPUx reading wrong counter -1` in dmesg (user space gator only). To work around, update to the latest Linux kernel or use kernel space gator.

Scheduler switch resolutions are on exact millisecond boundaries (user space gator only). To work around, update to the latest Linux kernel or use kernel space gator.

There is a bug in some Linux kernels where perf misidentifies the CPU type. To see if you are affected by this, run` ls /sys/bus/event_source/devices/` and verify the listed processor type matches what is expected. For example, an A9 should show the following.
```
# ls /sys/bus/event_source/devices/
ARMv7_Cortex_A9  breakpoint  software  tracepoint
```
To work around the issue try upgrading to a later kernel or comment out the `gator_events_perf_pmu_cpu_init(gator_cpu, type);` call in `gator_events_perf_pmu.c`

If you see one of these errors when using SELinux, ex: Android 4.4 or later
`Unable to mount the gator filesystem needed for profiling` or `Unable to load (insmod) gator.ko driver`
with the following dmesg output,
```
<7>[ 6745.475110] SELinux: initialized (dev gatorfs, type gatorfs), not configured for labeling
<5>[ 6745.477434] type=1400 audit(1393005053.336:10): avc:  denied  { mount } for  pid=1996 comm="gatord-main" name="/" dev="gatorfs" ino=8733 scontext=u:r:shell:s0 tcontext=u:object_r:unlabeled:s0 tclass=filesystem
```
disable SELinux so that gatorfs can be mounted by running
`# setenforce 0`
Once gator is started, SELinux can be reenabled

On some versions of Android, the Mali Filmstrip may not work and produces a dmesg output similar to
```
<4>[  585.367411] type=1400 audit(1421862808.850:48): avc: denied { search } for pid=3681 comm="mali-renderer" name="/" dev="gatorfs" ino=22378 scontext=u:r:untrusted_app:s0 tcontext=u:object_r:unlabeled:s0 tclass=dir
```
To work around this issue, use `streamline_annotate.h` and `streamline_annotate.c` from DS-5 v5.20 or later, or disable SELinux by running
`# setenforce 0`

On some versions of Android, annotations may not work unless SELinux is disabled by running
`# setenforce 0`

Some targets do not correctly emit uevents when cores go on/offline. This will cause CPU Activity with user space gator to be either 0% or 100% on a given core and the Heat Map may show a large number of unresolved processes. To work around this issue, use kernel space gator. To test for this run
`# ./gatord -d | grep uevent`
When cores go on/offline with user space gator something similar to the following should be emitted
```
INFO: read(UEvent.cpp:61): uevent: offline@/devices/system/cpu/cpu1
INFO: read(UEvent.cpp:61): uevent: online@/devices/system/cpu/cpu1
```
The cores that are on/offline can be checked by running
`# cat /sys/devices/system/cpu/cpu*/online`
This issue affects a given target if the on/offline cores shown by the cat command change but no cpu uevent is emitted.

On some older versions of Android, the following issue may occur when starting gatord when using ndk-build
```
# ./gatord
[1] + Stopped (signal)        ./gatord
#
[1]   Segmentation fault      ./gatord
#
```
Starting with Android-L only position independent executables (pie) are supported, but some older versions of Android do not support them. To avoid this issue, modify Android.mk and remove the references to pie.

## Profiling the kernel (optional)

CONFIG_DEBUG_INFO must be enabled, see "Kernel configuration" section above.

Use vmlinux as the image for debug symbols in Streamline.

Drivers may be profiled using this method by statically linking the driver into the kernel image or adding the driver as an image to Streamline.

To perform kernel stack unwinding and module unwinding, edit the Makefile to enable GATOR_KERNEL_STACK_UNWINDING and rebuild gator.ko or run `echo 1 > /sys/module/gator/parameters/kernel_stack_unwinding` as root on the target after gatord is started.

## Preventing gator.ko from onlining all cpus

By default gator.ko will automatically online all cpus in the system in order to discover their properties. This is required on systems where perf uses the generic PMU driver in order to be able to correctly map all cores to their associated set of counters.

This behaviour can be disabled either by modprobing gator.ko passing the parameter `disable_cpu_onlining=1` or by enabling `CONFIG_GATOR_DO_NOT_ONLINE_CORES_AT_STARTUP`.

## Automatically start gator on boot (optional)

```
cd /etc/init.d
cat << EOF > rungator.sh
#!/bin/bash
/path/to/gatord &
EOF
update-rc.d rungator.sh defaults
```

[Contributing]: Contributing.md
[annotate/LICENSE]: annotate/LICENSE
[daemon/COPYING]: daemon/COPYING
[daemon/k]: daemon/k
[daemon/libsensors]: daemon/libsensors
[daemon/mxml]: daemon/mxml
[driver/COPYING]: driver/COPYING
[hrtimer_module/COPYING]: hrtimer_module/COPYING
[musl]: http://www.musl-libc.org/download.html
[notify/COPYING]: notify/COPYING
[python/COPYING]: python/COPYING
[setup/COPYING]: setup/COPYING
