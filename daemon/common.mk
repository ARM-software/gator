# -g produces debugging information
# -O3 maximum optimization
# -O0 no optimization, used for debugging
# -Wall enables most warnings
# -Werror treats warnings as errors
# -std=c++0x is the planned new c++ standard
# -std=c++98 is the 1998 c++ standard
CPPFLAGS += -O3 -Wall -fno-exceptions -pthread -MD -DETCDIR=\"/etc\" -Ilibsensors -I.
CXXFLAGS += -std=c++11 -static-libstdc++ -fno-rtti -Wextra -Wshadow -Wpointer-arith -Wundef # -Weffc++ -Wmissing-declarations
ifeq ($(WERROR),1)
	CPPFLAGS += -Werror
endif

ifeq ($(shell expr `$(CXX) -dumpversion | cut -f1 -d.` \>= 5),1)
	CXXFLAGS += -fno-sized-deallocation
endif

# -s strips the binary of debug info
LDFLAGS += -s
LDLIBS += -lrt -lm -pthread
TARGET = gatord
ESCAPE_EXE = escape/escape
C_SRC = $(wildcard mxml/*.c) $(wildcard libsensors/*.c)
CXX_SRC = $(wildcard *.cpp lib/*.cpp linux/*.cpp linux/*/*.cpp mali_userspace/*.cpp non_root/*.cpp)

ifeq ($(V),1)
	Q =
	ECHO_HOSTCC =
	ECHO_GEN =
	ECHO_CC =
	ECHO_CXX =
	ECHO_CCLD =
else
	Q = @
	ECHO_HOSTCC = @echo "  HOSTCC " $@
	ECHO_GEN = @echo "  GEN    " $@
	ECHO_CC = @echo "  CC     " $@
	ECHO_CXX = @echo "  CXX    " $@
	ECHO_CCLD = @echo "  CCLD   " $@
endif

all: $(TARGET)

events.xml: events_header.xml $(wildcard events-*.xml) events_footer.xml
	$(ECHO_GEN)
	$(Q)cat $^ > $@

include $(wildcard *.d lib/*.d linux/*.d linux/*/*.d mali_userspace/*.d non_root/*.d)
include $(wildcard mxml/*.d)
include $(wildcard libsensors/*.d)

EventsXML.cpp: events_xml.h
ConfigurationXML.cpp: defaults_xml.h
PmuXML.cpp: pmus_xml.h

# Don't regenerate conf-lex.c or conf-parse.c
libsensors/conf-lex.c: ;
libsensors/conf-parse.c: ;

%_xml.h: %.xml $(ESCAPE_EXE)
	$(ECHO_GEN)
	$(Q)$(ESCAPE_EXE) "$(basename $<)_xml" $< $@

%.o: %.c
	$(ECHO_CC)
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

%.o: %.cpp
	$(ECHO_CXX)
	$(Q)$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

SrcMd5.cpp: $(filter-out SrcMd5.cpp, $(wildcard *.cpp lib/*.cpp linux/*.cpp linux/*/*.cpp mali_userspace/*.cpp non_root/*.cpp)) $(wildcard *.h lib/*.h linux/*.h linux/*/*.h mali_userspace/*.h non_root/*.h mxml/*.c mxml/*.h libsensors/*.c libsensors/*.h)
	$(ECHO_GEN)
	$(Q)echo 'extern const char *const gSrcMd5 = "'`ls $^ | grep -Ev '^(.*_xml\.h|$@)$$' | LC_ALL=C sort | xargs cat | md5sum | cut -b 1-32`'";' > $@

$(TARGET): $(CXX_SRC:%.cpp=%.o) $(C_SRC:%.c=%.o) SrcMd5.o
	$(ECHO_CCLD)
	$(Q)$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

# Intentionally ignore CC as a native binary is required
$(ESCAPE_EXE): escape/escape.c
	$(ECHO_HOSTCC)
	$(Q)gcc $^ -o $@

clean:
	rm -f *.d *.o lib/*.d linux/*.d linux/*/*.d lib/*.o linux/*.o linux/*/*.o mali_userspace/*.d mali_userspace/*.o non_root/*.d non_root/*.o mxml/*.d mxml/*.o libsensors/*.d libsensors/*.o $(TARGET) $(ESCAPE_EXE) events.xml *_xml.h SrcMd5.cpp
