# Copyright (C) 2016-2020 by Arm Limited. All rights reserved.

APP_PLATFORM := android-23
# Replace armeabi-v7a with arm64-v8a to build an arm64 gatord or with armeabi to build an ARM11 gatord
APP_ABI := arm64-v8a
# Require 4.8 or later (for c++11)
NDK_TOOLCHAIN_VERSION := clang
# Use c++ STL
APP_STL := c++_static
APP_CPPFLAGS += -std=c++11
