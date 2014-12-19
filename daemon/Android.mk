LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

XML_H := $(shell cd $(LOCAL_PATH) && make events_xml.h defaults_xml.h)

LOCAL_CFLAGS += -Wall -O3 -mthumb-interwork -fno-exceptions -DETCDIR=\"/etc\" -Ilibsensors

LOCAL_SRC_FILES := \
	Buffer.cpp \
	CapturedXML.cpp \
	Child.cpp \
	ConfigurationXML.cpp \
	Driver.cpp \
	DriverSource.cpp \
	DynBuf.cpp \
	EventsXML.cpp \
	ExternalSource.cpp \
	Fifo.cpp \
	Hwmon.cpp \
	KMod.cpp \
	LocalCapture.cpp \
	Logging.cpp \
	main.cpp \
	Monitor.cpp \
	OlySocket.cpp \
	OlyUtility.cpp \
	PerfBuffer.cpp \
	PerfDriver.cpp \
	PerfGroup.cpp \
	PerfSource.cpp \
	Proc.cpp \
	Sender.cpp \
	SessionData.cpp \
	SessionXML.cpp \
	Source.cpp \
	StreamlineSetup.cpp \
	UEvent.cpp \
	UserSpaceSource.cpp \
	libsensors/access.c \
	libsensors/conf-lex.c \
	libsensors/conf-parse.c \
	libsensors/data.c \
	libsensors/error.c \
	libsensors/general.c \
	libsensors/init.c \
	libsensors/sysfs.c \
	mxml/mxml-attr.c \
	mxml/mxml-entity.c \
	mxml/mxml-file.c \
	mxml/mxml-get.c \
	mxml/mxml-index.c \
	mxml/mxml-node.c \
	mxml/mxml-private.c \
	mxml/mxml-search.c \
	mxml/mxml-set.c \
	mxml/mxml-string.c

LOCAL_C_INCLUDES := $(LOCAL_PATH) 

LOCAL_MODULE := gatord
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
