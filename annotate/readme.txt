*** Annotations
Annotations are a way for users to gain insight into their user space application via instrumenting source code.
To use these annotations, include streamline_annotate.h and streamline_annotate.c into your project, or include a libstreamline_annotate library built using the Makefile.
The different annotation types include:
    String: works in a similar way to printf but instead of console output, string messages populate the Log view and place framing overlays directly in the Timeline view
    Visual: adds an image to the Visual Annotation Chart and also populates the Log view
    Marker: places a bookmark into the Timeline view highlighting an interesting event in time
    Counter: dynamically creates a counter chart (numeric values plotted over time) to the Timeline view
    Custom Activity Map (CAM): allows the user to define and visualize a complicated dependency chain of jobs and events

*** Examples
* Simple examples for all the above annotation types are available in the streamline/examples folder.
* For more complicated examples, import the Linux application example projects into Arm DS IDE and refer to readme.html for more details.

*** Options
If your application needs to send annotations to gator running as a different
user (usually root to use --system-wide=yes), and your system forbids unix
domain socket connections between different users (should be the case in
Android 11+), then you can use a TCP connection instead. Set -DTCP_ANNOTATIONS
when compiling your application to do this.
