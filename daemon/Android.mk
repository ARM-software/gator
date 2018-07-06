LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

XML_H := $(shell cd $(LOCAL_PATH) && make events_xml.h defaults_xml.h SrcMd5.cpp)

LOCAL_SRC_FILES := \
    AnnotateListener.cpp \
    AtraceDriver.cpp \
    Buffer.cpp \
    CapturedXML.cpp \
    CounterXML.cpp \
    CCNDriver.cpp \
    Child.cpp \
    Command.cpp \
    ConfigurationXML.cpp \
    DiskIODriver.cpp \
    DriverCounter.cpp \
    Driver.cpp \
    DriverSource.cpp \
    DynBuf.cpp \
    EventsXML.cpp \
    ExternalDriver.cpp \
    ExternalSource.cpp \
    Fifo.cpp \
    FSDriver.cpp \
    FtraceDriver.cpp \
    GatorCLIParser.cpp \
    HwmonDriver.cpp \
    KMod.cpp \
    lib/Assert.cpp \
    lib/FsEntry.cpp \
    lib/Popen.cpp \
    libsensors/access.c \
    libsensors/conf-lex.c \
    libsensors/conf-parse.c \
    libsensors/data.c \
    libsensors/error.c \
    libsensors/general.c \
    libsensors/init.c \
    libsensors/sysfs.c \
    lib/Thread.cpp \
    lib/TimestampSource.cpp \
    linux/perf/PerfBuffer.cpp \
    linux/perf/PerfDriver.cpp \
    linux/perf/PerfEventGroup.cpp \
    linux/perf/PerfEventGroupIdentifier.cpp \
    linux/perf/PerfGroups.cpp \
    linux/perf/PerfSource.cpp \
    linux/proc/ProcessPollerBase.cpp \
    linux/proc/ProcLoadAvgFileRecord.cpp \
    linux/proc/ProcPidStatFileRecord.cpp \
    linux/proc/ProcPidStatmFileRecord.cpp \
    linux/proc/ProcStatFileRecord.cpp \
    LocalCapture.cpp \
    Logging.cpp \
    main.cpp \
    mali_userspace/MaliDevice.cpp \
    mali_userspace/MaliHwCntrDriver.cpp \
    mali_userspace/MaliHwCntrReader.cpp \
    mali_userspace/MaliHwCntrSource.cpp \
    mali_userspace/MaliInstanceLocator.cpp \
    MaliVideoDriver.cpp \
    MemInfoDriver.cpp \
    MidgardDriver.cpp \
    Monitor.cpp \
    mxml/mxml-attr.c \
    mxml/mxml-entity.c \
    mxml/mxml-file.c \
    mxml/mxml-get.c \
    mxml/mxml-index.c \
    mxml/mxml-node.c \
    mxml/mxml-private.c \
    mxml/mxml-search.c \
    mxml/mxml-set.c \
    mxml/mxml-string.c \
    NetDriver.cpp \
    non_root/GlobalPoller.cpp \
    non_root/GlobalStateChangeHandler.cpp \
    non_root/GlobalStatsTracker.cpp \
    non_root/MixedFrameBuffer.cpp \
    non_root/NonRootDriver.cpp \
    non_root/NonRootSource.cpp \
    non_root/PerCoreMixedFrameBuffer.cpp \
    non_root/ProcessPoller.cpp \
    non_root/ProcessStateChangeHandler.cpp \
    non_root/ProcessStateTracker.cpp \
    non_root/ProcessStatsTracker.cpp \
    OlySocket.cpp \
    OlyUtility.cpp \
    PmuXML.cpp \
    PolledDriver.cpp \
    PrimarySourceProvider.cpp \
    Proc.cpp \
    Sender.cpp \
    SessionData.cpp \
    SessionXML.cpp \
    SimpleDriver.cpp \
    Source.cpp \
    SrcMd5.cpp \
    StreamlineSetup.cpp \
    TtraceDriver.cpp \
    UEvent.cpp \
    UserSpaceSource.cpp \
    WaitForProcessPoller.cpp

LOCAL_CFLAGS += -Wall -O3 -fno-exceptions -pthread -DETCDIR=\"/etc\" -Ilibsensors -fPIE
LOCAL_CXXFLAGS += -fno-rtti -Wextra -Wpointer-arith -std=c++11 -static-libstdc++ -fno-rtti -Wextra -Wpointer-arith
LOCAL_LDFLAGS += -fPIE -pie

LOCAL_C_INCLUDES := $(LOCAL_PATH)

LOCAL_MODULE := gatord
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
