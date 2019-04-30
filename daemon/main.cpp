/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <algorithm>
#include <cinttypes>

#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <atomic>
#include <map>
#include <vector>
#include <iterator>
#include <iostream>

#include "AnnotateListener.h"
#include "Child.h"
#include "CounterXML.h"
#include "ConfigurationXML.h"
#include "Drivers.h"
#include "EventsXML.h"
#include "ICpuInfo.h"
#include "Logging.h"
#include "Monitor.h"
#include "OlySocket.h"
#include "OlyUtility.h"
#include "PmuXMLParser.h"
#include "SessionData.h"
#include "Sender.h"
#include "GatorCLIParser.h"
#include "lib/FileDescriptor.h"
#include "lib/Memory.h"
#include "lib/Utils.h"

static int shutdownFilesystem();
static pthread_mutex_t numChildProcesses_mutex;
static OlyServerSocket* sock = NULL;
static Monitor monitor;
static bool driverRunningAtStart = false;
static bool driverMountedAtStart = false;

static const char NO_TCP_PIPE[] = "\0streamline-data";

enum State
{
    IDLE,
    CAPTURING,
    WAITING_FOR_TEAR_DOWN,
};

static std::atomic<State> state(State::IDLE);
static std::atomic_int localCapturePid = ATOMIC_VAR_INIT(0);

void cleanUp()
{
    if (shutdownFilesystem() == -1) {
        logg.logMessage("Error shutting down gator filesystem");
    }
    delete sock;
}

// Arbitrary sleep of up to 2 seconds to give time for the child to exit;
// if something bad happens, continue the shutdown process regardless
static bool poll_state()
{
    // sleep 10ms, 200 times
    for (int n = 0; n < 200; ++n) {
        // check exited
        if (state.load() == State::IDLE) {
            return true;
        }

        // sleep for 10ms
        usleep(10000);
    }

    return (state.load() == State::IDLE);
}

// CTRL C Signal Handler
__attribute__((noreturn))
static void handler(int signum)
{
    logg.logMessage("Received signal %d, gator daemon exiting", signum);

    // Case 1: both child and parent receive the signal, in which has poll_state will return true
    if (!poll_state())
    {
        // Case 2: only the parent received the signal - Kill child threads - the first signal exits gracefully
        logg.logMessage("Killing process group as more than one gator-child was running when signal was received");
        kill(0, SIGINT);

        // Give time for the child to exit
        if (!poll_state())
        {
            // The second signal should kill the child
            logg.logMessage("Sending SIGINT again");
            kill(0, SIGINT);

            // Again, wait for status update
            if (!poll_state())
            {
                // try to cleanup
                cleanUp();

                // now get terminal (this will also bring down gatord)
                logg.logError("Failed to shut down child. Possible deadlock. Sending SIGKILL to kill everything");
                kill(0, SIGKILL);
            }
        }
    }

    cleanUp();
    exit(0);
}

static void liveCaptureStoppedSigHandler(int)
{
    state.store(State::WAITING_FOR_TEAR_DOWN);
}

// Child exit Signal Handler
static void child_exit(int)
{
    int status;
    int pid = wait(&status);
    if (pid != -1) {
        logg.logMessage("Child process %d exited with status %d", pid, status);
        if (pid == localCapturePid.load()) {
            cleanUp();
            exit(0);
        }
        state.store(State::IDLE);
    }
}

static const int UDP_REQ_PORT = 30001;

typedef struct
{
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
} RVIConfigureInfo;

static const char DST_REQ[] = { 'D', 'S', 'T', '_', 'R', 'E', 'Q', ' ', 0, 0, 0, 0x64 };

class UdpListener
{
public:
    UdpListener()
            : mDstAns(),
              mReq(-1)
    {
    }

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

    int getReq() const
    {
        return mReq;
    }

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

    void close()
    {
        ::close(mReq);
    }

private:
    int udpPort(int port)
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

// retval: -1 = failure; 0 = was already mounted; 1 = successfully mounted
static int mountGatorFS()
{
    // If already mounted,
    if (access("/dev/gator/buffer", F_OK) == 0) {
        return 0;
    }

    // else, mount the filesystem
    mkdir("/dev/gator", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (mount("nodev", "/dev/gator", "gatorfs", 0, NULL) != 0) {
        return -1;
    }
    else {
        return 1;
    }
}

static bool init_module(const char * const location)
{
    bool ret(false);
    const int fd = open(location, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        struct stat st;
        if (fstat(fd, &st) == 0) {
            void * const p = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (p != MAP_FAILED) {
                if (syscall(__NR_init_module, p, static_cast<unsigned long>(st.st_size), "") == 0) {
                    ret = true;
                }
                munmap(p, st.st_size);
            }
        }
        close(fd);
    }

    return ret;
}

bool setupFilesystem(const char * module)
{
    if (module) {
        // unmount and rmmod if the module was specified on the commandline, i.e. ensure that the specified module is indeed running
        shutdownFilesystem();

        // if still mounted
        if (access("/dev/gator/buffer", F_OK) == 0) {
            logg.logError("Unable to remove the running gator.ko. Manually remove the module or use the running module by not specifying one on the commandline");
            handleException();
        }
    }

    const int retval = mountGatorFS();
    if (retval == 1) {
        logg.logMessage("Driver already running at startup");
        driverRunningAtStart = true;
    }
    else if (retval == 0) {
        logg.logMessage("Driver already mounted at startup");
        driverRunningAtStart = driverMountedAtStart = true;
    }
    else {
        char location[256]; // arbitrarily large amount

        if (module) {
            strncpy(location, module, sizeof(location));
        }
        else {
            // Is the driver co-located in the same directory?
            if (getApplicationFullPath(location, sizeof(location)) != 0) { // allow some buffer space
                logg.logMessage("Unable to determine the full path of gatord, the cwd will be used");
            }
            strncat(location, "gator.ko", sizeof(location) - strlen(location) - 1);
        }

        if (access(location, F_OK) == -1) {
            if (module == NULL) {
                // The gator kernel is not already loaded and unable to locate gator.ko in the default location
                return false;
            }
            else {
                // gator location specified on the command line but it was not found
                logg.logError("gator module not found at %s", location);
                handleException();
            }
        }

        // Load driver
        if (!init_module(location)) {
            logg.logMessage("Unable to load gator.ko driver from location %s", location);
            logg.logError("Unable to load (insmod) gator.ko driver:\n  >>> gator.ko must be built against the current kernel version & configuration\n  >>> See dmesg for more details");
            handleException();
        }

        if (mountGatorFS() == -1) {
            logg.logError("Unable to mount the gator filesystem needed for profiling.");
            handleException();
        }
    }

    return true;
}

static int shutdownFilesystem()
{
    if (driverMountedAtStart == false) {
        umount("/dev/gator");
    }
    if (driverRunningAtStart == false) {
        if (syscall(__NR_delete_module, "gator", O_NONBLOCK) != 0) {
            return -1;
        }
    }

    return 0; // success
}

static AnnotateListener annotateListener;

static void handleClient(Drivers & drivers)
{
    assert(sock != nullptr);

    if (state.load() == State::CAPTURING) {
        // A temporary socket connection to host, to transfer error message
        OlySocket client(sock->acceptConnection());
        logg.logError("Session already in progress");
        Sender sender(&client);
        sender.writeData(logg.getLastError(), strlen(logg.getLastError()), ResponseType::ERROR, true);

        // Ensure all data is flushed the host receive the data (not closing socket too quick)
        sleep(1);
        client.shutdownConnection();
        client.closeSocket();
    }
    else {
        OlySocket client(sock->acceptConnection());
        int pid = fork();
        if (pid < 0) {
            // Error
            logg.logError("Fork process failed with errno: %d.", errno);
            handleException();
        }
        else if (pid == 0) {
            // Child
            sock->closeServerSocket();
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
            client.closeSocket();
            state.store(State::CAPTURING);
        }
    }
}

static void doLocalCapture(Drivers & drivers, const Child::Config & config)
{
    state.store(State::CAPTURING);
    int pid = fork();
    if (pid < 0) {
        // Error
        logg.logError("Fork process failed with errno: %d.", errno);
        handleException();
    }
    else if (pid == 0) {
        // Child
        monitor.close();
        annotateListener.close();

        auto child = Child::createLocal(drivers, config);
        child->run();
        child.reset();

        exit(0);
    }
    else {
        localCapturePid.store(pid);
    }
}

void setDefaults() {
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
    gSessionData.mCaptureWorkingDir= nullptr;
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

    // use value from perf_event_mlock_kb
    if ((gSessionData.mPerfMmapSizeInPages <= 0) && (geteuid() != 0) && (gSessionData.mPageSize >= 1024)) {
        std::int64_t perfEventMlockKb = 0;
        if (lib::readInt64FromFile("/proc/sys/kernel/perf_event_mlock_kb", perfEventMlockKb) == 0) {
            if (perfEventMlockKb > 0) {
                const std::uint64_t perfEventMlockPages = (perfEventMlockKb / (gSessionData.mPageSize / 1024));
                gSessionData.mPerfMmapSizeInPages = int(std::min<std::uint64_t>(perfEventMlockPages - 1, INT_MAX));
                logg.logMessage("Default perf mmap size set to %d pages (%llukb)",
                                gSessionData.mPerfMmapSizeInPages,
                                gSessionData.mPerfMmapSizeInPages * gSessionData.mPageSize / 1024ull);
            }
        }
        else {
            // the default seen on most setups is 516kb, if user cannot read the file it is probably
            // because they are on Android in locked down setup so use default value of 128 pages
            gSessionData.mPerfMmapSizeInPages = 128;
            logg.logMessage("Default perf mmap size set to %d pages (%llukb)",
                            gSessionData.mPerfMmapSizeInPages,
                            gSessionData.mPerfMmapSizeInPages * gSessionData.mPageSize / 1024ull);
        }
    }
    //These values are set from command line and are alos part of session.xml
    //and hence cannot be modified during parse session
    if (result.parameterSetFlag & USE_CMDLINE_ARG_SAMPLE_RATE) {
        gSessionData.mSampleRate = result.mSampleRate;
    }
    if (result.parameterSetFlag & USE_CMDLINE_ARG_CALL_STACK_UNWINDING) {
        gSessionData.mBacktraceDepth = result.mBacktraceDepth;
    }
    if (result.parameterSetFlag & USE_CMDLINE_ARG_CAPTURE_WORKING_DIR) {
        gSessionData.mCaptureWorkingDir = strdup(result.mCaptureWorkingDir);
    }
    if(result.parameterSetFlag & USE_CMDLINE_ARG_CAPTURE_COMMAND) {
        gSessionData.mCaptureCommand  = result.mCaptureCommand;
    }
    if (result.parameterSetFlag & USE_CMDLINE_ARG_DURATION) {
        gSessionData.mDuration = result.mDuration;
    }
    if (result.parameterSetFlag & USE_CMDLINE_ARG_FTRACE_RAW) {
        gSessionData.mFtraceRaw = result.mFtraceRaw;
    }
}
// Gator data flow: collector -> collector fifo -> sender
int main(int argc, char** argv)
{
    // Ensure proper signal handling by making gatord the process group leader
    //   e.g. it may not be the group leader when launched as 'sudo gatord'
    setsid();

    GatorCLIParser parser;
    // Set up global thread-safe logging
    logg.setDebug(parser.hasDebugFlag(argc, argv));
    gSessionData.initialize();

    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-main"), 0, 0, 0);
    pthread_mutex_init(&numChildProcesses_mutex, NULL);

    signal(SIGINT, handler);
    signal(SIGTERM, handler);
    signal(SIGABRT, handler);

    // handle custom capture stop signal using dedicated handler
    signal(Child::SIG_LIVE_CAPTURE_STOPPED, liveCaptureStoppedSigHandler);

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
                logg.logMessage("Unable to increase the maximum number of files (%" PRIuMAX ", %" PRIuMAX ")", static_cast<uintmax_t>(rlim.rlim_cur), static_cast<uintmax_t>(rlim.rlim_max));
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
            snprintf(versionString, sizeof(versionString), "Streamline gatord version %d (Streamline v%d.%d)",
            PROTOCOL_VERSION,
                     majorVersion, minorVersion);
        }
        else {
            snprintf(versionString, sizeof(versionString), "Streamline gatord version %d (Streamline v%d.%d.%d)",
            PROTOCOL_VERSION,
                     majorVersion, minorVersion, revisionVersion);
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

    PmuXML pmuXml = readPmuXml(result.pmuPath);
    // detect the primary source
    // Call before setting up the SIGCHLD handler, as system() spawns child processes


Drivers drivers { result.module, result.mSystemWide, std::move(pmuXml), result.mMaliTypes, result.mMaliDevices };

    {
        auto xml = events_xml::getTree(drivers.getPrimarySourceProvider().getCpuInfo().getClusters());
        // Initialize all drivers
        for (Driver *driver : drivers.getAll()) {
            driver->readEvents(xml.get());
        }
        // build map of counter->event
        {
            gSessionData.globalCounterToEventMap.clear();

            mxml_node_t *node = xml.get();
            while (true) {
                node = mxmlFindElement(node, xml.get(), "event", NULL, NULL, MXML_DESCEND);
                if (node == nullptr) {
                    break;
                }
                const char * counter = mxmlElementGetAttr(node, "counter");
                const char * event = mxmlElementGetAttr(node, "event");
                if (counter == nullptr) {
                    continue;
                }

                if (event != nullptr) {
                    const int eventNo = (int) strtol(event, nullptr, 0);
                    gSessionData.globalCounterToEventMap[counter] = eventNo;
                }
                else {
                    gSessionData.globalCounterToEventMap[counter] = -1;
                }
            }
        }
    }

    if (result.mode == ParserResult::ExecutionMode::PRINT) {
        if (result.printables.count(ParserResult::Printable::EVENTS_XML) == 1) {
            std::cout << events_xml::getXML(drivers.getAllConst(), drivers.getPrimarySourceProvider().getCpuInfo().getClusters()).get();
        }
        if (result.printables.count(ParserResult::Printable::COUNTERS_XML) == 1) {
            std::cout << counters_xml::getXML(drivers.getAllConst(), drivers.getPrimarySourceProvider().getCpuInfo()).get();
        }
        if (result.printables.count(ParserResult::Printable::DEFAULT_CONFIGURATION_XML) == 1) {
            std::cout << configuration_xml::getDefaultConfigurationXml(drivers.getPrimarySourceProvider().getCpuInfo().getClusters()).get();
        }
        cleanUp();
        return 0;
    }

    // Handle child exit codes
    signal(SIGCHLD, child_exit);

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
            || !monitor.add(annotateListener.getUdsFd())
            || !monitor.add(pipefd[0])) {
        logg.logError("Monitor setup failed");
        handleException();
    }

    // If the command line argument is a session xml file, no need to open a socket
    if (gSessionData.mLocalCapture) {
        Child::Config childConfig { { }, { } };
        for (const auto & event : result.events) {
            CounterConfiguration config;
            config.counterName = event.first;
            config.event = event.second;
            childConfig.events.insert(std::move(config));
        }
        for (const auto & spe : result.mSpeConfigs) {
            childConfig.spes.insert(spe);
        }
        doLocalCapture(drivers, childConfig);
    }
    else {
        if (result.port == DISABLE_TCP_USE_UDS_PORT) {
            sock = new OlyServerSocket(NO_TCP_PIPE, sizeof(NO_TCP_PIPE), true);
            if (!monitor.add(sock->getFd())) {
                logg.logError("Monitor setup failed: couldn't add host listeners");
                handleException();
            }
        }
        else {
            sock = new OlyServerSocket(result.port);
            udpListener.setup(result.port);
            if (!monitor.add(sock->getFd()) || !monitor.add(udpListener.getReq())) {
                logg.logError("Monitor setup failed: couldn't add host listeners");
                handleException();
            }
        }
    }

    // Forever loop, can be exited via a signal or exception
    while (1) {
        struct epoll_event events[2];
        int ready = monitor.wait(events, ARRAY_LENGTH(events), -1);
        if (ready < 0) {
            logg.logError("Monitor::wait failed");
            handleException();
        }
        for (int i = 0; i < ready; ++i) {
            if (!gSessionData.mLocalCapture && events[i].data.fd == sock->getFd()) {
                handleClient(drivers);
            }
            else if (!gSessionData.mLocalCapture && events[i].data.fd == udpListener.getReq()) {
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
        }
    }

    cleanUp();
    return 0;
}
