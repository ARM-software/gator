#
# Makefile for Arm Streamline - Gator Daemon
OBJ_DIR     := ${CURDIR}/

# Uncomment and define CROSS_COMPILE if it is not already defined
# CROSS_COMPILE=/path/to/cross-compiler/aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++

# -g produces debugging information
# -O3 maximum optimization
# -O0 no optimization, used for debugging
# -Wall enables most warnings
# -Werror treats warnings as errors
# -std=c++0x is the planned new c++ standard
# -std=c++98 is the 1998 c++ standard
CPPFLAGS += -O3 -Wall -fno-exceptions -pthread -MD -DETCDIR=\"/etc\" -Ilibsensors -I. -I$(OBJ_DIR)
CXXFLAGS += -std=c++11 -static-libstdc++ -fno-rtti -Wextra -Wno-shadow -Wpointer-arith -Wundef # -Weffc++ -Wmissing-declarations
ifeq ($(WERROR),1)
	CPPFLAGS += -Werror
endif

ifeq ($(shell expr `$(CXX) -dumpversion | cut -f1 -d.` \>= 5),1)
	CXXFLAGS += -fno-sized-deallocation
endif

# -s strips the binary of debug info
LDFLAGS     += -s
LDLIBS      += -lrt -lm -pthread
TARGET      := $(OBJ_DIR)gatord
ESCAPE_EXE  := $(OBJ_DIR)escape/escape
C_SRC       := $(wildcard mxml/*.c) $(wildcard libsensors/*.c)
CXX_SRC     := $(wildcard *.cpp lib/*.cpp linux/*.cpp linux/*/*.cpp mali_userspace/*.cpp non_root/*.cpp)
D_FILES     := $(C_SRC:%.c=$(OBJ_DIR)%.d) $(CXX_SRC:%.cpp=$(OBJ_DIR)%.d)
$(shell mkdir -p $(dir $(D_FILES)))

ifeq ($(V),1)
	Q =
	ECHO_HOSTCC =
	ECHO_GEN =
	ECHO_CC =
	ECHO_CXX =
	ECHO_CCLD =
	ECHO_CLEAN =
else
	Q = @
	ECHO_HOSTCC = @echo "  HOSTCC " $@
	ECHO_GEN = @echo "  GEN    " $@
	ECHO_CC = @echo "  CC     " $@
	ECHO_CXX = @echo "  CXX    " $@
	ECHO_CCLD = @echo "  CCLD   " $@
	ECHO_CLEAN = @echo "  CLEAN  "
endif

all: $(TARGET)

ndk-prerequisites: $(OBJ_DIR)events_xml.h $(OBJ_DIR)defaults_xml.h $(OBJ_DIR)pmus_xml.h $(OBJ_DIR)SrcMd5.cpp

clean:
	$(ECHO_CLEAN)
	$(Q)rm -f $(OBJ_DIR)*.d \
          $(OBJ_DIR)*.o \
          $(OBJ_DIR)lib/*.d \
          $(OBJ_DIR)linux/*.d \
          $(OBJ_DIR)linux/*/*.d \
          $(OBJ_DIR)lib/*.o \
          $(OBJ_DIR)linux/*.o \
          $(OBJ_DIR)linux/*/*.o \
          $(OBJ_DIR)mali_userspace/*.d \
          $(OBJ_DIR)mali_userspace/*.o \
          $(OBJ_DIR)non_root/*.d \
          $(OBJ_DIR)non_root/*.o \
          $(OBJ_DIR)mxml/*.d \
          $(OBJ_DIR)mxml/*.o \
          $(OBJ_DIR)libsensors/*.d \
          $(OBJ_DIR)libsensors/*.o \
          $(OBJ_DIR)events.xml \
          $(OBJ_DIR)*_xml.h \
          $(OBJ_DIR)SrcMd5.cpp \
          $(OBJ_DIR)SrcMd5.md5 \
          $(TARGET) \
          $(ESCAPE_EXE)

# Don't regenerate conf-lex.c or conf-parse.c
libsensors/conf-lex.c: ;
libsensors/conf-parse.c: ;

$(OBJ_DIR)events.xml: events_header.xml $(wildcard events-*.xml) events_footer.xml
	$(ECHO_GEN)
	$(Q)cat $^ > $@

$(OBJ_DIR)%_xml.h: %.xml $(ESCAPE_EXE)
	$(ECHO_GEN)
	$(Q)$(ESCAPE_EXE) "$(notdir $(basename $<))_xml" $< $@

$(OBJ_DIR)%_xml.h: $(OBJ_DIR)%.xml $(ESCAPE_EXE)
	$(ECHO_GEN)
	$(Q)$(ESCAPE_EXE) "$(notdir $(basename $<))_xml" $< $@

$(OBJ_DIR)%.o: %.c
	$(ECHO_CC)
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(OBJ_DIR)%.o: %.cpp
	$(ECHO_CXX)
	$(Q)$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

$(OBJ_DIR)%.o: $(OBJ_DIR)%.cpp
	$(ECHO_CXX)
	$(Q)$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c -o $@ $<

EventsXML.cpp: $(OBJ_DIR)events_xml.h
ConfigurationXML.cpp: $(OBJ_DIR)defaults_xml.h
PmuXMLParser.cpp: $(OBJ_DIR)pmus_xml.h

rwildcard=$(foreach d,$(wildcard $1*),$(call rwildcard,$d/,$2) $(filter $(subst *,%,$2),$d))

$(OBJ_DIR)SrcMd5.md5: $(filter-out %SrcMd5.cpp %events.xml %_xml.h, $(sort $(call rwildcard, , *.h *.c *.cpp *.hpp *.xml)))
	$(ECHO_GEN)
	$(Q)$(eval $@_MD5_FILES=$(shell ls $^ | grep -Ev '^(.*_xml\.h|$@)$$' | LC_ALL=C sort))
	$(Q)$(eval $@_MD5_HASH=$(shell ls $^ | grep -Ev '^(.*_xml\.h|$@)$$' | LC_ALL=C sort | xargs cat | md5sum | cut -b 1-32))
ifeq ($(V),1)
	@echo "  LOG    MD5_FILES=" $($@_MD5_FILES)
	@echo "  LOG    MD5_HASH=" $($@_MD5_HASH)
endif
	$(Q)echo $($@_MD5_HASH) > $@

$(OBJ_DIR)SrcMd5.cpp: $(OBJ_DIR)SrcMd5.md5
	$(Q)echo 'extern const char *const gSrcMd5 = "'`cat $<`'";' > $@

$(OBJ_DIR):
	$(Q)mkdir -p $@

$(OBJ_DIR)%/:
	$(Q)mkdir -p $@

include $(wildcard $(OBJ_DIR)*.d $(OBJ_DIR)lib/*.d $(OBJ_DIR)linux/*.d $(OBJ_DIR)linux/*/*.d $(OBJ_DIR)mali_userspace/*.d $(OBJ_DIR)non_root/*.d)
include $(wildcard $(OBJ_DIR)mxml/*.d)
include $(wildcard $(OBJ_DIR)libsensors/*.d)

$(TARGET): $(CXX_SRC:%.cpp=$(OBJ_DIR)%.o) $(C_SRC:%.c=$(OBJ_DIR)%.o) $(OBJ_DIR)SrcMd5.o
	$(ECHO_CCLD)
	$(Q)$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

# Intentionally ignore CC as a native binary is required
$(ESCAPE_EXE): escape/escape.c
	$(Q)mkdir -p $(OBJ_DIR)/escape/
	$(ECHO_HOSTCC)
	$(Q)gcc $^ -o $@
