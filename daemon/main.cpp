/**
 * Copyright (C) ARM Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <algorithm>

#include <arpa/inet.h>
#include <fcntl.h>
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
#include <unistd.h>
#include <atomic>

#include "AnnotateListener.h"
#include "Child.h"
#include "DriverSource.h"
#include "EventsXML.h"
#include "Logging.h"
#include "Monitor.h"
#include "OlySocket.h"
#include "OlyUtility.h"
#include "PmuXML.h"
#include "SessionData.h"
#include "PrimarySourceProvider.h"
#include "Sender.h"

static int shutdownFilesystem();
static pthread_mutex_t numChildProcesses_mutex;
static OlyServerSocket* sock = NULL;
static Monitor monitor;
static bool driverRunningAtStart = false;
static bool driverMountedAtStart = false;

enum State
{
    IDLE,
    CAPTURING,
    WAITING_FOR_TEAR_DOWN,
};
static std::atomic<State> state(State::IDLE);

struct cmdline_t
{
    char *module;
    char *pmuPath;
    int port;
};

#define DEFAULT_PORT 8080

void cleanUp()
{
    if (shutdownFilesystem() == -1) {
        logg.logMessage("Error shutting down gator filesystem");
    }
    delete sock;
}

// CTRL C Signal Handler
__attribute__((noreturn))
static void handler(int signum)
{
    logg.logMessage("Received signal %d, gator daemon exiting", signum);

    // Case 1: both child and parent receive the signal
    if (state.load() != State::IDLE) {
        // Arbitrary sleep of 1 second to give time for the child to exit;
        // if something bad happens, continue the shutdown process regardless
        sleep(1);
    }

    // Case 2: only the parent received the signal
    if (state.load() != State::IDLE) {
        // Kill child threads - the first signal exits gracefully
        logg.logMessage("Killing process group as more than one gator-child was running when signal was received");
        kill(0, SIGINT);

        // Give time for the child to exit
        sleep(1);

        if (state.load() != State::IDLE) {
            // The second signal force kills the child
            logg.logMessage("Force kill the child");
            kill(0, SIGINT);
            // Again, sleep for 1 second
            sleep(1);

            if (state.load() != State::IDLE) {
                // Something bad has really happened; the child is not exiting and therefore may hold the /dev/gator resource open
                printf("Unable to kill the gatord child process, thus gator.ko may still be loaded.\n");
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
        state.store(State::IDLE);
        logg.logMessage("Child process %d exited with status %d", pid, status);
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

static const char OPTSTRING[] = "hvVdap:s:c:e:E:P:m:o:A:";

static bool hasDebugFlag(int argc, char** argv)
{
    int c;

    optind = 1;
    opterr = 0;
    while ((c = getopt(argc, argv, OPTSTRING)) != -1) {
        if (c == 'd') {
            return true;
        }
    }

    return false;
}

static struct cmdline_t parseCommandLine(int argc, char** argv)
{
    struct cmdline_t cmdline;
    memset(&cmdline, 0, sizeof(cmdline));
    cmdline.port = DEFAULT_PORT;
    char version_string[256]; // arbitrary length to hold the version information
    int c;

    // build the version string
    if (PROTOCOL_VERSION < PROTOCOL_DEV) {
        const int majorVersion = PROTOCOL_VERSION / 100;
        const int minorVersion = (PROTOCOL_VERSION / 10) % 10;
        const int revisionVersion = PROTOCOL_VERSION % 10;
        if (revisionVersion == 0) {
            snprintf(version_string, sizeof(version_string), "Streamline gatord version %d (Streamline v%d.%d)",
                     PROTOCOL_VERSION, majorVersion, minorVersion);
        }
        else {
            snprintf(version_string, sizeof(version_string), "Streamline gatord version %d (Streamline v%d.%d.%d)",
                     PROTOCOL_VERSION, majorVersion, minorVersion, revisionVersion);
        }
    }
    else {
        snprintf(version_string, sizeof(version_string), "Streamline gatord development version %d", PROTOCOL_VERSION);
    }
    logg.logMessage("%s", version_string);

    optind = 1;
    opterr = 1;
    while ((c = getopt(argc, argv, OPTSTRING)) != -1) {
        switch (c) {
        case 'A':
            if (!stringToInt(&gSessionData.mAndroidApiLevel, optarg, 10)) {
                logg.logError("-A must be followed by an int");
                handleException();
            }
            break;
        case 'c':
            gSessionData.mConfigurationXMLPath = optarg;
            break;
        case 'd':
            // Already handled
            break;
        case 'e':
            gSessionData.mEventsXMLPath = optarg;
            break;
        case 'E':
            gSessionData.mEventsXMLAppend = optarg;
            break;
        case 'P':
            cmdline.pmuPath = optarg;
            break;
        case 'm':
            cmdline.module = optarg;
            break;
        case 'p':
            if (!stringToInt(&cmdline.port, optarg, 10)) {
                logg.logError("Port must be an integer");
                handleException();
            }
            if ((cmdline.port == 8082) || (cmdline.port == 8083)) {
                logg.logError("Gator can't use port %i, as it already uses ports 8082 and 8083 for annotations. Please select a different port.", cmdline.port);
                handleException();
            }
            if (cmdline.port < 1 || cmdline.port > 65535) {
                logg.logError("Gator can't use port %i, as it is not valid. Please pick a value between 1 and 65535", cmdline.port);
                handleException();
            }
            break;
        case 's':
            gSessionData.mSessionXMLPath = optarg;
            break;
        case 'o':
            gSessionData.mTargetPath = optarg;
            break;
        case 'a':
            gSessionData.mAllowCommands = true;
            break;
        case 'h':
        case '?':
            logg.logError(
                    "%s. All parameters are optional:\n"
                    "-c config_xml   path and filename of the configuration XML to use\n"
                    "-e events_xml   path and filename of the events XML to use\n"
                    "-E events_xml   path and filename of events XML to append\n"
                    "-P pmu_xml      path and filename of pmu XML to append\n"
                    "-h              this help page\n"
                    "-m module       path and filename of gator.ko\n"
                    "-p port_number  port upon which the server listens; default is 8080\n"
                    "-s session_xml  path and filename of a session.xml used for local capture\n"
                    "-o apc_dir      path and name of the output for a local capture\n"
                    "-v              version information\n"
                    "-d              enable debug messages\n"
                    "-a              allow the user to issue a command from Streamline"
                    , version_string);
            handleException();
            break;
        case 'v':
            logg.logError("%s", version_string);
            handleException();
            break;
        case 'V':
            logg.logError("%s\nSRC_MD5: %s", version_string, gSrcMd5);
            handleException();
            break;
        }
    }

    // Error checking
    if (cmdline.port != DEFAULT_PORT && gSessionData.mSessionXMLPath != NULL) {
        logg.logError("Only a port or a session xml can be specified, not both");
        handleException();
    }

    if (gSessionData.mTargetPath != NULL && gSessionData.mSessionXMLPath == NULL) {
        logg.logError("Missing -s command line option required for a local capture.");
        handleException();
    }

    if (optind < argc) {
        logg.logError("Unknown argument: %s. Use '-h' for help.", argv[optind]);
        handleException();
    }

    return cmdline;
}

static AnnotateListener annotateListener;

static void handleClient()
{
    assert(sock!=nullptr);

    if (state.load() == State::CAPTURING)
    {
            // A temporary socket connection to host, to transfer error message
            OlySocket client (sock->acceptConnection());
            logg.logError("Session already in progress");
            Sender sender(&client);
            sender.writeData(logg.getLastError(), strlen(logg.getLastError()), RESPONSE_ERROR, true);

            // Ensure all data is flushed the host receive the data (not closing socket too quick)
            sleep(1);
            client.shutdownConnection();
            client.closeSocket();
    }
    else
    {
        OlySocket client (sock->acceptConnection());
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

            auto child = Child::createLive(*gSessionData.mPrimarySource, client);
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

static void doLocalCapture()
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

        auto child = Child::createLocal(*gSessionData.mPrimarySource);
        child->run();
        child.reset();

        exit(0);
    }
}

// Gator data flow: collector -> collector fifo -> sender
int main(int argc, char** argv)
{
    // Ensure proper signal handling by making gatord the process group leader
    //   e.g. it may not be the group leader when launched as 'sudo gatord'
    setsid();

    // Set up global thread-safe logging
    logg.setDebug(hasDebugFlag(argc, argv));
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
            rlim.rlim_cur = std::max(rlim_t(1) << 15, rlim.rlim_cur);
            rlim.rlim_max = std::max(rlim.rlim_cur, rlim.rlim_max);
            if (setrlimit(RLIMIT_NOFILE, &rlim) != 0) {
                logg.logMessage("Unable to increase the maximum number of files");
                // Not good, but not a fatal error either
            }
        }
    }

    // Parse the command line parameters
    struct cmdline_t cmdline = parseCommandLine(argc, argv);

    PmuXML::read(cmdline.pmuPath);

    // detect the primary source
    // Call before setting up the SIGCHLD handler, as system() spawns child processes
    gSessionData.mPrimarySource = PrimarySourceProvider::detect(cmdline.module);

    if (gSessionData.mPrimarySource == nullptr) {
        logg.logError(
                "Unable to initialize primary capture source:\n"
                "  >>> gator.ko should be co-located with gatord in the same directory\n"
                "  >>> OR insmod gator.ko prior to launching gatord\n"
                "  >>> OR specify the location of gator.ko on the command line\n"
                "  >>> OR run Linux 3.4 or later with perf (CONFIG_PERF_EVENTS and CONFIG_HW_PERF_EVENTS) and tracing (CONFIG_TRACING and CONFIG_CONTEXT_SWITCH_TRACER) support to collect data via userspace only");
        handleException();
    }

    {
        EventsXML eventsXML;
        mxml_node_t *xml = eventsXML.getTree();
        // Initialize all drivers
        for (Driver *driver = Driver::getHead(); driver != NULL; driver = driver->getNext()) {
            driver->readEvents(xml);
        }
        mxmlDelete(xml);
    }

    // Handle child exit codes
    signal(SIGCHLD, child_exit);

    // Ignore the SIGPIPE signal so that any send to a broken socket will return an error code instead of asserting a signal
    // Handling the error at the send function call is much easier than trying to do anything intelligent in the sig handler
    signal(SIGPIPE, SIG_IGN);

    annotateListener.setup();
    int pipefd[2];
    if (pipe_cloexec(pipefd) != 0) {
        logg.logError("Unable to set up annotate pipe");
        handleException();
    }
    gSessionData.mAnnotateStart = pipefd[1];
    if (!monitor.init() || !monitor.add(annotateListener.getSockFd())
                        || !monitor.add(annotateListener.getUdsFd())
                        || !monitor.add(pipefd[0])) {
        logg.logError("Monitor setup failed");
        handleException();
    }

    // If the command line argument is a session xml file, no need to open a socket
    bool localCapture = gSessionData.mSessionXMLPath != NULL;
    if (localCapture) {
        doLocalCapture();
    }
    else {
        sock = new OlyServerSocket(cmdline.port);
        udpListener.setup(cmdline.port);
        if (!monitor.add(sock->getFd()) || !monitor.add(udpListener.getReq())) {
            logg.logError("Monitor setup failed: couldn't add host listeners");
            handleException();
        }
    }

    // Forever loop, can be exited via a signal or exception
    while (1) {
        struct epoll_event events[2];
        logg.logMessage("Waiting on connection...");
        int ready = monitor.wait(events, ARRAY_LENGTH(events), -1);
        if (ready < 0) {
            logg.logError("Monitor::wait failed");
            handleException();
        }
        for (int i = 0; i < ready; ++i) {
            if (!localCapture && events[i].data.fd == sock->getFd()) {
                handleClient();
            }
            else if (!localCapture && events[i].data.fd == udpListener.getReq()) {
                udpListener.handle();
            }
            else if (events[i].data.fd == annotateListener.getSockFd()) {
                annotateListener.handleSock();
            }
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
