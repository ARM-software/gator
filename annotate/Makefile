CFLAGS += -fPIC

CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar
RANLIB = $(CROSS_COMPILE)ranlib
COMPILE.c = $(CC) $(CFLAGS) $(CPPFLAGS) -c

all: libstreamline_annotate.a libstreamline_annotate.so

.PHONY: clean

%.o: %.c
	$(COMPILE.c) -o $@ $<

streamline_annotate.o: streamline_annotate.c streamline_annotate.h

libstreamline_annotate.a: streamline_annotate.o
	rm -f $@
	$(AR) cru $@ $^
	$(RANLIB) $@

libstreamline_annotate.so: streamline_annotate.o
	$(CC) $^ -shared -o $@

clean:
	rm -f libstreamline_annotate.a libstreamline_annotate.so streamline_annotate.o
