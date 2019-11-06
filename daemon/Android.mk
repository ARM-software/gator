# Copyright (C) 2016, 2017, 2018, 2019 by Arm Limited. All rights reserved.

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

# LOCAL_SRC_FILES requires paths to be relative to LOCAL_PATH :-(
GEN_DIR := generated-files/$(TARGET_ARCH_ABI)/
MD5_SRC := $(GEN_DIR)/SrcMd5.cpp
# Generate some files into the output directory
XML_H := $(shell $(MAKE) -C $(LOCAL_PATH) OBJ_DIR="$(GEN_DIR)" ndk-prerequisites)

# Set to 1 to enable compiling with LTO using *LDD* as the linker
USE_LTO := 0

include $(LOCAL_PATH)/Sources.mk
LOCAL_SRC_FILES := \
    $(GATORD_C_SRC_FILES) \
    $(GATORD_CXX_SRC_FILES) \
    $(MD5_SRC)

LOCAL_CFLAGS += -Wall -O3 -fno-exceptions -pthread -DETCDIR=\"/etc\" -Ilibsensors -fPIE -I$(LOCAL_PATH)/$(GEN_DIR) -fvisibility=hidden
LOCAL_CPPFLAGS += -fno-rtti -Wextra -Wpointer-arith -std=c++11 -static-libstdc++
LOCAL_LDFLAGS += -fPIE -pie

ifeq ($(USE_LTO),1)
LOCAL_CFLAGS += -flto -fuse-ld=lld -ffunction-sections -fdata-sections
LOCAL_LDFLAGS += -flto -fuse-ld=lld -Wl,--gc-sections
endif

LOCAL_C_INCLUDES := $(LOCAL_PATH)

LOCAL_MODULE := gatord
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
