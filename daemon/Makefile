#
# Makefile for ARM Streamline - Gator Daemon
#

# Uncomment and define CROSS_COMPILE if it is not already defined
# CROSS_COMPILE=/path/to/cross-compiler/arm-none-linux-gnueabi-
ARCH=arm

CPP=$(CROSS_COMPILE)g++
GCC=$(CROSS_COMPILE)gcc

# -g produces debugging information
# -O3 maximum optimization
# -O0 no optimization, used for debugging
# -Wall enables most warnings
# -Werror treats warnings as errors
# -std=c++0x is the planned new c++ standard
# -std=c++98 is the 1998 c++ standard
# -mthumb-interwork is required for interworking to ARM or Thumb stdlibc
CFLAGS=-O3 -Wall -Werror -Wno-error=sequence-point -mthumb-interwork
# -s strips the binary of debug info
LDFLAGS=-s
TARGET=gatord
C_SRC = $(wildcard mxml/*.c)
CPP_SRC  = $(wildcard *.cpp)
TGT_OBJS = $(CPP_SRC:%.cpp=%.o) \
           $(C_SRC:%.c=%.o)

all: $(TARGET)

%.o: %.c *.h
	$(GCC) -c $(CFLAGS) -o $@ $<

%.o: %.cpp *.h
	$(CPP) -c $(CFLAGS) -o $@ $<

$(TARGET): convert $(TGT_OBJS)
	$(CPP) $(LDFLAGS) -o $@ $(TGT_OBJS) -lc -lrt -lpthread
	rm events_xml.h configuration_xml.h

convert:
	cat events_header.xml events-*\.xml events_footer.xml > events.xml
	xxd -i events.xml > events_xml.h
	xxd -i configuration.xml > configuration_xml.h

clean:
	rm -f *.o mxml/*.o $(TARGET) events.xml events_xml.h configuration_xml.h
