# Gator annotation tracepoints

The source code for `gator_annotate`.

## License

* `gator_annotate.h` and `gator_annotate.c` are provided under GPL-2.0-only.

## Contributing

Contributions are accepted under the same license as the associated subproject with developer sign-off as described in [Contributing].

## Purpose

This feature is to support kernel annotations in Streamline. Kernel annotations are provided by  kernel tracepoints, which you can define as `gator_bookmark`, `gator_text`, or `gator_counter`.

## Creating kernel tracepoints

Follow these steps to create and define different types of kernel tracepoints:

1. Define CREATE_TRACE_POINTS and include the header file `gator_annotate.h` as part of the kernel module.
   In these examples, `gator_annotate.h` is found in the folder `include`, so we must define `#define TRACE_INCLUDE PATH ../include`.
   Depending on the layout of your project, you may need to specify an alternate value, or leave TRACE_INCLUDE_PATH unset.
   In order to add tracepoints, please add the following in the c file:
       #define CREATE_TRACE_POINTS
       #include "gator_annotate.h"
   NOTE: make sure that CREATE_TRACE_POINTS is defined only in one file which includes the header in a given module.
         To use gator annotations in the same module but in a different file, include the header file `gator_annotate.h` only.
   We have included two examples, `example_standalone/gator_annotate_standalone` and `example_shared/gator_annotate_sample`,
   In `gator_annotate_standalone`, annotations are added from the same module.
   In `gator_annotate_sample`, annotations are added from a different module and `EXPORT_TRACEPOINT_SYMBOL_GPL` is used from 
   `module_shared_annotate/gator_annotate` which `gator_annotate_sample` depends on.

2. Call the following functions, depending on the tracepoint that you want to create:

   - To add a tracepoint that creates a bookmark in Streamline, call function `trace_gator_bookmark(int tid, int color, const char * label)`
      - `tid` thread id.
      - `color` color for the bookmark.
      - `label` name for the bookmark.

   - To add a tracepoint that creates text in Streamline, call function `trace_gator_text(int tid, int color, const char * channel, const char * label)`
      - `tid` thread id.
      - `color` color for the annotation.
      - `channel` channel id.
      - `label` name for the annotation.

   - To add a tracepoint that creates a counter in Streamline, call function `trace_gator_counter(int tid, const char * title, const char * name, const char * units, bool isdelta, unsigned long long value)`
      - `tid` thread id.
      - `title` title for the counter.
      - `name` name for the counter.
      - `units` units for the counter (for example "cycles").
      - `isdelta` true if it is a delta counter, false otherwise.
      - `value` value of the counter at the time of collecting this tracepoint.

Alternatively, you can call the following convenience macros instead of calling the tracepoint function shown in step 2:
   - GATOR_BOOKMARK(label) - creates a bookmark with the given label.
   - GATOR_BOOKMARK_COLOR(color, label) - same as GATOR_BOOKMARK but with a specific color.
   - GATOR_TEXT_START(tid, channel, label) - starts a text annotation using the label on the given tid and channel.
   - GATOR_TEXT_START_COLOR(tid, channel, label, color) - same as GATOR_TEXT_START but with a specific color.
   - GATOR_TEXT_STOP(tid, channel) - stops the most recent text annotation of the specified tid and channel by using the tracepoint function to send an empty text annotation (for example "trace_gator_text(tid, color, channel, "");").
   - GATOR_DELTA_COUNTER_VALUE(title, name, units, value) - sends a delta counter value
   - GATOR_ABSOLUTE_COUNTER_VALUE(title, name, units, value) - sends an absolute counter value
Refer to `include/gator_annotate.h` for more information.

## Building the example

1. example_standalone
   a) Use `Makefile` from the `events/` folder. This has a make target `install_standalone` which will install the module for you.
   b) Alternatively use the Makefile inside `events/example_standalone`.
2. example_shared
   a) Use `Makefile` from the `events/` folder. This has a make target `install_shared` which will install the module for you.
   b) Alternatively use the Makefile inside `events/example_shared`.
Compile and install the kernel module, then reboot.

Optionally you can use `sudo insmod <modulename>.ko`.
1. example_standalone
   Build module using `make all`.
   `sudo insmod gator_annotate_standalone.ko`
2. example_shared
   Build module using `make all` from `example_shared` folder.
   `sudo insmod ../module_shared_annotate/gator_annotate.ko;insmod gator_annotate_sample.ko;` from `example_shared` folder
   and for removing module
   `sudo rmmod gator_annotate_sample;rmmod gator_annotate;`

Please refer to the Streamline user guide for more details.

[Contributing]: ../../daemon/Contributing.md
