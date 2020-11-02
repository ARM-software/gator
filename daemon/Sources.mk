# Copyright (C) 2016-2020 by Arm Limited. All rights reserved.

GATORD_C_SRC_FILES := \
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
    mxml/mxml-string.c \

GATORD_CXX_SRC_FILES := \
    AnnotateListener.cpp \
    AtraceDriver.cpp \
    Buffer.cpp \
    BufferUtils.cpp \
    CapturedXML.cpp \
    CCNDriver.cpp \
    Child.cpp \
    Command.cpp \
    ConfigurationXML.cpp \
    ConfigurationXMLParser.cpp \
    CounterXML.cpp \
    CpuUtils.cpp \
    CpuUtils_Topology.cpp \
    DiskIODriver.cpp \
    DriverCounter.cpp \
    Drivers.cpp \
    DynBuf.cpp \
    ExternalDriver.cpp \
    ExternalSource.cpp \
    Fifo.cpp \
    FSDriver.cpp \
    FtraceDriver.cpp \
    GatorCLIParser.cpp \
    HwmonDriver.cpp \
    BlockCounterFrameBuilder.cpp \
    BlockCounterMessageConsumer.cpp \
    LocalCapture.cpp \
    Logging.cpp \
    main.cpp \
    MemInfoDriver.cpp \
    MidgardDriver.cpp \
    Monitor.cpp \
    NetDriver.cpp \
    OlySocket.cpp \
    OlyUtility.cpp \
    pmus_xml.cpp \
    PolledDriver.cpp \
    PrimarySourceProvider.cpp \
    Proc.cpp \
    Sender.cpp \
    SessionData.cpp \
    SessionXML.cpp \
    SimpleDriver.cpp \
    StreamlineSetup.cpp \
    StreamlineSetupLoop.cpp \
    SummaryBuffer.cpp \
    Tracepoints.cpp \
    TtraceDriver.cpp \
    UEvent.cpp \
    UserSpaceSource.cpp \
    armnn/ArmNNDriver.cpp \
    armnn/ArmNNSource.cpp \
    armnn/CounterDirectoryDecoder.cpp \
    armnn/CounterDirectoryStateUtils.cpp \
    armnn/DecoderUtility.cpp \
    armnn/DriverSourceIpc.cpp \
    armnn/FrameBuilderFactory.cpp \
    armnn/GlobalState.cpp \
    armnn/PacketDecoder.cpp \
    armnn/PacketDecoderEncoderFactory.cpp \
    armnn/PacketEncoder.cpp \
    armnn/PacketUtility.cpp \
    armnn/SenderQueue.cpp \
    armnn/SenderThread.cpp \
    armnn/Session.cpp \
    armnn/SessionPacketSender.cpp \
    armnn/SessionStateTracker.cpp \
    armnn/SocketAcceptor.cpp \
    armnn/SocketIO.cpp \
    armnn/ThreadManagementServer.cpp \
    armnn/TimestampCorrector.cpp \
    lib/Assert.cpp \
    lib/File.cpp \
    lib/FileDescriptor.cpp \
    lib/FsEntry.cpp \
    lib/Popen.cpp \
    lib/Syscall.cpp \
    lib/TimestampSource.cpp \
    lib/Utils.cpp \
    lib/WaitForProcessPoller.cpp \
    linux/CoreOnliner.cpp \
    linux/PerCoreIdentificationThread.cpp \
    linux/perf/PerfAttrsBuffer.cpp \
    linux/perf/PerfBuffer.cpp \
    linux/perf/PerfCpuOnlineMonitor.cpp \
    linux/perf/PerfDriverConfiguration.cpp \
    linux/perf/PerfDriver.cpp \
    linux/perf/PerfEventGroup.cpp \
    linux/perf/PerfEventGroupIdentifier.cpp \
    linux/perf/PerfGroups.cpp \
    linux/perf/PerfSource.cpp \
    linux/perf/PerfSyncThreadBuffer.cpp \
    linux/perf/PerfSyncThread.cpp \
    linux/proc/ProcessChildren.cpp \
    linux/proc/ProcessPollerBase.cpp \
    linux/proc/ProcLoadAvgFileRecord.cpp \
    linux/proc/ProcPidStatFileRecord.cpp \
    linux/proc/ProcPidStatmFileRecord.cpp \
    linux/proc/ProcStatFileRecord.cpp \
    linux/SysfsSummaryInformation.cpp \
    mali_userspace/MaliDeviceApi.cpp \
    mali_userspace/MaliDevice.cpp \
    mali_userspace/MaliHwCntrDriver.cpp \
    mali_userspace/MaliHwCntrReader.cpp \
    mali_userspace/MaliHwCntrSource.cpp \
    mali_userspace/MaliHwCntrTask.cpp \
    mali_userspace/MaliInstanceLocator.cpp \
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
    xml/EventsXML.cpp \
    xml/EventsXMLProcessor.cpp \
    xml/MxmlUtils.cpp \
    xml/PmuXML.cpp \
    xml/PmuXMLParser.cpp \
    xml/PmuXMLParser.h
