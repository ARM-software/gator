# Defines for Mali-Midgard/Bifrost driver
EXTRA_CFLAGS += -DMALI_USE_UMP=1 \
                -DMALI_LICENSE_IS_GPL=1 \
                -DMALI_BASE_TRACK_MEMLEAK=0 \
                -DMALI_DEBUG=0 \
                -DMALI_ERROR_INJECT_ON=0 \
                -DMALI_CUSTOMER_RELEASE=1 \
                -DMALI_UNIT_TEST=0 \
                -DMALI_BACKEND_KERNEL=1 \
                -DMALI_NO_MALI=0

DDK_DIR ?= .

GATOR_MALI_MIDGARD_PATH := $(shell echo $(CONFIG_GATOR_MALI_MIDGARD_PATH))

ifneq ($(wildcard $(DDK_DIR)/$(GATOR_MALI_MIDGARD_PATH)/mali_kbase_gator_api.h),)
  # r5p0/Fluorine - ...
  EXTRA_CFLAGS += -DMALI_SIMPLE_API=1 \
                  -DMALI_DIR_MIDGARD=1 \
                  -I$(DDK_DIR)/$(GATOR_MALI_MIDGARD_PATH) \

else
  ifneq ($(wildcard $(DDK_DIR)/$(GATOR_MALI_MIDGARD_PATH)/kbase),)
    # ? - r3p0
    KBASE_DIR = $(DDK_DIR)/$(GATOR_MALI_MIDGARD_PATH)/kbase
    OSK_DIR = $(DDK_DIR)/$(GATOR_MALI_MIDGARD_PATH)/kbase/osk
  else
    ifneq ($(wildcard $(DDK_DIR)/$(GATOR_MALI_MIDGARD_PATH)),)
      # r4p0/Europium - r4p1/Europium-Inc
      KBASE_DIR = $(DDK_DIR)/$(GATOR_MALI_MIDGARD_PATH)
      OSK_DIR = $(DDK_DIR)/$(GATOR_MALI_MIDGARD_PATH)/osk
      EXTRA_CFLAGS += -DMALI_DIR_MIDGARD=1
    endif
  endif

  UMP_DIR = $(DDK_DIR)/include/linux

  # Include directories in the DDK
  EXTRA_CFLAGS += -I$(KBASE_DIR)/ \
                  -I$(KBASE_DIR)/.. \
                  -I$(OSK_DIR)/.. \
                  -I$(UMP_DIR)/.. \
                  -I$(DDK_DIR)/include \
                  -I$(KBASE_DIR)/osk/src/linux/include \
                  -I$(KBASE_DIR)/platform_dummy \
                  -I$(KBASE_DIR)/src \
                  -Idrivers/staging/android \

endif
