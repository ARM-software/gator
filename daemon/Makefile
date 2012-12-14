#
# Makefile for ARM Streamline - Gator Daemon
#

# Uncomment and define CROSS_COMPILE if it is not already defined
# CROSS_COMPILE=/path/to/cross-compiler/arm-linux-gnueabihf-
# NOTE: This toolchain uses the hardfloat abi by default. For non-hardfloat
# targets it is necessary to add options
# '-marm -march=armv4t -mfloat-abi=soft'.

CPP = $(CROSS_COMPILE)g++
GCC = $(CROSS_COMPILE)gcc

# -g produces debugging information
# -O3 maximum optimization
# -O0 no optimization, used for debugging
# -Wall enables most warnings
# -Werror treats warnings as errors
# -std=c++0x is the planned new c++ standard
# -std=c++98 is the 1998 c++ standard
# -mthumb-interwork is required for interworking to ARM or Thumb stdlibc
CFLAGS = -O3 -Wall -mthumb-interwork -fno-exceptions
CXXFLAGS = -fno-rtti
ifeq ($(WERROR),1)
	CFLAGS += -Werror
endif
# -s strips the binary of debug info
LDFLAGS = -s
TARGET = gatord
C_SRC = $(wildcard mxml/*.c)
CPP_SRC = $(wildcard *.cpp)

all: $(TARGET)

events.xml: events_header.xml $(wildcard events-*.xml) events_footer.xml
	cat $^ > $@

StreamlineSetup.cpp: events_xml.h
ConfigurationXML.cpp: configuration_xml.h

%_xml.h: %.xml escape
	./escape $< > $@

%.o: %.c *.h
	$(GCC) -c $(CFLAGS) -o $@ $<

%.o: %.cpp *.h
	$(CPP) -c $(CFLAGS) $(CXXFLAGS) -o $@ $<

$(TARGET): $(CPP_SRC:%.cpp=%.o) $(C_SRC:%.c=%.o)
	$(CPP) $(LDFLAGS) -o $@ $^ -lc -lrt -lpthread
	rm -f events_xml.h configuration_xml.h

escape: escape.c
	gcc $^ -o $@

clean:
	rm -f *.o mxml/*.o $(TARGET) escape events.xml events_xml.h configuration_xml.h
