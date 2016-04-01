LOCAL_PATH := $(call my-dir)

define examples_build_executable
  include $(CLEAR_VARS)

  LOCAL_SRC_FILES := $1.c ../gator/annotate/streamline_annotate.c

  LOCAL_CFLAGS += -I../gator/annotate -O0 -g3 -pthread -fPIE
  LOCAL_CFLAGS += -Wall -Wextra -Wc++-compat -Wpointer-arith -Wmissing-prototypes -Wstrict-prototypes
  LOCAL_LDFLAGS += -fPIE -pie
  LOCAL_LDLIBS += -pthread

  LOCAL_C_INCLUDES := $(LOCAL_PATH)

  LOCAL_MODULE := $1
  LOCAL_MODULE_TAGS := optional

  include $(BUILD_EXECUTABLE)
endef

TARGETS = absolute text cam delta visual shared
$(foreach target, $(TARGETS), $(eval $(call examples_build_executable, $(target))))
