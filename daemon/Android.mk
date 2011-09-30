LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_CFLAGS +=  -Wall -O3 -ftree-vectorize

LOCAL_SRC_FILES:= \
	CapturedXML.cpp \
	Child.cpp \
	Collector.cpp \
	ConfigurationXML.cpp \
	Fifo.cpp \
	HashMap.cpp \
	LocalCapture.cpp \
	Logging.cpp \
	main.cpp \
	OlySocket.cpp \
	OlyUtility.cpp \
	ReadSession.cpp \
	RequestXML.cpp \
	Sender.cpp \
	SessionData.cpp \
	StreamlineSetup.cpp \
	XMLOut.cpp \
	XMLReader.cpp 

LOCAL_C_INCLUDES := $(LOCAL_PATH) 

LOCAL_MODULE:= gatord

LOCAL_LDLIBS := -lz -llog

include $(BUILD_EXECUTABLE)
