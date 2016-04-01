CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++

CFLAGS +=
CXXFLAGS +=
CPPFLAGS += -I../annotate -O0 -g3 -MD -pthread -pipe
CPPFLAGS += -Wall -Wextra -Wshadow -Wc++-compat -Wpointer-arith -Wundef -Wmissing-prototypes -Wstrict-prototypes
LDFLAGS += -pthread
LDLIBS += -lrt

OUTPUT_OPTION = -o $@
COMPILE.c = $(CC) $(CFLAGS) $(CPPFLAGS) -c
COMPILE.cpp = $(CXX) $(CXXFLAGS) $(CPPFLAGS) -c
LINK.o = $(CC) $(LDFLAGS)

TARGETS = absolute text cam delta visual shared

all: $(TARGETS)

include $(wildcard *.d)

%.o: %.c
	$(COMPILE.c) $(OUTPUT_OPTION) $<

%.o: %.cpp
	$(COMPILE.cpp) $(OUTPUT_OPTION) $<

streamline_annotate.o: ../annotate/streamline_annotate.c
	$(COMPILE.c) $(OUTPUT_OPTION) $<

absolute: absolute.o streamline_annotate.o
	$(LINK.o) $^ $(LDLIBS) $(OUTPUT_OPTION)

text: text.o streamline_annotate.o
	$(LINK.o) $^ $(LDLIBS) $(OUTPUT_OPTION)

cam: cam.o streamline_annotate.o
	$(LINK.o) $^ $(LDLIBS) $(OUTPUT_OPTION)

delta: delta.o streamline_annotate.o
	$(LINK.o) $^ $(LDLIBS) $(OUTPUT_OPTION)

visual: visual.o streamline_annotate.o
	$(LINK.o) $^ $(LDLIBS) $(OUTPUT_OPTION)

shared: shared.o streamline_annotate.o
	$(LINK.o) $^ $(LDLIBS) $(OUTPUT_OPTION)

clean:
	rm -f *~ *.d *.i *.s *.o $(TARGETS)
