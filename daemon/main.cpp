/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#include "AnnotateListener.h"
#include "Child.h"
#include "ConfigurationXML.h"
#include "CounterXML.h"
#include "Drivers.h"
#include "ExitStatus.h"
#include "GatorCLIParser.h"
#include "ICpuInfo.h"
#include "Logging.h"
#include "Monitor.h"
#include "OlySocket.h"
#include "OlyUtility.h"
#include "Sender.h"
#include "SessionData.h"
#include "StreamlineSetupLoop.h"
#include "lib/FileDescriptor.h"
#include "lib/Memory.h"
#include "lib/Utils.h"
#include "xml/CurrentConfigXML.h"
#include "linux/perf/PerfUtils.h"
#include "xml/EventsXML.h"
#include "xml/PmuXMLParser.h"

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cinttypes>
#include <fcntl.h>
#include <iostream>
#include <iterator>
#include <map>
#include <pthread.h>
#include <pwd.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

static pthread_mutex_t numChildProcesses_mutex;
static std::unique_ptr<OlyServerSocket> socketUds;
static std::unique_ptr<OlyServerSocket> socketTcp;
static Monitor monitor;

static const char NO_TCP_PIPE[] = "\0streamline-data";

namespace {
    enum class State {
        IDLE,
        CAPTURING,
        EXITING, //< CAPTURING but we have received a request to exit
    };
    struct StateAndPid {
        State state;
        int pid;
    };
}

void cleanUp()
{
    socketUds.reset();
    socketTcp.reset();
}

static int signalPipe[2];

// Signal Handler
static void handler(int signum)
{
    if (::write(signalPipe[1], &signum, sizeof(signum)) != sizeof(signum)) {
        logg.logError("read failed (%d) %s", errno, strerror(errno));
        handleException();
    }
}

static StateAndPid handleSigchld(StateAndPid currentStateAndChildPid, Drivers & drivers)
{
    int status;
    int pid = waitpid(currentStateAndChildPid.pid, &status, WNOHANG);
    if (pid == -1 || !(WIFEXITED(status) || WIFSIGNALED(status))) {
        // wasn't gator-child  or it was but just a stop/continue
        // so just ignore it
        return currentStateAndChildPid;
    }

    for (const auto & driver : drivers.getAll()) {
        driver->postChildExitInParent();
    }

    int exitStatus;
    if (WIFEXITED(status)) {
        exitStatus = WEXITSTATUS(status);
        logg.logMessage("Child process %d terminated normally with status %d", pid, exitStatus);
        if (exitStatus == OK_TO_EXIT_GATOR_EXIT_CODE) {
            logg.logMessage("Received EXIT_OK command. exiting gatord");
            cleanUp();
            exit(0);
        }
    }
    else {
        assert(WIFSIGNALED(status));
        int signal = WTERMSIG(status);
        logg.logMessage("Child process %d was terminated by signal %s (%d)", pid, strsignal(signal), signal);
        // child exit codes start from 1 so should be less than 64.
        // add 64 for signal to differentiate from normal exit.
        // can't use 128 to 255 because that would be used by a shell
        // if this process (gator-main) signalled.
        exitStatus = 64 + signal;
    }

    assert(currentStateAndChildPid.state != State::IDLE);
    if (currentStateAndChildPid.state == State::CAPTURING) {
        return {.state = State::IDLE, .pid = -1};
    }

    // currentStateAndChildPid.state == State::EXITING
    cleanUp();
    exit(exitStatus);
}

static StateAndPid handleSignal(StateAndPid currentStateAndChildPid, Drivers & drivers, int signum)
{

    if (signum == SIGCHLD) {
        return handleSigchld(currentStateAndChildPid, drivers);
    }

    logg.logMessage("Received signal %d, gator daemon exiting", signum);

    switch (currentStateAndChildPid.state) {
        case State::CAPTURING:
            // notify child to exit
            logg.logError("Waiting for gator-child to finish, send SIGKILL or SIGQUIT (Ctrl+\\) to force exit");
            kill(currentStateAndChildPid.pid, SIGINT);
            currentStateAndChildPid.state = State::EXITING;
            break;
        case State::IDLE:
            cleanUp();
            exit(0);
        case State::EXITING:
            logg.logError("Still waiting for gator-child to finish, send SIGKILL or SIGQUIT (Ctrl+\\) to force exit");
            break;
    }
    return currentStateAndChildPid;
}

static const int UDP_REQ_PORT = 30001;

struct RVIConfigureInfo {
    char rviHeader[8];
    uint32_t messageID;
    uint8_t ethernetAddress[8];
    uint32_t ethernetType;
    uint32_t dhcp;
    char dhcpName[40];
    uint32_t ipAddress;
    uint32_t defaultGateway;
    uint32_t subnetMask;
    uint32_t activeConnections;
};

static const char DST_REQ[] = {'D', 'S', 'T', '_', 'R', 'E', 'Q', ' ', 0, 0, 0, 0x64};

class UdpListener {
public:
    UdpListener() : mDstAns(), mReq(-1) {}

    void setup(int port)
    {
        mReq = udpPort(UDP_REQ_PORT);

        // Format the answer buffer
        memset(&mDstAns, 0, sizeof(mDstAns));
        memcpy(mDstAns.rviHeader, "STR_ANS ", sizeof(mDstAns.rviHeader));
        if (gethostname(mDstAns.dhcpName, sizeof(mDstAns.dhcpName) - 1) != 0) {
            logg.logError("gethostname failed");
            handleException();
        }
        // Subvert the defaultGateway field for the port number
        if (port != DEFAULT_PORT) {
            mDstAns.defaultGateway = port;
        }
        // Subvert the subnetMask field for the protocol version
        mDstAns.subnetMask = PROTOCOL_VERSION;
    }

    int getReq() const { return mReq; }

    void handle()
    {
        char buf[128];
        struct sockaddr_in6 sockaddr;
        socklen_t addrlen;
        int read;
        addrlen = sizeof(sockaddr);
        read = recvfrom(mReq, &buf, sizeof(buf), 0, reinterpret_cast<struct sockaddr *>(&sockaddr), &addrlen);
        if (read < 0) {
            logg.logError("recvfrom failed");
            handleException();
        }
        else if ((read == 12) && (memcmp(buf, DST_REQ, sizeof(DST_REQ)) == 0)) {
            // Don't care if sendto fails - gatord shouldn't exit because of it and Streamline will retry
            sendto(mReq, &mDstAns, sizeof(mDstAns), 0, reinterpret_cast<struct sockaddr *>(&sockaddr), addrlen);
        }
    }

    void close() const { ::close(mReq); }

private:
    static int udpPort(int port)
    {
        int s;
        struct sockaddr_in6 sockaddr;
        int on;
        int family = AF_INET6;

        s = socket_cloexec(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
        if (s == -1) {
            family = AF_INET;
            s = socket_cloexec(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (s == -1) {
                logg.logError("socket failed");
                handleException();
            }
        }

        on = 1;
        if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0) {
            logg.logError("setsockopt REUSEADDR failed");
            handleException();
        }

        // Listen on both IPv4 and IPv6
        on = 0;
        if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on)) != 0) {
            logg.logMessage("setsockopt IPV6_V6ONLY failed");
        }

        memset(&sockaddr, 0, sizeof(sockaddr));
        sockaddr.sin6_family = family;
        sockaddr.sin6_port = htons(port);
        sockaddr.sin6_addr = in6addr_any;
        if (bind(s, reinterpret_cast<struct sockaddr *>(&sockaddr), sizeof(sockaddr)) < 0) {
            logg.logError("socket failed");
            handleException();
        }

        return s;
    }

    RVIConfigureInfo mDstAns;
    int mReq;
};

static UdpListener udpListener;
static AnnotateListener annotateListener;

namespace {
    class StreamlineCommandHandler : public IStreamlineCommandHandler {
    public:
        StreamlineCommandHandler() {}

        State handleRequest(char *) override
        {
            logg.logMessage("INVESTIGATE: Received unknown command type COMMAND_REQUEST_XML");
            return State::PROCESS_COMMANDS;
        }
        State handleDeliver(char *) override
        {
            logg.logMessage("INVESTIGATE: Received unknown command type COMMAND_DELIVER_XML");
            return State::PROCESS_COMMANDS;
        }
        State handleApcStart() override
        {
            logg.logMessage("INVESTIGATE: Received unknown command type COMMAND_APC_START");
            return State::EXIT_APC_START;
        }
        State handleApcStop() override
        {
            logg.logMessage("INVESTIGATE: Received unknown command type COMMAND_APC_STOP");
            return State::EXIT_APC_STOP;
        }
        State handleDisconnect() override { return State::EXIT_DISCONNECT; }
        State handlePing() override
        {
            logg.logMessage("INVESTIGATE: Received unknown command type COMMAND_PING");
            return State::PROCESS_COMMANDS;
        }
        State handleExit() override
        {
            logg.logMessage("INVESTIGATE: Received unknown command type COMMAND_EXIT_OK");
            return State::EXIT_OK;
        }
        State handleRequestCurrentConfig() override
        {
            logg.logMessage("INVESTIGATE: Received unknown command type COMMAND_REQUEST_CURRENT_CONFIG");
            return State::PROCESS_COMMANDS_CONFIG;
        }
    };

    /**
     * Handles an incoming connection when there is already a session active.
     *
     * The user may only send the COMMAND_DISCONNECT. All other commands are considered errors.
     *
     * This is used to allow the ADB device scanner to continue to function even during a
     * capture without flooding the console with "Session already active" messages.
     *
     * @param fd The newly accepted connection's file handle
     */
    void handleSecondaryConnection(int fd)
    {
        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-2ndconn"), 0, 0, 0);

        OlySocket client {fd};
        Sender sender(&client);

        // Wait to receive a single command
        StreamlineCommandHandler commandHandler;
        const auto result = streamlineSetupCommandIteration(client, commandHandler, [](bool) -> void {});

        if (result == IStreamlineCommandHandler::State::PROCESS_COMMANDS_CONFIG) {
            auto currentConfigXML =
                current_config_xml::generateCurrentConfigXML(getpid(), // since its main get the pid, instead of ppid
                                                             getuid(),
                                                             gSessionData.mSystemWide,
                                                             gSessionData.mWaitingOnCommand,
                                                             gSessionData.mWaitForProcessCommand,
                                                             gSessionData.mCaptureWorkingDir,
                                                             gSessionData.mPids);
            sender.writeData(currentConfigXML.data(), currentConfigXML.size(), ResponseType::CURRENT_CONFIG, true);
        }
        else if (result != IStreamlineCommandHandler::State::EXIT_DISCONNECT) {
            // the expectation is that the user sends COMMAND_DISCONNECT, so anything else is an error
            logg.logError("Session already in progress");
            sender.writeData(logg.getLastError(), strlen(logg.getLastError()), ResponseType::ERROR, true);
        }

        // Ensure all data is flushed the host receive the data (not closing socket too quick)
        sleep(1);
        client.shutdownConnection();
        client.closeSocket();
    }
}

static StateAndPid handleClient(StateAndPid currentStateAndChildPid,
                                Drivers & drivers,
                                OlyServerSocket & sock,
                                OlyServerSocket * otherSock)
{
    if (currentStateAndChildPid.state != State::IDLE) {
        // A temporary socket connection to host, to transfer error message
        std::thread handler {handleSecondaryConnection, sock.acceptConnection()};
        handler.detach();

        return currentStateAndChildPid;
    }

    OlySocket client(sock.acceptConnection());
    for (const auto & driver : drivers.getAll()) {
        driver->preChildFork();
    }
    int pid = fork();
    if (pid < 0) {
        // Error
        logg.logError("Fork process failed with errno: %d.", errno);
        handleException();
    }
    else if (pid == 0) {
        // Child
        for (const auto & driver : drivers.getAll()) {
            driver->postChildForkInChild();
        }
        sock.closeServerSocket();
        if (otherSock != nullptr) {
            otherSock->closeServerSocket();
        }

        udpListener.close();
        monitor.close();
        annotateListener.close();

        auto child = Child::createLive(drivers, client);
        child->run();
        child.reset();
        exit(0);
    }
    else {
        // Parent
        for (const auto & driver : drivers.getAll()) {
            driver->postChildForkInParent();
        }
        client.closeSocket();
        return {.state = State::CAPTURING, .pid = pid};
    }
}

static StateAndPid doLocalCapture(Drivers & drivers, const Child::Config & config)
{
    for (const auto & driver : drivers.getAll()) {
        driver->preChildFork();
    }
    int pid = fork();
    if (pid < 0) {
        // Error
        logg.logError("Fork process failed with errno: %d.", errno);
        handleException();
    }
    else if (pid == 0) {
        // Child
        for (const auto & driver : drivers.getAll()) {
            driver->postChildForkInChild();
        }
        monitor.close();
        annotateListener.close();

        auto child = Child::createLocal(drivers, config);
        child->run();
        logg.logMessage("gator-child finished running");
        child.reset();

        logg.logMessage("gator-child exiting");
        exit(0);
    }
    else {
        for (const auto & driver : drivers.getAll()) {
            driver->postChildForkInParent();
        }
        return {.state = State::EXITING, // we should exit immediately after this capture finishes
                .pid = pid};
    }
}

void setDefaults()
{
    //default system wide.
    gSessionData.mSystemWide = false;
    //buffer_mode is streaming
    gSessionData.mOneShot = false;
    gSessionData.mTotalBufferSize = 1;
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
    gSessionData.mWaitForProcessCommand = result.mWaitForCommand;
    gSessionData.mPids = result.mPids;

    gSessionData.mTargetPath = result.mTargetPath;
    gSessionData.mAllowCommands = result.mAllowCommands;
    gSessionData.parameterSetFlag = result.parameterSetFlag;
    gSessionData.mStopOnExit = result.mStopGator;
    gSessionData.mPerfMmapSizeInPages = result.mPerfMmapSizeInPages;
    gSessionData.mSpeSampleRate = result.mSpeSampleRate;

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

void updatePerfMmapSize()
{
    // use value from perf_event_mlock_kb
    if ((gSessionData.mPerfMmapSizeInPages <= 0) && (geteuid() != 0) && (gSessionData.mPageSize >= 1024)) {

        // the default seen on most setups is 516kb, if user cannot read the file it is probably
        // because they are on Android in locked down setup so use default value of 128 pages
        gSessionData.mPerfMmapSizeInPages = 128;

        const lib::Optional<std::int64_t> perfEventMlockKb = perf_utils::readPerfEventMlockKb();

        if (perfEventMlockKb.valid() && perfEventMlockKb.get() > 0) {
            const int perfMmapSizeInPages = lib::calculatePerfMmapSizeInPages(std::uint64_t(perfEventMlockKb.get()),
                                                                              std::uint64_t(gSessionData.mPageSize));

            if (perfMmapSizeInPages > 0) {
                gSessionData.mPerfMmapSizeInPages = perfMmapSizeInPages;
            }
        }

        logg.logMessage("Default perf mmap size set to %d pages (%llukb)",
                        gSessionData.mPerfMmapSizeInPages,
                        gSessionData.mPerfMmapSizeInPages * gSessionData.mPageSize / 1024ULL);
    }
}

// Gator data flow: collector -> collector fifo -> sender
int main(int argc, char ** argv)
{
    GatorCLIParser parser;
    // Set up global thread-safe logging
    logg.setDebug(GatorCLIParser::hasDebugFlag(argc, argv));
    gSessionData.initialize();

    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-main"), 0, 0, 0);
    pthread_mutex_init(&numChildProcesses_mutex, nullptr);

    const int pipeResult = pipe2(signalPipe, O_CLOEXEC);
    if (pipeResult == -1) {
        logg.logError("pipe failed (%d) %s", errno, strerror(errno));
        handleException();
    }

    signal(SIGINT, handler);
    signal(SIGTERM, handler);
    signal(SIGABRT, handler);

    // Set to high priority
    if (setpriority(PRIO_PROCESS, syscall(__NR_gettid), -19) == -1) {
        logg.logMessage("setpriority() failed");
    }

    // Try to increase the maximum number of file descriptors
    {
        struct rlimit rlim;
        memset(&rlim, 0, sizeof(rlim));
        if (getrlimit(RLIMIT_NOFILE, &rlim) != 0) {
            logg.logMessage("Unable to get the maximum number of files");
            // Not good, but not a fatal error either
        }
        else {
            rlim.rlim_max = std::max(rlim.rlim_cur, rlim.rlim_max);
            rlim.rlim_cur = std::min(std::max(rlim_t(1) << 15, rlim.rlim_cur), rlim.rlim_max);
            if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
                logg.logMessage("Unable to increase the maximum number of files (%" PRIuMAX ", %" PRIuMAX ")",
                                static_cast<uintmax_t>(rlim.rlim_cur),
                                static_cast<uintmax_t>(rlim.rlim_max));
                // Not good, but not a fatal error either
            }
        }
    }
    //setting default values of gSessionData
    setDefaults();
    char versionString[256];
    if (PROTOCOL_VERSION < PROTOCOL_DEV) {
        const int majorVersion = PROTOCOL_VERSION / 100;
        const int minorVersion = (PROTOCOL_VERSION / 10) % 10;
        const int revisionVersion = PROTOCOL_VERSION % 10;
        if (revisionVersion == 0) {
            snprintf(versionString,
                     sizeof(versionString),
                     "Streamline gatord version %d (Streamline v%d.%d)",
                     PROTOCOL_VERSION,
                     majorVersion,
                     minorVersion);
        }
        else {
            snprintf(versionString,
                     sizeof(versionString),
                     "Streamline gatord version %d (Streamline v%d.%d.%d)",
                     PROTOCOL_VERSION,
                     majorVersion,
                     minorVersion,
                     revisionVersion);
        }
    }
    else {
        snprintf(versionString, sizeof(versionString), "Streamline gatord development version %d", PROTOCOL_VERSION);
    }
    // Parse the command line parameters
    parser.parseCLIArguments(argc, argv, versionString, MAX_PERFORMANCE_COUNTERS, gSrcMd5);
    const ParserResult & result = parser.result;
    if (result.mode == ParserResult::ExecutionMode::EXIT) {
        handleException();
    }

    updateSessionData(result);

    // detect the primary source
    // Call before setting up the SIGCHLD handler, as system() spawns child processes
    Drivers drivers {result.mSystemWide, readPmuXml(result.pmuPath), result.mDisableCpuOnlining, TraceFsConstants::detect()};

    updatePerfMmapSize();

    if (result.mode == ParserResult::ExecutionMode::PRINT) {
        if (result.printables.count(ParserResult::Printable::EVENTS_XML) == 1) {
            std::cout << events_xml::getDynamicXML(drivers.getAllConst(),
                                                   drivers.getPrimarySourceProvider().getCpuInfo().getClusters())
                             .get();
        }
        if (result.printables.count(ParserResult::Printable::COUNTERS_XML) == 1) {
            std::cout << counters_xml::getXML(drivers.getPrimarySourceProvider().supportsMultiEbs(),
                                              drivers.getAllConst(),
                                              drivers.getPrimarySourceProvider().getCpuInfo())
                             .get();
        }
        if (result.printables.count(ParserResult::Printable::DEFAULT_CONFIGURATION_XML) == 1) {
            std::cout << configuration_xml::getDefaultConfigurationXml(
                             drivers.getPrimarySourceProvider().getCpuInfo().getClusters())
                             .get();
        }
        cleanUp();
        return 0;
    }

    // Handle child exit codes
    signal(SIGCHLD, handler);

    // Ignore the SIGPIPE signal so that any send to a broken socket will return an error code instead of asserting a signal
    // Handling the error at the send function call is much easier than trying to do anything intelligent in the sig handler
    signal(SIGPIPE, SIG_IGN);

    annotateListener.setup();
    int pipefd[2];
    if (lib::pipe_cloexec(pipefd) != 0) {
        logg.logError("Unable to set up annotate pipe");
        handleException();
    }
    gSessionData.mAnnotateStart = pipefd[1];
    if (!monitor.init()
#ifdef TCP_ANNOTATIONS
        || !monitor.add(annotateListener.getSockFd())
#endif
        || !monitor.add(annotateListener.getUdsFd()) || !monitor.add(pipefd[0]) || !monitor.add(signalPipe[0])) {
        logg.logError("Monitor setup failed");
        handleException();
    }

    StateAndPid stateAndChildPid = {.state = State::IDLE, .pid = -1};

    // If the command line argument is a session xml file, no need to open a socket
    if (gSessionData.mLocalCapture) {
        Child::Config childConfig {{}, {}};
        for (const auto & event : result.events) {
            CounterConfiguration config;
            config.counterName = event.first;
            config.event = event.second;
            childConfig.events.insert(std::move(config));
        }
        for (const auto & spe : result.mSpeConfigs) {
            childConfig.spes.insert(spe);
        }
        stateAndChildPid = doLocalCapture(drivers, childConfig);
    }
    else {
        // enable TCP socket
        if (result.port != DISABLE_TCP_USE_UDS_PORT) {
            socketTcp.reset(new OlyServerSocket(result.port));
            udpListener.setup(result.port);
            if (!monitor.add(socketTcp->getFd()) || !monitor.add(udpListener.getReq())) {
                logg.logError("Monitor setup failed: couldn't add host listeners");
                handleException();
            }
        }
        // always enable UDS socket
        {
            socketUds.reset(new OlyServerSocket(NO_TCP_PIPE, sizeof(NO_TCP_PIPE), true));
            if (!monitor.add(socketUds->getFd())) {
                logg.logError("Monitor setup failed: couldn't add host listeners");
                handleException();
            }
        }
    }

    // This line has to be printed because Streamline needs to detect when
    // gator is ready to listen and accept socket connections via adb forwarding.  Without this
    // print out there is a chance that Streamline establishes a connection to the adb forwarder,
    // but the forwarder cannot establish a connection to a gator, because gator is not up and listening
    // for sockets yet.  If the adb forwarder cannot establish a connection to gator, what streamline
    // experiences is a successful socket connection, but when it attempts to read from the socket
    // it reads an empty line when attempting to read the gator protocol header, and terminates the
    // connection.
    std::cout << "Gator ready" << std::endl;
    std::cout.flush();

    // Forever loop, can be exited via a signal or exception
    while (1) {
        struct epoll_event events[3];
        int ready = monitor.wait(events, ARRAY_LENGTH(events), -1);
        if (ready < 0) {
            logg.logError("Monitor::wait failed");
            handleException();
        }

        for (int i = 0; i < ready; ++i) {
            if ((socketUds != nullptr) && (events[i].data.fd == socketUds->getFd())) {
                stateAndChildPid = handleClient(stateAndChildPid, drivers, *socketUds, socketTcp.get());
            }
            else if ((socketTcp != nullptr) && (events[i].data.fd == socketTcp->getFd())) {
                stateAndChildPid = handleClient(stateAndChildPid, drivers, *socketTcp, socketUds.get());
            }
            else if (events[i].data.fd == udpListener.getReq()) {
                udpListener.handle();
            }
#ifdef TCP_ANNOTATIONS
            else if (events[i].data.fd == annotateListener.getSockFd()) {
                annotateListener.handleSock();
            }
#endif
            else if (events[i].data.fd == annotateListener.getUdsFd()) {
                annotateListener.handleUds();
            }
            else if (events[i].data.fd == pipefd[0]) {
                uint64_t val;
                if (read(pipefd[0], &val, sizeof(val)) != sizeof(val)) {
                    logg.logMessage("Reading annotate pipe failed");
                }
                annotateListener.signal();
            }
            else if (events[i].data.fd == signalPipe[0]) {
                int signum;
                const int amountRead = ::read(signalPipe[0], &signum, sizeof(signum));
                if (amountRead != sizeof(signum)) {
                    logg.logError("read failed (%d) %s", errno, strerror(errno));
                    handleException();
                }
                stateAndChildPid = handleSignal(stateAndChildPid, drivers, signum);
            }
            else {
                // shouldn't really happen unless we forgot to handle a new item
                logg.logError("Unexpected fd in monitor");
                handleException();
            }
        }
    }

    cleanUp();
    return 0;
}
