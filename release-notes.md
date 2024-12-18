# Gator 9.3.1

Detect CPU revision and correctly select metrics based on that (for Neoverse-N2 in particular)

# Gator 9.3.0

Adds support for SPE 1.2, including 'Not taken' event filter and the ability to invert the event filter

Adds support for AE variants of A520, A720, V3, R82

Adds new virtual counter for ATrace events

Fixes SPE data collection for devices with multiple SPE clusters

# Gator 9.2.2

Adds support for Kirin 980

# Gator 9.2.1

Updated metrics/SPE support for recently released CPUs

Fix crash on CPUs with high core count.

# Gator 9.2.0

Add support for Cortex-A725, Cortex-X925

Add support for Immortalis G925, Mali G725, G625 GPUs

Add Neoverse N3, V3 support

Add support for metrics

# Gator 9.1

Support for CLOCK_MONOTONIC in the gator protocol for jitdump support in Streamline.

# Gator 8.9

Adds the ability to pass command-line arguments to Android activity manager when running an Android package.

The default for exclude_kernel is now yes for application profiling. To restore the previous behaviour, pass `--exclude_kernel yes` command line argument, or select the correct option in the Streamline capture configuration dialog.

# Gator 8.8

Adds support for capturing data from the [Arm NN](https://developer.arm.com/Tools%20and%20Software/ArmNN) library on Android devices.

# Gator 8.7

Add the ability to include gator log messages in a capture to help with analysing support cases.

Per-core/-cache Mali counters are now summed in all cases, rather than some summed and others averaged, to align with [HWCPipe](https://github.com/ARM-software/HWCPipe). The Streamline templates have been updated so that derived metrics show sum or average as is appropriate to the metric, rather than on a per-HW-block basis.

Support for Cortex A520, A720 and X4 CPUs.

# Gator 8.6

Support for Immortalis-G720, Mali-G720 and Mali-G620 GPUs.

# Gator 8.5

Support for collecting Immortalis/Mali GPU timeline events from Perfetto on Linux.
Various bug fixes

# Gator 8.4

Updated Mali events files for recent GPUs and various minor fixes

# Gator 8.3

Adds support for Neoverse V2, Mali-G615 and Immortalis-G715 GPUs, Corelink MMU-700 (and improved support for MMU-600).

# Gator 8.2

This release adds support for Mali GPU Timeline visualization, using GPU Renderstages data from Perfetto.

# Gator 8.1

This release completes the preparatory work needed to support accessing data from Perfetto (traced) on the target.

# Gator 8.0

This release introduces support for Android Thermal State polling and associated visualization in Streamline. As a side affect, separate Android and Linux binaries are shipped with Streamline.
This release also sees continuing refactoring of gatord sources as part of on-going work to access to a wider set of data on Android.

# Gator 7.9

This release introduces a major change to the build system and minimum target C++ version
from C++11 to C++17.

