LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

$(shell cd $(LOCAL_PATH);cat events_header.xml events-*\.xml events_footer.xml > events.xml;xxd -i events.xml > events_xml.h;xxd -i configuration.xml > configuration_xml.h)

LOCAL_CFLAGS +=  -Wall -O3 -ftree-vectorize

LOCAL_SRC_FILES:= \
	CapturedXML.cpp \
	Child.cpp \
	Collector.cpp \
	ConfigurationXML.cpp \
	Fifo.cpp \
	LocalCapture.cpp \
	Logging.cpp \
	main.cpp \
	OlySocket.cpp \
	OlyUtility.cpp \
	Sender.cpp \
	SessionData.cpp \
	SessionXML.cpp \
	StreamlineSetup.cpp \
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

LOCAL_MODULE:= gatord
LOCAL_MODULE_TAGS:= optional

LOCAL_LDLIBS := -lz -llog

include $(BUILD_EXECUTABLE)
