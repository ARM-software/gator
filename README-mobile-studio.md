# Gator daemon, driver and related tools

The source code for `gatord` and related tools.

## License

* `daemon` and `notify` are provided under GPL-2.0-only. See [daemon/COPYING], and [notify/COPYING] respectively.
* `annotate`, and `gator_me.py` are provided under the BSD-3-Clause license. See [annotate/LICENSE].

This project contains code from other projects listed below. The original license text is included in those source files.

* `libsensors` source code in [daemon/libsensors] licensed under LGPL-2.1-or-later
* `mxml` source code in [daemon/mxml] licensed under APACHE-2.0 WITH Mini-XML-exception
* `perf_event.h` from Linux userspace kernel headers in [daemon/k] licensed under GPL-2.0-only WITH Linux-syscall-note

## Contributing

Contributions are accepted under the same license as the associated subproject with developer sign-off as described in [Contributing].

## Purpose

Instructions on setting up Arm Streamline on the target.

A target agent (gator) is required to run on the Arm target in order for Arm Streamline to operate. Gator requires Linux 3.4 or later.

Please refer to the Streamline Userguide for more details.

[Contributing]: Contributing.md
[annotate/LICENSE]: annotate/LICENSE
[daemon/COPYING]: daemon/COPYING
[daemon/k]: daemon/k
[daemon/libsensors]: daemon/libsensors
[daemon/mxml]: daemon/mxml
[notify/COPYING]: notify/COPYING
