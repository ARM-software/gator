/* Copyright (C) 2010-2023 by Arm Limited. All rights reserved. */

#include "GatorMain.h"

#include "ConfigurationXML.h"
#include "CounterXML.h"
#include "ExitStatus.h"
#include "GatorCLIFlags.h"
#include "GatorCLIParser.h"
#include "GatorException.h"
#include "ICpuInfo.h"
#include "ParserResult.h"
#include "ProtocolVersion.h"
#include "SessionData.h"
#include "android/AndroidActivityManager.h"
#include "android/AppGatorRunner.h"
#include "android/Spawn.h"
#include "android/Utils.h"
#include "capture/CaptureProcess.h"
#include "capture/Environment.h"
#include "lib/FileDescriptor.h"
#include "lib/Popen.h"
#include "lib/Process.h"
#include "lib/String.h"
#include "lib/Syscall.h"
#include "lib/Utils.h"
#include "logging/file_log_sink.h"
#include "logging/global_log.h"
#include "logging/std_log_sink.h"
#include "logging/suppliers.h"
#include "xml/EventsXML.h"
#include "xml/PmuXMLParser.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <ios>
#include <iostream>
#include <limits>
#include <sstream>
#include <string_view>

#include <Drivers.h>
#include <Monitor.h>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
    std::array<int, 2> signalPipe;

    // Signal Handler
    void handler(int signum)
    {
        if (::write(signalPipe[1], &signum, sizeof(signum)) != sizeof(signum)) {
            handleException();
        }
    }

    //Gator ready messages
    constexpr std::string_view gator_shell_ready = "Gator ready";
    constexpr unsigned int VERSION_STRING_CHAR_SIZE = 256;
}

void setDefaults()
{
    //default system wide.
    gSessionData.mSystemWide = false;
    // buffer_mode is normal
    gSessionData.mOneShot = false;
    gSessionData.mTotalBufferSize = 4;
    gSessionData.mPerfMmapSizeInPages = -1;
    //callStack unwinding default is yes
    gSessionData.mBacktraceDepth = 128;
    //sample rate is normal
    gSessionData.mSampleRate = normal;
    //duration default to 0
    gSessionData.mDuration = 0;
    //use_efficient_ftrace default is yes
    gSessionData.mFtraceRaw = true;
#if defined(WIN32)
    //TODO
    gSessionData.mCaptureUser = nullptr;
    gSessionData.mCaptureWorkingDir = nullptr;
#else
    // default to current user
    gSessionData.mCaptureUser = nullptr;

    // use current working directory
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) != nullptr) {
        gSessionData.mCaptureWorkingDir = strdup(cwd);
    }
    else {
        gSessionData.mCaptureWorkingDir = nullptr;
    }
#endif
}

void updateSessionData(const ParserResult & result)
{
    gSessionData.mLocalCapture = result.mode == ParserResult::ExecutionMode::LOCAL_CAPTURE;
    gSessionData.mAndroidApiLevel = result.mAndroidApiLevel;
    gSessionData.mConfigurationXMLPath = result.mConfigurationXMLPath;
    gSessionData.mEventsXMLAppend = result.mEventsXMLAppend;
    gSessionData.mEventsXMLPath = result.mEventsXMLPath;
    gSessionData.mSessionXMLPath = result.mSessionXMLPath;
    gSessionData.mSystemWide = result.mSystemWide;
    gSessionData.mExcludeKernelEvents = result.mExcludeKernelEvents;
    gSessionData.mWaitForProcessCommand = result.mWaitForCommand;
    gSessionData.mPids = result.mPids;
    gSessionData.mLogToFile = result.mLogToFile;

    if (result.mTargetPath != nullptr) {
        if (gSessionData.mTargetPath != nullptr) {
            free(const_cast<char *>(gSessionData.mTargetPath));
        }
        gSessionData.mTargetPath = strdup(result.mTargetPath);
    }

    gSessionData.mAllowCommands = result.mAllowCommands;
    gSessionData.parameterSetFlag = result.parameterSetFlag;
    gSessionData.mStopOnExit = result.mStopGator;
    gSessionData.mPerfMmapSizeInPages = result.mPerfMmapSizeInPages;
    gSessionData.mSpeSampleRate = result.mSpeSampleRate;
    gSessionData.mAndroidPackage = result.mAndroidPackage;
    gSessionData.mAndroidActivity = result.mAndroidActivity;
    gSessionData.smmu_identifiers = result.smmu_identifiers;

    // when profiling an android package, use the package name as the '--wait-process' value
    if ((gSessionData.mAndroidPackage != nullptr) && (gSessionData.mWaitForProcessCommand == nullptr)) {
        gSessionData.mWaitForProcessCommand = gSessionData.mAndroidPackage;
    }

    //These values are set from command line and are alos part of session.xml
    //and hence cannot be modified during parse session
    if ((result.parameterSetFlag & USE_CMDLINE_ARG_SAMPLE_RATE) != 0) {
        gSessionData.mSampleRate = result.mSampleRate;
    }
    if ((result.parameterSetFlag & USE_CMDLINE_ARG_CALL_STACK_UNWINDING) != 0) {
        gSessionData.mBacktraceDepth = result.mBacktraceDepth;
    }
    if ((result.parameterSetFlag & USE_CMDLINE_ARG_CAPTURE_WORKING_DIR) != 0) {
        gSessionData.mCaptureWorkingDir = strdup(result.mCaptureWorkingDir);
    }
    if ((result.parameterSetFlag & USE_CMDLINE_ARG_CAPTURE_COMMAND) != 0) {
        gSessionData.mCaptureCommand = result.mCaptureCommand;
    }
    if ((result.parameterSetFlag & USE_CMDLINE_ARG_DURATION) != 0) {
        gSessionData.mDuration = result.mDuration;
    }
    if ((result.parameterSetFlag & USE_CMDLINE_ARG_FTRACE_RAW) != 0) {
        gSessionData.mFtraceRaw = result.mFtraceRaw;
    }
    if ((result.parameterSetFlag & USE_CMDLINE_ARG_OFF_CPU_PROFILING) != 0) {
        gSessionData.mEnableOffCpuSampling = result.mEnableOffCpuSampling;
    }
}

void dumpCounterDetails(const ParserResult & result, const logging::log_access_ops_t & log_ops)
{
    Drivers drivers {result.mSystemWide,
                     readPmuXml(result.pmuPath),
                     result.mDisableCpuOnlining,
                     result.mDisableKernelAnnotations,
                     TraceFsConstants::detect()};

    if (result.printables.count(ParserResult::Printable::EVENTS_XML) == 1) {
        std::cout << events_xml::getDynamicXML(drivers.getAllConst(),
                                               drivers.getPrimarySourceProvider().getCpuInfo().getClusters(),
                                               drivers.getPrimarySourceProvider().getDetectedUncorePmus())
                         .get();
    }
    if (result.printables.count(ParserResult::Printable::COUNTERS_XML) == 1) {
        std::cout << counters_xml::getXML(drivers.getPrimarySourceProvider().supportsMultiEbs(),
                                          drivers.getAllConst(),
                                          drivers.getPrimarySourceProvider().getCpuInfo(),
                                          log_ops)
                         .get();
    }
    if (result.printables.count(ParserResult::Printable::DEFAULT_CONFIGURATION_XML) == 1) {
        std::cout << configuration_xml::getDefaultConfigurationXml(
                         drivers.getPrimarySourceProvider().getCpuInfo().getClusters())
                         .get();
    }
}

int start_capture_process(const ParserResult & result, logging::log_access_ops_t & log_ops)
{
    // Call before setting up the SIGCHLD handler, as system() spawns child processes
    Drivers drivers {result.mSystemWide,
                     readPmuXml(result.pmuPath),
                     result.mDisableCpuOnlining,
                     result.mDisableKernelAnnotations,
                     TraceFsConstants::detect()};

    // Handle child exit codes
    signal(SIGCHLD, handler);

    class local_event_handler_t : public capture::capture_process_event_listener_t {
    public:
        local_event_handler_t()
        {
            if (gSessionData.mAndroidPackage != nullptr && gSessionData.mAndroidActivity != nullptr) {
                activity_manager =
                    create_android_activity_manager(gSessionData.mAndroidPackage, gSessionData.mAndroidActivity);
            }
        }

        ~local_event_handler_t() override
        {
            if (activity_manager) {
                static_cast<void>(activity_manager->stop());
            }
        }

        void process_initialised() override
        {
            // This line has to be printed because Streamline needs to detect when
            // gator is ready to listen and accept socket connections via adb forwarding.  Without this
            // print out there is a chance that Streamline establishes a connection to the adb forwarder,
            // but the forwarder cannot establish a connection to a gator, because gator is not up and listening
            // for sockets yet.  If the adb forwarder cannot establish a connection to gator, what streamline
            // experiences is a successful socket connection, but when it attempts to read from the socket
            // it reads an empty line when attempting to read the gator protocol header, and terminates the
            // connection.
            std::cout << gator_shell_ready.data() << std::endl;
        }

        [[nodiscard]] bool waiting_for_target() override
        {
            if (!activity_manager) {
                return true;
            }

            LOG_DEBUG("Starting the target application now...");
            if (activity_manager->start()) {
                return true;
            }

            LOG_ERROR("The target application could not be started automatically. Please start it manually.");
            return false;
        }

    private:
        std::unique_ptr<IAndroidActivityManager> activity_manager;

    } event_handler {};

    // we're starting gator in legacy mode - run the loop as normal
    return capture::beginCaptureProcess(result, drivers, signalPipe, log_ops, event_handler);
}

// Gator data flow: collector -> collector fifo -> sender
int gator_main(int argc, char ** argv)
{
    // Set up global thread-safe logging
    auto global_logging = std::make_shared<logging::global_logger_t>();
    global_logging->add_sink<logging::std_log_sink_t>();
    logging::set_logger(global_logging);

    // and enable debug mode
    global_logging->set_debug_enabled(GatorCLIParser::hasDebugFlag(argc, argv));

    // enable fine level logging mode
    global_logging->set_fine_enabled(GatorCLIParser::hasCaptureLogFlag(argc, argv));

    // write the log to a file, if requested
    if (GatorCLIParser::hasCaptureLogFlag(argc, argv)) {
        try {
            global_logging->add_sink<logging::file_log_sink_t>();
        }
        catch (std::ios_base::failure & e) {
            LOG_ERROR("Log setup error: %s", e.what());
            handleException();
        }
    }

    gSessionData.initialize();
    //setting default values of gSessionData
    setDefaults();

    const int pipeResult = lib::pipe2(signalPipe, O_CLOEXEC);
    if (pipeResult == -1) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        LOG_ERROR("pipe failed (%d) %s", errno, strerror(errno));
        handleException();
    }

    (void) signal(SIGINT, handler);
    (void) signal(SIGTERM, handler);
    (void) signal(SIGABRT, handler);
    (void) signal(SIGHUP, handler);
    (void) signal(SIGUSR1, handler);
    gator::process::set_parent_death_signal(SIGKILL);

    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-main"), 0, 0, 0);

    lib::printf_str_t<VERSION_STRING_CHAR_SIZE> versionString;
    {
        const int baseProtocolVersion =
            (PROTOCOL_VERSION >= 0 ? PROTOCOL_VERSION : -(PROTOCOL_VERSION % PROTOCOL_VERSION_DEV_MULTIPLIER));
        const int protocolDevTag = (PROTOCOL_VERSION >= 0 ? 0 : -(PROTOCOL_VERSION / PROTOCOL_VERSION_DEV_MULTIPLIER));
        const int majorVersion = baseProtocolVersion / 100;
        const int minorVersion = (baseProtocolVersion / 10) % 10;
        const int revisionVersion = baseProtocolVersion % 10;
        const char * formatString =
            (PROTOCOL_VERSION >= 0 ? (revisionVersion == 0 ? "Streamline gatord version %d (Streamline v%d.%d)"
                                                           : "Streamline gatord version %d (Streamline v%d.%d.%d)")
                                   : "Streamline gatord development version %d (Streamline v%d.%d.%d), tag %d");

        versionString
            .printf(formatString, PROTOCOL_VERSION, majorVersion, minorVersion, revisionVersion, protocolDevTag);
    }
    // Parse the command line parameters
    GatorCLIParser parser;
    parser.parseCLIArguments(argc, argv, versionString, MAX_PERFORMANCE_COUNTERS, gSrcMd5, gBuildId);
    const ParserResult & result = parser.result;
    if (result.mode == ParserResult::ExecutionMode::EXIT) {
        handleException();
    }

    updateSessionData(result);

    // configure any environment settings we'll need to start sampling
    // e.g. perf security settings.
    auto environment = capture::prepareCaptureEnvironment();
    environment->postInit(gSessionData);

    if (result.mode == ParserResult::ExecutionMode::PRINT) {
        dumpCounterDetails(result, *global_logging);
    }
    else {
        return start_capture_process(result, *global_logging);
    }

    return 0;
}
