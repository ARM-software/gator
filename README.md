# Gator daemon, driver and related tools

The source code for `gatord`, `gator.py` and related tools.

## License

* `daemon`, `python`, and `notify` are provided under GPL-2.0-only. See
   [daemon/COPYING], [python/COPYING], and [notify/COPYING] respectively.
* `annotate` is provided under the BSD-3-Clause license. See [annotate/LICENSE].

This project contains code from other projects listed below. The original license
text is included in those source files.

* `libsensors` source code in [daemon/libsensors] licensed under LGPL-2.1-or-later
* `mxml` source code in [daemon/mxml] licensed under
  APACHE-2.0 WITH Mini-XML-exception
* `perf_event.h` from Linux userspace kernel headers in [daemon/k] licensed
  under GPL-2.0-only WITH Linux-syscall-note

The pre-built `gatord` shipped with Streamline uses [musl]. For musl license
information see the COPYRIGHT file shipped with Streamline, or
<https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT>

## Contributing

Contributions are accepted under the same license as the associated subproject with
developer sign-off as described in [Contributing].

## Purpose

Instructions on setting up Arm Streamline on the target.

A target agent (gator) is required to run on the Arm Linux target in order for Arm
Streamline to operate. Gator requires Linux kernel version 3.4 or later.

## Introduction

A Linux development environment with cross compiling tools is most likely required,
depending on what is already created and provided.

## Kernel configuration

Gator uses the Linux Perf API (perf_event_open) for most of its data collection.
Additionally it will use ftrace tracepoints and some other common features such as
debugfs/sysfs.
Most users will not need to make any changes to their kernel configuration (and in
many cases they cannot) as most recent Android devices and Linux distributions
correctly configure their kernel with the required options.
If you are a system integrator, or compiling your own kernel, the following options
are required for correct functioning of Gator.

menuconfig options (depending on the kernel version, the location of these
configuration settings within menuconfig may differ)

- General Setup
  - Timers subsystem
    - [*] High Resolution Timer Support (enables CONFIG_HIGH_RES_TIMERS)
  - Kernel Performance Events And Counters
    - [*] Kernel performance events and counters (enables CONFIG_PERF_EVENTS)
  - [*] Profiling Support (enables CONFIG_PROFILING)
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

These may be verified on a running system using `/proc/config.gz`
(if this file exists) by running `zcat /proc/config.gz | grep <option>`.
For example, confirming that CONFIG_PROFILING is enabled
```
> zcat /proc/config.gz | grep CONFIG_PROFILING
CONFIG_PROFILING=y
```

If a device tree is used it must include the pmu bindings, see
Documentation/devicetree/bindings/arm/pmu.txt for details.

## Use the pre-built gator daemon

The Streamline Setup Target tool will automatically install a pre-built gator
daemon. This gator daemon should work in most cases so building the gator daemon is
only required if the pre-built gator daemon doesn't work.

To improve portablility gatord is statically compiled against musl libc from
http://www.musl-libc.org/download.html instead of glibc. The gator daemon will work
correctly with either glibc or musl.

## Building the gator daemon

Building gatord has the following requirements:

- C++11 supporting compiler
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

For Android targets (install the Android NDK appropriate for your target (ndk32 for
32-bit targets and ndk64 for 64-bit targets), see developer.android.com)
```
mv daemon jni
ndk-build
```
or execute `/path/to/ndk/ndk-build` if the ndk is not on your path.

gatord should now be created and located in libs/armeabi.

If you get an error like the following, upgrade to a more recent version of the
android ndk
```
jni/PerfGroup.cpp: In function 'int sys_perf_event_open(perf_event_attr*, pid_t, int, int, long unsigned int)':
jni/PerfGroup.cpp:36:17: error: '__NR_perf_event_open' was not declared in this scope
```
To build gatord for aarch64 edit `jni/Application.mk` and replace
`armeabi-v7a` with `arm64-v8a`. To build for ARM11 `jni/Application.mk`
and replace `armeabi-v7a` with `armeabi`.


## Running gator

### As a root user

- Copy gatord into the target's filesystem.
- Ensure gatord has execute permissions:
  `chmod +x gatord`
- The daemon must be run with root privileges:
  `sudo su`
  `gatord &`

This configuration requires Linux 3.4 or later with a correctly configured kernel.

### As a non-root user

- Copy gatord into the target's filesystem.
- Ensure gatord has execute permissions:
  `chmod +x gatord`
- Run the daemon:
  `./gatord &`

This configuration provides a reduced set of software only CPU counters such as
CPU utilization and process statistics, as well as Mali hardware counters on
supported Mali platforms.

## Perf PMU support

To check the perf PMUs support by your kernel, run
`ls /sys/bus/event_source/devices/`
If you see something like `ARMv7_Cortex_A##` this indicates A## support. If you
see `CCI_400` this indicates CCI-400 support. If you see `ccn`, it indicates
CCN support.

## CCN

CCN requires a perf driver to work. The necessary perf driver has been merged into
Linux 3.17 but can be backported to previous versions (see
https://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/diff/?id=a33b0daab73a0e08cc04459dd44b0121a8e8f81b
and later bugfixes)

## Compiling an application or shared library

Recommended compiler settings:
- `-g`: Debug information, such as line numbers, needed for best analysis results.
- `-fno-inline`: Speed improvement when processing the image files and most
  accurate analysis results.
- `-fno-omit-frame-pointer`: Arm EABI frame pointers allow recording of the
  call stack with each sample taken when in Arm state (i.e. not `-mthumb`).
- `-marm`: This option is required for ARMv7 and earlier if your compiler is
  configured with `--with-mode=thumb`, otherwise call stack unwinding will not
  work.

For Android ART, passing `--no-strip-symbols` to dex2oat will result in
function names but not line numbers to be included in the dex files. This can be
done by running `setprop dalvik.vm.dex2oat-flags --no-strip-symbols`
on the device and then regenerating the dex files.

## Polling /dev, /sys and /proc files

Gator supports reading arbitrary `/dev`, `/sys` and `/proc` files 10 times a
second. It will either interpret the file contents as a number or use a POSIX
extended regex to extract the number, see `events-Filesystem.xml` for
examples.

## Bugs

Kernels with `CONFIG_CPU_PM` enabled may produce invalid results on kernel
versions prior to 4.6. The problem manifests as counters not showing any data, large
spikes and non-sensible values for counters (e.g. Cycle Counter reading as *very*
high).
This issue stems from the fact that the kernel PMU driver does not save/restore
state when the CPU is powered down/up. This issue is fixed in 4.6 so to resolve the
issue either upgrade to a later kernel, or apply the fix to an older kernel.
The patch for 4.6 that resolves the issue is found here
https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=da4e4f18afe0f3729d68f3785c5802f786d36e34 -
this patch has been tested as applying cleanly to 4.4 kernel and it may be
possible to back port it to other versions as well.
Users of this patch may also need to apply
https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=cbcc72e037b8a3eb1fad3c1ae22021df21c97a51
as well.

There is a bug in some Linux kernels where an Oops may occur when a core is
offlined (user space gator only). The fix was merged into mainline in 3.14-rc5, see
http://git.kernel.org/tip/e3703f8cdfcf39c25c4338c3ad8e68891cca3731, and has been
backported to older kernels (3.4.83, 3.10.33, 3.12.14 and 3.13.6).

`CPU PMU: CPUx reading wrong counter -1` in dmesg. To work around,
update to the latest Linux kernel or use kernel space gator.

Scheduler switch resolutions are on exact millisecond boundaries.
To work around, update to the latest Linux kernel or use kernel space gator.

There is a bug in some Linux kernels where perf misidentifies the CPU type. To see
if you are affected by this, run` ls /sys/bus/event_source/devices/` and
verify the listed processor type matches what is expected. For example, an A9 should
show the following.
```
# ls /sys/bus/event_source/devices/
ARMv7_Cortex_A9  breakpoint  software  tracepoint
```
To work around the issue try upgrading to a later kernel.

On some versions of Android, annotations may not work unless SELinux is disabled by
running
`# setenforce 0`

Some targets do not correctly emit uevents when cores go on/offline. This will cause
CPU Activity with user space gator to be either 0% or 100% on a given core and the
Heat Map may show a large number of unresolved processes. To work around this issue,
use kernel space gator. To test for this run
`# ./gatord -d | grep uevent`
When cores go on/offline with user space gator something similar to the following
should be emitted
```
INFO: read(UEvent.cpp:61): uevent: offline@/devices/system/cpu/cpu1
INFO: read(UEvent.cpp:61): uevent: online@/devices/system/cpu/cpu1
```
The cores that are on/offline can be checked by running
`# cat /sys/devices/system/cpu/cpu*/online`
This issue affects a given target if the on/offline cores shown by the cat command
change but no cpu uevent is emitted.

On some older versions of Android, the following issue may occur when starting
gatord when using ndk-build
```
# ./gatord
[1] + Stopped (signal)        ./gatord
#
[1]   Segmentation fault      ./gatord
#
```
Starting with Android-L only position independent executables (pie) are supported,
but some older versions of Android do not support them. To avoid this issue, modify
Android.mk and remove the references to pie.

## Profiling the kernel (optional)

CONFIG_DEBUG_INFO must be enabled, see "Kernel configuration" section above.

Use vmlinux as the image for debug symbols in Streamline.

Drivers may be profiled using this method by statically linking the driver into the
kernel image or adding the driver as an image to Streamline.

[Contributing]: Contributing.md
[annotate/LICENSE]: annotate/LICENSE
[daemon/COPYING]: daemon/COPYING
[daemon/k]: daemon/k
[daemon/libsensors]: daemon/libsensors
[daemon/mxml]: daemon/mxml
[musl]: http://www.musl-libc.org/download.html
[notify/COPYING]: notify/COPYING
[python/COPYING]: python/COPYING
