# Copyright (C) 2010-2024 by Arm Limited. All rights reserved.
CMAKE_MINIMUM_REQUIRED(VERSION 3.16 FATAL_ERROR)

OPTION(ENABLE_VCPKG "Pull in dependencies using vcpkg" ON)

IF(ENABLE_VCPKG)
    SET(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/triplets" CACHE STRING "")
    SET(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
ENDIF()

PROJECT(gatord C CXX)

SET(CMAKE_CXX_STANDARD 17)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)

# ###
# Generate compile_commands.json for clang-tidy et al
# ###
SET(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Configuration options
OPTION(GATORD_WERROR "Build with -Werror set" ON)
OPTION(CONFIG_USE_PERFETTO "Build gator with Perfetto support (Mali Timeline support)." ON)
OPTION(CLANG_TIDY_FIX "Enable --fix with clang-tidy" OFF)
OPTION(CONFIG_PREFER_SYSTEM_WIDE_MODE "Enable system-wide capture by default" ON)
OPTION(CONFIG_ASSUME_PERF_HIGH_PARANOIA "Assume perf_event_paranoid is 2 if it cannot be read" ON)

# Include the target detection code
INCLUDE(${CMAKE_CURRENT_SOURCE_DIR}/cmake/build-target.cmake)

# Include the source MD5 macro
INCLUDE(${CMAKE_CURRENT_SOURCE_DIR}/cmake/create-src-md5.cmake)

# Include the strip/split-debug rules
INCLUDE(${CMAKE_CURRENT_SOURCE_DIR}/cmake/split-strip.cmake)

# Include the escaper
INCLUDE(${CMAKE_CURRENT_SOURCE_DIR}/cmake/escape.cmake)

# Include hwcpipe2
SET(HWCPIPE_ENABLE_TESTS OFF CACHE BOOL "")
SET(HWCPIPE_ENABLE_EXCEPTIONS ON CACHE BOOL "")
SET(HWCPIPE_ENABLE_SYMBOLS_VISIBILITY OFF CACHE BOOL "")
SET(HWCPIPE_ENABLE_RTTI OFF CACHE BOOL "")
SET(HWCPIPE_WERROR OFF CACHE BOOL "")
SET(HWCPIPE_WALL OFF CACHE BOOL "")
SET(HWCPIPE_ENABLE_NEURAL_ENGINE OFF CACHE BOOL "")
SET(HWCPIPE_ENABLE_END_TO_END_TESTS OFF CACHE BOOL "")
ADD_SUBDIRECTORY(hwcpipe2)

# Configure target flags
IF(("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU") AND(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "7.4.0"))
    SET(GATORD_C_CXX_FLAGS "-fvisibility=default")
ELSE()
    SET(GATORD_C_CXX_FLAGS "-fvisibility=hidden")
ENDIF()

IF(GATORD_WERROR)
    SET(GATORD_C_CXX_FLAGS "${GATORD_C_CXX_FLAGS} -Werror")
ENDIF()

IF(GATORD_BUILD_STATIC)
    SET(GATORD_C_CXX_FLAGS "${GATORD_C_CXX_FLAGS} -static")
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
ENDIF()

IF(ANDROID)
    SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
ENDIF()

IF(NOT DEFINED GATORD_BUILD_ID)
    SET(GATORD_BUILD_ID "oss")
ENDIF()

MESSAGE(STATUS "GATORD_BUILD_ID = ${GATORD_BUILD_ID}")

SET(GATORD_C_CXX_FLAGS "${GATORD_C_CXX_FLAGS}")

IF(NOT CONFIG_PREFER_SYSTEM_WIDE_MODE)
    SET(GATORD_C_CXX_FLAGS "${GATORD_C_CXX_FLAGS} -DCONFIG_PREFER_SYSTEM_WIDE_MODE=0")
ENDIF()

IF(NOT CONFIG_ASSUME_PERF_HIGH_PARANOIA)
    SET(GATORD_C_CXX_FLAGS "${GATORD_C_CXX_FLAGS} -DCONFIG_ASSUME_PERF_HIGH_PARANOIA=0")
ENDIF()

INCLUDE(${CMAKE_CURRENT_SOURCE_DIR}/cmake/compiler-flags.cmake)

ADD_SUBDIRECTORY(${CMAKE_CURRENT_SOURCE_DIR}/ipc/proto
                 ${CMAKE_CURRENT_BINARY_DIR}/ipc/proto/generated)

# Include the clang tools (after HWCPIPE and PROTO, so as not to lint that subproject when ENABLE_CLANG_TIDY_DURING_BUILD is ON)
SET(CLANG_TIDY_YAML "${CMAKE_SOURCE_DIR}/../.clang-tidy")
INCLUDE(${CMAKE_CURRENT_SOURCE_DIR}/cmake/clang-tools.cmake)

# Find the external dependencies
IF(ENABLE_VCPKG)
    FIND_PACKAGE(mxml CONFIG REQUIRED)
    SET(MXML_TARGET mxml)
ELSE()
    INCLUDE(FindPkgConfig)
    pkg_search_module(PKG_MXML REQUIRED IMPORTED_TARGET "mxml>=3")
    SET(MXML_TARGET PkgConfig::PKG_MXML)
    INCLUDE_DIRECTORIES(SYSTEM ${PKG_MXML_INCLUDE_DIRS} ${PKG_MXML_INCLUDEDIR})
ENDIF()

FIND_PACKAGE(Threads REQUIRED)
SET(Boost_USE_MULTITHREADED ON)
FIND_PACKAGE(Boost 1.78 REQUIRED COMPONENTS
    filesystem
    regex
)
ADD_DEFINITIONS(-DBOOST_FILESYSTEM_NO_DEPRECATED)

ADD_DEFINITIONS(-DETCDIR="/etc")
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
INCLUDE_DIRECTORIES(SYSTEM
    ${CMAKE_CURRENT_SOURCE_DIR}/extheaders
    ${CMAKE_CURRENT_SOURCE_DIR}/libsensors)

#Get copyright year from git
find_package(Git REQUIRED)
execute_process(COMMAND ${GIT_EXECUTABLE} log -1 --format=%cd --date=format:%Y
                OUTPUT_VARIABLE COPYRIGHT_YEAR
                OUTPUT_STRIP_TRAILING_WHITESPACE)

# This file is generated and contains a hash of the source files
SET(GENERATED_MD5_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/SrcMd5.cpp)
SET(GENERATED_MD5_FILE ${CMAKE_CURRENT_BINARY_DIR}/SrcMd5.md5)

# The source files to build
SET(GATORD_3RD_PARTY_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/extheaders/k/perf_event.h
    ${CMAKE_CURRENT_SOURCE_DIR}/libsensors/access.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libsensors/conf-lex.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libsensors/conf-parse.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libsensors/data.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libsensors/error.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libsensors/general.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libsensors/init.c
    ${CMAKE_CURRENT_SOURCE_DIR}/libsensors/sysfs.c)

SET_SOURCE_FILES_PROPERTIES(${GATORD_3RD_PARTY_SOURCES}
    COMPILE_FLAGS -w)

SET(GATORD_SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/AnnotateListener.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AnnotateListener.h
    ${CMAKE_CURRENT_SOURCE_DIR}/AtraceDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AtraceDriver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/BlockCounterFrameBuilder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BlockCounterFrameBuilder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/BlockCounterMessageConsumer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BlockCounterMessageConsumer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Buffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Buffer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/BufferUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BufferUtils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/CapturedSpe.h
    ${CMAKE_CURRENT_SOURCE_DIR}/CapturedXML.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CapturedXML.h
    ${CMAKE_CURRENT_SOURCE_DIR}/CCNDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CCNDriver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Child.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Child.h
    ${CMAKE_CURRENT_SOURCE_DIR}/CommitTimeChecker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Config.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Configuration.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ConfigurationXML.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ConfigurationXML.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ConfigurationXMLParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ConfigurationXMLParser.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Constant.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Constant.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ConstantMode.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Counter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/CounterXML.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CounterXML.h
    ${CMAKE_CURRENT_SOURCE_DIR}/CpuUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CpuUtils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/CpuUtils_Topology.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CpuUtils_Topology.h
    ${CMAKE_CURRENT_SOURCE_DIR}/DiskIODriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DiskIODriver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/DriverCounter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DriverCounter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Driver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers.h
    ${CMAKE_CURRENT_SOURCE_DIR}/DynBuf.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/DynBuf.h
    ${CMAKE_CURRENT_SOURCE_DIR}/EventCode.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Events.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ExitStatus.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ExternalDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ExternalDriver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ExternalSource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ExternalSource.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Fifo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Fifo.h
    ${CMAKE_CURRENT_SOURCE_DIR}/FSDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FSDriver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/FtraceDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FtraceDriver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/GatorCLIFlags.h
    ${CMAKE_CURRENT_SOURCE_DIR}/GatorCLIParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GatorCLIParser.h
    ${CMAKE_CURRENT_SOURCE_DIR}/GatorException.h
    ${CMAKE_CURRENT_SOURCE_DIR}/GatorMain.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GatorMain.h
    ${CMAKE_CURRENT_SOURCE_DIR}/GetEventKey.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GetEventKey.h
    ${CMAKE_CURRENT_SOURCE_DIR}/HwmonDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/HwmonDriver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/IBlockCounterFrameBuilder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/IBlockCounterMessageConsumer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/IBufferControl.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ICpuInfo.h
    ${CMAKE_CURRENT_SOURCE_DIR}/IMonitor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/IRawFrameBuilder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ISender.h
    ${CMAKE_CURRENT_SOURCE_DIR}/LocalCapture.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/LocalCapture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Logging.h
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MemInfoDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MemInfoDriver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/MidgardDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidgardDriver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Monitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Monitor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/monotonic_pair.h
    ${CMAKE_CURRENT_SOURCE_DIR}/NetDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/NetDriver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/OlySocket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/OlySocket.h
    ${CMAKE_CURRENT_SOURCE_DIR}/OlyUtility.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/OlyUtility.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ParserResult.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ParserResult.h
    ${CMAKE_CURRENT_SOURCE_DIR}/pmus_xml.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PolledDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PolledDriver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/PrimarySourceProvider.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PrimarySourceProvider.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Proc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Proc.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Protocol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ProductVersion.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Sender.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Sender.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionData.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionData.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionXML.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SessionXML.h
    ${CMAKE_CURRENT_SOURCE_DIR}/SimpleDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SimpleDriver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/smmu_identifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/smmu_identifier.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/smmu_support.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/smmu_support.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Source.h
    ${CMAKE_CURRENT_SOURCE_DIR}/StreamlineSetup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StreamlineSetup.h
    ${CMAKE_CURRENT_SOURCE_DIR}/StreamlineSetupLoop.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/StreamlineSetupLoop.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Time.h
    ${CMAKE_CURRENT_SOURCE_DIR}/TtraceDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TtraceDriver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/UEvent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UEvent.h
    ${CMAKE_CURRENT_SOURCE_DIR}/UserSpaceSource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UserSpaceSource.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/agent_environment.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/agent_environment.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/agent_worker_base.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/agent_worker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/agent_workers_process.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/agent_workers_process_fwd.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/agent_workers_process_holder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/agent_workers_process_holder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/common/coalescing_cpu_monitor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/common/nl_cpu_monitor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/common/polling_cpu_monitor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/common/socket_listener.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/common/socket_reference.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/common/socket_worker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/ext_source/ext_source_agent.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/ext_source/ext_source_agent_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/ext_source/ext_source_agent_main.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/ext_source/ext_source_agent_worker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/ext_source/ipc_sink_wrapper.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/async_buffer_builder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/async_perf_ringbuffer_monitor.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/capture_configuration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/capture_configuration.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/cpufreq_counter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/cpufreq_counter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/cpu_info.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/events/event_binding_manager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/events/event_bindings.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/events/event_configuration.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/events/perf_activator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/events/perf_activator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/events/perf_event_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/events/perf_event_utils.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/events/perf_ringbuffer_mmap.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/events/types.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/perf_agent.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/perf_agent_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/perf_agent_main.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/perf_agent_worker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/perf_buffer_consumer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/perf_buffer_consumer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/perf_capture_cpu_monitor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/perf_capture.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/perf_capture_helper.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/perf_driver_summary.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/perf_driver_summary.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/perf_frame_packer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/perf_frame_packer.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/record_types.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/source_adapter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/source_adapter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perf/sync_generator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perfetto/perfetto_driver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/perfetto/perfetto_driver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/spawn_agent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/agents/spawn_agent.h
    ${CMAKE_CURRENT_SOURCE_DIR}/android/AndroidActivityManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/android/AndroidActivityManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/android/AppGatorRunner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/android/AppGatorRunner.h
    ${CMAKE_CURRENT_SOURCE_DIR}/android/GatorAndroidSetupHandler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/android/GatorAndroidSetupHandler.h
    ${CMAKE_CURRENT_SOURCE_DIR}/android/IAppGatorRunner.h
    ${CMAKE_CURRENT_SOURCE_DIR}/android/PropertyUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/android/PropertyUtils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/android/Spawn.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/android/Spawn.h
    ${CMAKE_CURRENT_SOURCE_DIR}/android/ThermalDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/android/ThermalDriver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/android/Utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/android/Utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/apc/misc_apc_frame_ipc_sender.h
    ${CMAKE_CURRENT_SOURCE_DIR}/apc/perf_apc_frame_utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/apc/perf_counter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/apc/summary_apc_frame_utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/AcceptedSocketQueue.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/ArmNNDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/ArmNNDriver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/ArmNNSource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/ArmNNSource.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/ByteOrder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/CaptureMode.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/CounterDirectoryDecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/CounterDirectoryDecoder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/CounterDirectoryStateUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/CounterDirectoryStateUtils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/DecoderUtility.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/DecoderUtility.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/DriverSourceIpc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/DriverSourceIpc.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/DriverSourceWithAgent.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/DriverSourceWithAgent.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/FrameBuilderFactory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/FrameBuilderFactory.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/GlobalState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/GlobalState.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/IAcceptor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/ICaptureController.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/ICounterConsumer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/ICounterDirectoryConsumer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/IEncoder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/IFrameBuilderFactory.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/IGlobalState.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/IPacketConsumer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/IPacketDecoder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/IPeriodicCounterCaptureConsumer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/IPeriodicCounterSelectionConsumer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/IPerJobCounterCaptureConsumer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/IPerJobCounterSelectionConsumer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/ISender.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/ISession.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/ISocketIO.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/IAcceptingSocket.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/ISessionPacketSender.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/IStartStopHandler.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/PacketDecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/PacketDecoderEncoderFactory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/PacketDecoderEncoderFactory.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/PacketDecoder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/PacketEncoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/PacketEncoder.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/PacketUtility.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/PacketUtility.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/PacketUtilityModels.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/SenderQueue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/SenderQueue.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/SenderThread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/SenderThread.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/Session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/Session.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/SessionPacketSender.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/SessionPacketSender.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/SessionStateTracker.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/SessionStateTracker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/SocketAcceptor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/SocketIO.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/SocketIO.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/ThreadManagementServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/ThreadManagementServer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/TimestampCorrector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/armnn/TimestampCorrector.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/asio_traits.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/async_line_reader.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/async/completion_handler.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/async_initiate.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/continuation.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/continuation_of.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/continuation_traits.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/cont_if.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/cont_if_state.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/continuation_factory.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/do_if.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/do_if_state.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/initiation_chain.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/loop.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/loop_state.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/map_error.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/map_error_state.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/on_executor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/on_executor_state.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/polymorphic_state.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/predicate.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/predicate_state.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/start_state.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/state_chain.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/then.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/then_state.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/trace.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/unpack_tuple.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/unpack_tuple_state.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/unpack_variant.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/unpack_variant_state.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/detail/use_continuation_state.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/nop_receiver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/operations.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/stored_continuation.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/continuations/use_continuation.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/netlink/nl_protocol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/netlink/uevents.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/proc/async_exec.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/async/proc/async_exec.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/async/proc/async_process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/async/proc/async_process.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/async/proc/async_proc_poller.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/proc/async_read_proc_maps.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/proc/async_read_proc_sys_dependencies.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/proc/async_wait_for_process.h
    ${CMAKE_CURRENT_SOURCE_DIR}/async/proc/process_monitor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/async/proc/process_monitor.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/async/proc/process_state.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/async/proc/process_state_tracker.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/async/proc/wait.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/capture/CaptureProcess.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/capture/CaptureProcess.h
    ${CMAKE_CURRENT_SOURCE_DIR}/capture/Environment.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/capture/Environment.h
    ${CMAKE_CURRENT_SOURCE_DIR}/capture/internal/UdpListener.h
    ${CMAKE_CURRENT_SOURCE_DIR}/handleException.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ipc/async_streamline_sender.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ipc/codec.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ipc/message_key.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ipc/messages.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ipc/message_traits.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ipc/raw_ipc_channel_sink.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ipc/raw_ipc_channel_source.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Assert.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Assert.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/AutoClosingFd.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/EnumUtils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/error_code_or.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/exception.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Error.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Error.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/File.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/FileDescriptor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/FileDescriptor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/File.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/forked_process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/forked_process.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/forked_process_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/forked_process_utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Format.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/FsEntry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/FsEntry.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/FsUtils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/GenericTimer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/LineReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/LineReader.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Memory.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/perfetto_utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/PmuCommonEvents.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Popen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Popen.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Process.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Process.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Resource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Resource.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/SharedMemory.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/source_location.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Span.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/StaticVector.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/String.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Syscall.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Syscall.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Throw.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Time.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/TimestampSource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/TimestampSource.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Utils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/Waiter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/WaitForProcessPoller.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/WaitForProcessPoller.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/CoreOnliner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/CoreOnliner.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/PerCoreIdentificationThread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/PerCoreIdentificationThread.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/attr_to_key_mapping_tracker.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/IPerfAttrsConsumer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/IPerfGroups.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/PerfAttrsBuffer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/PerfAttrsBuffer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/PerfConfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/PerfDriverConfiguration.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/PerfDriverConfiguration.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/PerfDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/PerfDriverCreateSource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/PerfDriver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/PerfEventGroup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/PerfEventGroup.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/PerfEventGroupIdentifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/PerfEventGroupIdentifier.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/PerfGroups.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/PerfGroups.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/PerfSyncThread.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/PerfSyncThread.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/perf/PerfUtils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/proc/ProcessChildren.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/proc/ProcessChildren.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/proc/ProcessPollerBase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/proc/ProcessPollerBase.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/proc/ProcLoadAvgFileRecord.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/proc/ProcLoadAvgFileRecord.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/proc/ProcPidStatFileRecord.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/proc/ProcPidStatFileRecord.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/proc/ProcPidStatmFileRecord.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/proc/ProcPidStatmFileRecord.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/proc/ProcStatFileRecord.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/proc/ProcStatFileRecord.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/SysfsSummaryInformation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/SysfsSummaryInformation.h
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/Tracepoints.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/linux/Tracepoints.h
    ${CMAKE_CURRENT_SOURCE_DIR}/logging/agent_log.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logging/agent_log.h
    ${CMAKE_CURRENT_SOURCE_DIR}/logging/configuration.h
    ${CMAKE_CURRENT_SOURCE_DIR}/logging/file_log_sink.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logging/file_log_sink.h
    ${CMAKE_CURRENT_SOURCE_DIR}/logging/global_log.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logging/global_log.h
    ${CMAKE_CURRENT_SOURCE_DIR}/logging/logger_t.h
    ${CMAKE_CURRENT_SOURCE_DIR}/logging/logging.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logging/log_sink_t.h
    ${CMAKE_CURRENT_SOURCE_DIR}/logging/parameters.h
    ${CMAKE_CURRENT_SOURCE_DIR}/logging/std_log_sink.h
    ${CMAKE_CURRENT_SOURCE_DIR}/logging/suppliers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/logging/suppliers.h
    ${CMAKE_CURRENT_SOURCE_DIR}/metrics/definitions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/metrics/definitions.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/metrics/group_generator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/metrics/group_generator.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/metrics/metric_group_set.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/metrics/metric_group_set.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliDevice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliDevice.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliGPUClockPolledDriverCounter.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliGPUClockPolledDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliGPUClockPolledDriver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliHwCntrDriver.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliHwCntrDriver.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliHwCntr.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliHwCntrNamesGenerated.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliHwCntrSource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliHwCntrSource.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliHwCntrTask.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliHwCntrTask.h
    ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliInstanceLocator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mali_userspace/MaliInstanceLocator.h
    ${CMAKE_CURRENT_SOURCE_DIR}/xml/CurrentConfigXML.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/xml/CurrentConfigXML.h
    ${CMAKE_CURRENT_SOURCE_DIR}/xml/EventsXMLHelpers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/xml/EventsXMLHelpers.h
    ${CMAKE_CURRENT_SOURCE_DIR}/xml/EventsXML.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/xml/EventsXML.h
    ${CMAKE_CURRENT_SOURCE_DIR}/xml/EventsXMLProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/xml/EventsXMLProcessor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/xml/MxmlUtils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/xml/MxmlUtils.h
    ${CMAKE_CURRENT_SOURCE_DIR}/xml/PmuXML.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/xml/PmuXML.h
    ${CMAKE_CURRENT_SOURCE_DIR}/xml/PmuXMLParser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/xml/PmuXMLParser.h
)

IF(ANDROID)
    SET(GATORD_SRC_FILES ${GATORD_SRC_FILES}
        ${CMAKE_CURRENT_SOURCE_DIR}/agents/armnn/armnn_agent.h
        ${CMAKE_CURRENT_SOURCE_DIR}/agents/armnn/armnn_agent_main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/agents/armnn/armnn_agent_main.h
        ${CMAKE_CURRENT_SOURCE_DIR}/agents/armnn/armnn_agent_worker.h
    )
ENDIF()

IF(CONFIG_USE_PERFETTO)
    SET(GATORD_SRC_FILES ${GATORD_SRC_FILES}
        ${CMAKE_CURRENT_SOURCE_DIR}/agents/perfetto/perfetto_agent.h
        ${CMAKE_CURRENT_SOURCE_DIR}/agents/perfetto/perfetto_agent_main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/agents/perfetto/perfetto_agent_main.h
        ${CMAKE_CURRENT_SOURCE_DIR}/agents/perfetto/perfetto_sdk_helper.h
        ${CMAKE_CURRENT_SOURCE_DIR}/agents/perfetto/perfetto_sdk_helper.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/agents/perfetto/perfetto_agent_worker.h)

    ADD_LIBRARY(perfetto_generated STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../ext/perfetto/sdk/perfetto.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/../ext/perfetto/sdk/perfetto.h
    )
    TARGET_INCLUDE_DIRECTORIES(perfetto_generated
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../ext
    )
    TARGET_COMPILE_OPTIONS(perfetto_generated PRIVATE "-Wno-error")
ENDIF()

# Create a list of files to hash for the generated hash file
SET(GLOB FILES_TO_HASH
    ${CMAKE_CURRENT_SOURCE_DIR}/events-*.xml)
SET(FILES_TO_HASH ${FILES_TO_HASH}
    ${GATORD_SRC_FILES}
    ${GATORD_3RD_PARTY_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/events_header.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/events_footer.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/pmus.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/defaults.xml)
LIST(FILTER FILES_TO_HASH EXCLUDE REGEX "/ipc/proto/generated/|capture_configuration\\.pb\\.h|SrcMd5\\.cpp|_xml\\.h|events\\.xml")

CREATE_SRC_MD5("gSrcMd5" "gBuildId" "gCopyrightYear" "${GATORD_BUILD_ID}" "${COPYRIGHT_YEAR}" "${GENERATED_MD5_SOURCE}" "${GENERATED_MD5_FILE}" ${FILES_TO_HASH})

# Build the merged events.xml
FILE(GLOB EVENTS_XML_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/events-*.xml)
LIST(SORT EVENTS_XML_FILES)
LIST(REMOVE_DUPLICATES EVENTS_XML_FILES)

# Convert the list to a string that can be passed to the custom command
SET(EVENTS_XML_FILES_STRING "${CMAKE_CURRENT_SOURCE_DIR}/events_header.xml")

FOREACH(EVENTS_XML_FILE ${EVENTS_XML_FILES})
    SET(EVENTS_XML_FILES_STRING
        "${EVENTS_XML_FILES_STRING};${EVENTS_XML_FILE}")
ENDFOREACH()

SET(EVENTS_XML_FILES_STRING "${EVENTS_XML_FILES_STRING};${CMAKE_CURRENT_SOURCE_DIR}/events_footer.xml")

SET(CONCATENATED_EVENTS_XML ${CMAKE_CURRENT_BINARY_DIR}/events.xml)
ADD_CUSTOM_COMMAND(OUTPUT ${CONCATENATED_EVENTS_XML}
    COMMAND ${CMAKE_COMMAND} -DOUTPUT_FILE="${CONCATENATED_EVENTS_XML}"
    -DFILES_TO_CONCATENATE="${EVENTS_XML_FILES_STRING}"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/concatenate-files.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/events_header.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/events_footer.xml
    ${EVENTS_XML_FILES}
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/concatenate-files.cmake)

# Macro to aid generation of xml->header files
SET(ALL_XML_HEADERS)

MACRO(CREATE_XML_HEADER CONSTANT_NAME
    SOURCE_XML_FILE
    OUTPUT_HEADER_FILE)
    ESCAPE_FILE_TO_C_STRING("${CONSTANT_NAME}" "${SOURCE_XML_FILE}" "${OUTPUT_HEADER_FILE}")
    LIST(APPEND ALL_XML_HEADERS ${OUTPUT_HEADER_FILE})
ENDMACRO()

# Target to generate defaults_xml.h
CREATE_XML_HEADER(DEFAULTS_XML
    ${CMAKE_CURRENT_SOURCE_DIR}/defaults.xml
    ${CMAKE_CURRENT_BINARY_DIR}/defaults_xml.h)

# Target to generate pmus_xml.h
CREATE_XML_HEADER(PmuXML::DEFAULT_XML
    ${CMAKE_CURRENT_SOURCE_DIR}/pmus.xml
    ${CMAKE_CURRENT_BINARY_DIR}/pmus_xml.h)

# Target to generate events.h
CREATE_XML_HEADER(DEFAULT_EVENTS_XML
    ${CONCATENATED_EVENTS_XML}
    ${CMAKE_CURRENT_BINARY_DIR}/events_xml.h)

# Compile the 3rd party files separately, so that
# the clang-tidy rules can be applied only to the gatord
# target
ADD_LIBRARY(gatord-tpip OBJECT ${GATORD_3RD_PARTY_SOURCES})
TARGET_COMPILE_OPTIONS(gatord-tpip PRIVATE "-Wno-error")

# Generate the gatord executable
ADD_EXECUTABLE(gatord ${GATORD_SRC_FILES}
    ${GENERATED_MD5_SOURCE}
    ${CMAKE_CURRENT_BINARY_DIR}/defaults_xml.h
    ${CMAKE_CURRENT_BINARY_DIR}/events_xml.h
    ${CMAKE_CURRENT_BINARY_DIR}/pmus_xml.h)

TARGET_LINK_LIBRARIES(gatord
    PRIVATE gatord-tpip
    PRIVATE Threads::Threads
    PRIVATE atomic
    PRIVATE device
    PRIVATE ${MXML_TARGET}
    PRIVATE Boost::boost
    PRIVATE Boost::filesystem
    PRIVATE Boost::regex
    PRIVATE ipcproto
    PRIVATE dl
)

IF(NOT ANDROID)
    TARGET_LINK_LIBRARIES(gatord
        PRIVATE rt
        PRIVATE m
    )
ENDIF()

IF(CONFIG_USE_PERFETTO)
    TARGET_COMPILE_DEFINITIONS(gatord
        PRIVATE CONFIG_USE_PERFETTO
    )
    TARGET_LINK_LIBRARIES(gatord PRIVATE perfetto_generated)
ENDIF()

# Strip binary
IF(NOT((${CMAKE_BUILD_TYPE} STREQUAL "Debug") OR(${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")))
    ADD_CUSTOM_COMMAND(TARGET gatord POST_BUILD
        COMMAND ${CMAKE_STRIP} --strip-unneeded --strip-debug $<TARGET_FILE:gatord>
        COMMENT "Stripping gatord")
ENDIF()

#
# Strip target?
#
STRIP_TARGET("gatord")

# Installation configuration
IF(NOT DEFINED GATOR_INSTALL_PREFIX)
    SET(GATOR_INSTALL_PREFIX "share/gator-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
ENDIF()

SET(GATORD_INSTALL_DIR ./${GATOR_INSTALL_PREFIX}/daemon/)

INSTALL(TARGETS gatord
    RUNTIME DESTINATION ${GATORD_INSTALL_DIR})

INSTALL(FILES ${CMAKE_CURRENT_SOURCE_DIR}/COPYING
    DESTINATION ${GATORD_INSTALL_DIR})

#
# Add clang-format and clang-tidy
#
add_clang_tools(gatord ${GATORD_SRC_FILES})
