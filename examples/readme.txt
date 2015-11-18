*** Example Programs Extending Gator ***

text.c:     output bookmarks along with string annotations that make use of groups and channels.
visual.c:   output the ARM logo as a visual annotation.
delta.c:    generate a custom delta counter chart by emitting annotations.
absolute.c: generate a custom absolute counter chart by emitting annotations.
cam.c:      generate a custom activity map (CAM) view by emitting CAM messages.

*** Building the examples ***

The examples in this folder can be built natively on an ARM target by
running
	make

They can also be cross compiled by running
	make CROSS_COMPILE=<...>

*** Other Ways of Extending Gator ***

- Regularly read the contents of a file and plot it in Streamline, see
  streamline/gator/daemon/events-Filesystem.xml at for an example.

- Regularly read the contents of the ftrace buffer and plot it in
  Streamline, see streamline/gator/daemon/events-ftrace.xml for an
  example.

- Generate a custom counter from the driver via gator.ko, see
  streamline/gator/driver/gator_events_mmapped.c for an example.
