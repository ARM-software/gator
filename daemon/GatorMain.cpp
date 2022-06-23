/* Copyright (C) 2010-2022 by Arm Limited. All rights reserved. */

#include "GatorMain.h"

#include "ConfigurationXML.h"
#include "CounterXML.h"
#include "ExitStatus.h"
#include "GatorCLIParser.h"
#include "GatorException.h"
#include "ICpuInfo.h"
#include "ParserResult.h"
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
#include "logging/global_log.h"
#include "logging/suppliers.h"
#include "xml/EventsXML.h"
#include "xml/PmuXMLParser.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>
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

using gator::android::IAppGatorRunner;
using gator::io::IMonitor;

namespace {
    std::array<int, 2> signalPipe;

    // Signal Handler
    void handler(int signum)
    {
        if (::write(signalPipe[1], &signum, sizeof(signum)) != sizeof(signum)) {
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            LOG_ERROR("write failed (%d) %s", errno, strerror(errno));
            handleException();
        }
    }

    //Gator ready messages
    constexpr std::string_view gator_shell_ready = "Gator ready";
    constexpr std::string_view gator_agent_ready = "Gator agent ready";
    constexpr std::string_view start_app_msg = "start app\n";
    constexpr int AGENT_STD_OUT_UNEXPECTED_MESSAGE_LIMIT = 32;
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
}

void dumpCounterDetails(const ParserResult & result, logging::log_setup_supplier_t log_setup_supplier)
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
                                          log_setup_supplier)
                         .get();
    }
    if (result.printables.count(ParserResult::Printable::DEFAULT_CONFIGURATION_XML) == 1) {
        std::cout << configuration_xml::getDefaultConfigurationXml(
                         drivers.getPrimarySourceProvider().getCpuInfo().getClusters())
                         .get();
    }
}

int startAppGator(int argc, char ** argv)
{
    auto stripped_args = std::vector<char *>();
    // TODO: use Boost filter iterator
    stripped_args.push_back(argv[0]);
    stripped_args.insert(stripped_args.end(), &argv[2], &argv[argc]);

    auto global_logging = std::make_shared<logging::global_log_sink_t>();

    logging::set_log_sink(global_logging);
    logging::last_log_error_supplier_t last_log_error_supplier {
        [global_logging]() { return global_logging->get_last_log_error(); }};
    logging::log_setup_supplier_t log_setup_supplier {
        [global_logging]() { return global_logging->get_log_setup_messages(); }};

    GatorCLIParser parser;
    global_logging->set_debug_enabled(GatorCLIParser::hasDebugFlag(argc, argv));

    if (stripped_args.size() > std::numeric_limits<int>::max()) {
        LOG_ERROR("Command line too long");
        return EXCEPTION_EXIT_CODE;
    }
    parser.parseCLIArguments(static_cast<int>(stripped_args.size()),
                             stripped_args.data(),
                             "",
                             MAX_PERFORMANCE_COUNTERS,
                             gSrcMd5);
    const ParserResult & result = parser.result;
    if (result.mode == ParserResult::ExecutionMode::EXIT) {
        handleException();
    }

    updateSessionData(result);

    if (gSessionData.mLocalCapture) {
        //for agent capture apc will be created in /data/data/<pkgname>
        //which is the cwd.
        std::array<char, PATH_MAX> cwd;
        if (getcwd(cwd.data(), cwd.size()) != nullptr) {
            std::string appCwd(cwd.data());
            auto apcPathInPackage = android_utils::getApcFolderInAndroidPackage(appCwd, result.mTargetPath);
            if (apcPathInPackage.has_value()) {
                if (gSessionData.mTargetPath != nullptr) {
                    free(const_cast<char *>(gSessionData.mTargetPath));
                }
                gSessionData.mTargetPath = strdup(apcPathInPackage.value().c_str());
                LOG_DEBUG("The directory will be created at '%s'", gSessionData.mTargetPath);
            }
            else {
                LOG_ERROR("Failed to create the directory in android package");
                return EXCEPTION_EXIT_CODE;
            }
        }
        else {
            LOG_ERROR("Failed to create the directory in android package");
            return EXCEPTION_EXIT_CODE;
        }
    }
    // Call before setting up the SIGCHLD handler, as system() spawns child processes
    Drivers drivers {result.mSystemWide,
                     readPmuXml(result.pmuPath),
                     result.mDisableCpuOnlining,
                     result.mDisableKernelAnnotations,
                     TraceFsConstants::detect()};

    // Handle child exit codes
    signal(SIGCHLD, handler);

    // an event handler that waits for capture events and forwards the notifications
    // to the parent process via this process' stdout pipe
    class local_event_handler_t : public capture::capture_process_event_listener_t {
    public:
        ~local_event_handler_t() override = default;

        void process_initialised() override { std::cout << gator_agent_ready.data() << std::endl; }

        void waiting_for_target() override { std::cout << start_app_msg.data() << std::endl; }

    } event_handler {};

    capture::beginCaptureProcess(result,
                                 drivers,
                                 signalPipe,
                                 last_log_error_supplier,
                                 log_setup_supplier,
                                 event_handler);
    return 0;
}

struct StateAndPid {
    bool exited;
    /**
     * PID will contain the exit code once the process has finished.
     */
    int pid;
};

int waitForAppAgentToExit(const std::string & packageName,
                          const std::string & activityName,
                          IAppGatorRunner & runner,
                          IMonitor & monitor,
                          const lib::PopenResult & cmdResult)
{
    std::string match(gator_agent_ready.data());
    match.append("\n");
    std::string matchStartAppMessage(start_app_msg.data());

    StateAndPid agentState {false, 0};
    bool isStdOutReadFinished = false;
    bool isStdErrReadFinished = false;

    bool isGatorReadyReceived = false;
    bool isStartAppReceived = false;

    std::string appGatorMessageReader;
    std::string appGatorErrorReader;

    auto activityManager = create_android_activity_manager(packageName, activityName);
    if (!activityManager || !activityManager->stop()) {
        LOG_WARNING("Attempt to stop the target activity failed. It may need to be terminated manually.");
    }
    while (!agentState.exited || !isStdOutReadFinished || !isStdErrReadFinished) {
        std::array<epoll_event, 3> events;
        int ready = monitor.wait(events.data(), events.size(), -1);
        if (ready < 0) {
            LOG_ERROR("Epoll wait on app gator FDs failed");
            break;
        }
        for (int i = 0; i < ready; ++i) {
            if (events[i].data.fd == cmdResult.out) {
                char value = '\0';
                if (lib::read(cmdResult.out, &value, 1) <= 0) { //EOF or error
                    if (agentState.exited) {
                        isStdOutReadFinished = true;
                        continue;
                    }
                }
                if (isGatorReadyReceived && isStartAppReceived) {
                    //Not processing any other std::out from agent
                    continue;
                }
                appGatorMessageReader += value;
                if (!isGatorReadyReceived) {
                    if (match.rfind(appGatorMessageReader, 0) == std::string::npos) {
                        appGatorMessageReader.clear();
                    }
                    isGatorReadyReceived = (appGatorMessageReader == match);
                    if (isGatorReadyReceived) {
                        std::cout << gator_shell_ready.data() << std::endl;
                        std::cout.flush();
                        appGatorMessageReader.clear();
                    }
                }
                else if (!isStartAppReceived) {
                    if (matchStartAppMessage.rfind(appGatorMessageReader, 0) == std::string::npos) {
                        appGatorMessageReader.clear();
                    }
                    isStartAppReceived = appGatorMessageReader == matchStartAppMessage;
                    if (isStartAppReceived) {
                        appGatorMessageReader.clear();
                        if (activityManager) {
                            if (!activityManager->start()) {
                                LOG_ERROR("Failed to start activity (%s) from package (%s)", //
                                          activityName.c_str(),                              //
                                          packageName.c_str());
                                // send a signal to the child process so that it can exit cleanly.
                                // we'll get a SIGCHLD when it exits.
                                runner.sendSignalsToAppGator(SIGTERM);
                                //handling exception here to make sure the unused APC directory is deleted.
                                handleException();
                            }
                        }
                        else {
                            LOG_DEBUG("Application (%s) could not be started, please start manually.",
                                      packageName.c_str());
                        }
                    }
                }
                else {
                    if (appGatorMessageReader.length() > AGENT_STD_OUT_UNEXPECTED_MESSAGE_LIMIT) {
                        LOG_DEBUG("Unexpected messages in std::out from agent (message = %s )",
                                  appGatorMessageReader.c_str());
                        appGatorMessageReader.clear();
                    }
                }
            }
            else if (events[i].data.fd == cmdResult.err) {
                char value = '\0';
                if (lib::read(cmdResult.err, &value, 1) <= 0) { //EOF or error
                    if (agentState.exited) {
                        isStdErrReadFinished = true;
                    }
                }
                if (value != '\n') {
                    appGatorErrorReader += value;
                }
                else {
                    //Log what ever read so far
                    LOG_ERROR("From Agent %s", appGatorErrorReader.c_str());
                    appGatorErrorReader.clear();
                }
            }
            else if (events[i].data.fd == signalPipe[0]) {
                int signum;
                const auto amountRead = lib::read(signalPipe[0], &signum, sizeof(signum));
                if (amountRead != sizeof(signum)) {
                    // NOLINTNEXTLINE(concurrency-mt-unsafe)
                    LOG_DEBUG("read failed %d  %s", errno, strerror(errno));
                }
                if (signum == SIGCHLD) {
                    LOG_DEBUG("Received SIGCHILD");

                    int status = 0;
                    auto pid = lib::waitpid(-1, &status, WNOHANG);

                    // NOLINTNEXTLINE(hicpp-signed-bitwise)
                    if (pid == pid_t(-1)) {
                        // NOLINTNEXTLINE(concurrency-mt-unsafe)
                        LOG_DEBUG("waitpid() failed %d (%s)", errno, strerror(errno));
                        // wasn't gator-child  or it was but just a stop/continue
                        // so just ignore it
                        continue;
                    }

                    if (pid == 0) {
                        LOG_DEBUG("waitpid() returned zero; spurious SIGCHILD");
                        continue;
                    }

                    LOG_DEBUG("waitpid() succeeded with status=%d, pid=%d", status, pid);

                    if (pid != cmdResult.pid) {
                        LOG_DEBUG("... Ignoring as not the child process");
                        continue;
                    }

                    if (agentState.exited) {
                        LOG_DEBUG("... Ignoring as already exited");
                        continue;
                    }

                    // NOLINTNEXTLINE(hicpp-signed-bitwise)
                    if (WIFEXITED(status)) {
                        // NOLINTNEXTLINE(hicpp-signed-bitwise)
                        agentState = {true, WEXITSTATUS(status)};
                    }
                    // NOLINTNEXTLINE(hicpp-signed-bitwise)
                    else if (WIFSIGNALED(status)) {
                        // NOLINTNEXTLINE(hicpp-signed-bitwise)
                        agentState = {true, WTERMSIG(status)};
                    }
                    else {
                        LOG_DEBUG("... Ignoring as not exited or signal");
                    }
                }
                else {
                    LOG_DEBUG("Forwarding signal %d to child process", signum);
                    runner.sendSignalsToAppGator(signum);
                }
            }
        }
    }

    // capture has ended so try to stop the target app
    if (activityManager && !activityManager->stop()) {
        LOG_WARNING("Attempt to stop the target activity failed. It may need to be terminated manually.");
    }
    return agentState.pid;
}

int startShellGator(const ParserResult & result)
{
    auto maybe_app_gator_path = gSessionData.mAndroidPackage != nullptr                             //
                                  ? gator::android::deploy_to_package(gSessionData.mAndroidPackage) //
                                  : std::nullopt;

    if (!maybe_app_gator_path) {
        LOG_ERROR("Unable to copy gatord to the target directory.");
        handleException();
    }

    if (gSessionData.mLocalCapture && !android_utils::canCreateApcDirectory(gSessionData.mTargetPath)) {
        LOG_ERROR("Failed to create the directory '%s'", gSessionData.mTargetPath);
        handleException();
    }
    int exitCode = 0;
    const auto * const activityName = result.mAndroidActivity == nullptr ? "" : result.mAndroidActivity;
    // Handle child exit codes
    signal(SIGCHLD, handler);

    auto runner =
        gator::android::create_app_gator_runner(maybe_app_gator_path.value(), result.mAndroidPackage, "--child");
    // start an epoll loop to read from the child & signal pipes
    auto cmdResult = runner->startGator(result.getArgValuePairs());
    auto monitor = gator::io::create_monitor();
    if (!monitor->init() || !monitor->add(signalPipe[0]) || !monitor->add(cmdResult->out)
        || !monitor->add(cmdResult->err)) {
        LOG_ERROR("Failed to set up the IO event loop. Capture cannot continue.");
        handleException();
    }

    if (cmdResult.has_value()) {
        if (cmdResult->pid < 0) {
            LOG_ERROR("Failed to start a gator process. Errno: %d", cmdResult->pid);
            return cmdResult->pid;
        }

        exitCode = waitForAppAgentToExit(result.mAndroidPackage, activityName, *runner, *monitor, cmdResult.value());

        if (exitCode == 0) {
            if (gSessionData.mLocalCapture && gSessionData.mAndroidPackage != nullptr
                && gSessionData.mTargetPath != nullptr) {
                if (!android_utils::copyApcToActualPath(std::string(gSessionData.mAndroidPackage),
                                                        std::string(gSessionData.mTargetPath))) {
                    LOG_ERROR("There was an error while copying apc, please try manually to pull from (/data/data/%s)",
                              gSessionData.mAndroidPackage);
                }
            }
        }
    }
    else {
        LOG_ERROR("Failed to get a popenresult ");
        return EXCEPTION_EXIT_CODE;
    }

    return exitCode;
}

int gator_local_capture(const ParserResult & result,
                        const logging::last_log_error_supplier_t & last_log_error_supplier,
                        const logging::log_setup_supplier_t & log_setup_supplier)
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

        void waiting_for_target() override
        {
            if (activity_manager) {
                LOG_DEBUG("Starting the target application now...");
                if (!activity_manager->start()) {
                    LOG_ERROR("The target application could not be started automatically. Please start it manually.");
                }
            }
        }

    private:
        std::unique_ptr<IAndroidActivityManager> activity_manager;

    } event_handler {};

    // we're starting gator in legacy mode - run the loop as normal
    return capture::beginCaptureProcess(result,
                                        drivers,
                                        signalPipe,
                                        last_log_error_supplier,
                                        log_setup_supplier,
                                        event_handler);
}

// Gator data flow: collector -> collector fifo -> sender
int gator_main(int argc, char ** argv)
{
    // Set up global thread-safe logging
    auto global_logging = std::make_shared<logging::global_log_sink_t>();

    logging::set_log_sink(global_logging);
    logging::last_log_error_supplier_t last_log_error_supplier {
        [global_logging]() { return global_logging->get_last_log_error(); }};
    logging::log_setup_supplier_t log_setup_supplier {
        [global_logging]() { return global_logging->get_log_setup_messages(); }};
    // and enable debug mode
    global_logging->set_debug_enabled(GatorCLIParser::hasDebugFlag(argc, argv));

    gSessionData.initialize();
    //setting default values of gSessionData
    setDefaults();

    const int pipeResult = lib::pipe2(signalPipe, O_CLOEXEC);
    if (pipeResult == -1) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        LOG_ERROR("pipe failed (%d) %s", errno, strerror(errno));
        handleException();
    }

    signal(SIGINT, handler);
    signal(SIGTERM, handler);
    signal(SIGABRT, handler);
    signal(SIGHUP, handler);
    signal(SIGUSR1, handler);
    gator::process::set_parent_death_signal(SIGHUP);

    // check for the special command line arg to see if we're being asked to
    // start in child mode
    if (argc > 2 && strcmp("--child", argv[1]) == 0) {
        return startAppGator(argc, argv);
    }

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
    parser.parseCLIArguments(argc, argv, versionString, MAX_PERFORMANCE_COUNTERS, gSrcMd5);
    const ParserResult & result = parser.result;
    if (result.mode == ParserResult::ExecutionMode::EXIT) {
        handleException();
    }

    updateSessionData(result);

    // configure any environment settings we'll need to start sampling
    // e.g. perf security settings.
    auto environment = capture::prepareCaptureEnvironment(gSessionData);

    // if we're not being asked to do a system-wide capture then start the gator agent in the
    // context of the target android app
    if (!gSessionData.mSystemWide && gSessionData.mAndroidPackage != nullptr) {
        return startShellGator(result);
    }

    if (result.mode == ParserResult::ExecutionMode::PRINT) {
        dumpCounterDetails(result, log_setup_supplier);
    }
    else {
        return gator_local_capture(result, last_log_error_supplier, log_setup_supplier);
    }

    return 0;
}
