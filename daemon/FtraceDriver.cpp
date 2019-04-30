/**
 * Copyright (C) Arm Limited 2014-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "FtraceDriver.h"

#include <dirent.h>
#include <fcntl.h>
#include <regex.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/FileDescriptor.h"
#include "lib/Utils.h"

#include "linux/perf/IPerfAttrsConsumer.h"

#include "Config.h"
#include "Logging.h"
#include "PrimarySourceProvider.h"
#include "Tracepoints.h"
#include "SessionData.h"


Barrier::Barrier()
        : mMutex(),
          mCond(),
          mCount(0)
{
    pthread_mutex_init(&mMutex, NULL);
    pthread_cond_init(&mCond, NULL);
}

Barrier::~Barrier()
{
    pthread_cond_destroy(&mCond);
    pthread_mutex_destroy(&mMutex);
}

void Barrier::init(unsigned int count)
{
    mCount = count;
}

void Barrier::wait()
{
    pthread_mutex_lock(&mMutex);

    mCount--;
    if (mCount == 0) {
        pthread_cond_broadcast(&mCond);
    }
    else {
        // Loop in case of spurious wakeups
        for (;;) {
            pthread_cond_wait(&mCond, &mMutex);
            if (mCount <= 0) {
                break;
            }
        }
    }

    pthread_mutex_unlock(&mMutex);
}

class FtraceCounter : public DriverCounter
{
public:
    FtraceCounter(DriverCounter *next, const char *name, const char *enable);
    ~FtraceCounter();

    bool readTracepointFormat(const uint64_t currTime, IPerfAttrsConsumer & attrsConsumer);

    void prepare();
    void stop();

private:
    char * const mEnable;
    int mWasEnabled;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(FtraceCounter)
    ;
};

FtraceCounter::FtraceCounter(DriverCounter *next, const char *name, const char *enable)
        : DriverCounter(next, name),
          mEnable(enable == NULL ? NULL : strdup(enable)),
          mWasEnabled(false)
{
}

FtraceCounter::~FtraceCounter()
{
    if (mEnable != NULL) {
        free(mEnable);
    }
}

void FtraceCounter::prepare()
{
    if (mEnable == NULL) {
        if (gSessionData.mFtraceRaw) {
            logg.logError("The ftrace counter %s is not compatible with the more efficient ftrace collection as it is missing the enable attribute. Please either add the enable attribute to the counter in events XML or disable the counter in counter configuration.", getName());
            handleException();
        }
        return;
    }

    char buf[1 << 10];
    snprintf(buf, sizeof(buf), EVENTS_PATH "/%s/enable", mEnable);
    if ((lib::readIntFromFile(buf, mWasEnabled) != 0) || (lib::writeIntToFile(buf, 1) != 0)) {
        logg.logError("Unable to read or write to %s", buf);
        handleException();
    }
}

void FtraceCounter::stop()
{
    if (mEnable == NULL) {
        return;
    }

    char buf[1 << 10];
    snprintf(buf, sizeof(buf), EVENTS_PATH "/%s/enable", mEnable);
    lib::writeIntToFile(buf, mWasEnabled);
}

bool FtraceCounter::readTracepointFormat(const uint64_t currTime, IPerfAttrsConsumer & attrsConsumer)
{
    return ::readTracepointFormat(currTime, attrsConsumer, mEnable);
}

static void handlerUsr1(int signum)
{
    (void) signum;

    // Although this signal handler does nothing, SIG_IGN doesn't interrupt splice in all cases
}

static ssize_t pageSize;

class FtraceReader
{
public:
    FtraceReader(Barrier * const barrier, int cpu, int tfd, int pfd0, int pfd1)
            : mNext(mHead),
              mBarrier(barrier),
              mThread(),
              mCpu(cpu),
              mTfd(tfd),
              mPfd0(pfd0),
              mPfd1(pfd1)
    {
        mHead = this;
    }

    void start();
    bool interrupt();
    bool join();

    static FtraceReader *getHead()
    {
        return mHead;
    }
    FtraceReader *getNext() const
    {
        return mNext;
    }
    int getPfd0() const
    {
        return mPfd0;
    }

private:
    static FtraceReader *mHead;
    FtraceReader * const mNext;
    Barrier * const mBarrier;
    pthread_t mThread;
    const int mCpu;
    const int mTfd;
    const int mPfd0;
    const int mPfd1;

    static void *runStatic(void *arg);
    void run();
};

FtraceReader *FtraceReader::mHead;

void FtraceReader::start()
{
    if (pthread_create(&mThread, NULL, runStatic, this) != 0) {
        logg.logError("Unable to start the ftraceReader thread");
        handleException();
    }
}

bool FtraceReader::interrupt()
{
    return pthread_kill(mThread, SIGUSR1) == 0;
}

bool FtraceReader::join()
{
    return pthread_join(mThread, NULL) == 0;
}

void *FtraceReader::runStatic(void *arg)
{
    FtraceReader * const ftraceReader = static_cast<FtraceReader *>(arg);
    ftraceReader->run();
    return NULL;
}

#ifndef SPLICE_F_MOVE

#include <sys/syscall.h>

// Pre Android-21 does not define splice
#define SPLICE_F_MOVE 1

static ssize_t sys_splice(int fd_in, loff_t *off_in, int fd_out, loff_t *off_out, size_t len, unsigned int flags) {
    return syscall(__NR_splice, fd_in, off_in, fd_out, off_out, len, flags);
}

#define splice(fd_in, off_in, fd_out, off_out, len, flags) sys_splice(fd_in, off_in, fd_out, off_out, len, flags)

#endif

void FtraceReader::run()
{
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "gatord-reader%02i", mCpu);
        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&buf), 0, 0, 0);
    }

    // Gator runs at a high priority, reset the priority to the default
    if (setpriority(PRIO_PROCESS, syscall(__NR_gettid), 0) == -1) {
        logg.logError("setpriority failed");
        handleException();
    }

    mBarrier->wait();

    while (gSessionData.mSessionIsActive) {
        const ssize_t bytes = splice(mTfd, NULL, mPfd1, NULL, pageSize, SPLICE_F_MOVE);
        if (bytes == 0) {
            logg.logError("ftrace splice unexpectedly returned 0");
            handleException();
        }
        else if (bytes < 0) {
            if (errno != EINTR) {
                logg.logError("splice failed");
                handleException();
            }
        }
        else {
            // Can there be a short splice read?
            if (bytes != pageSize) {
                logg.logError("splice short read");
                handleException();
            }
            // Will be read by gatord-external
        }
    }

    if (!lib::setNonblock(mTfd)) {
        logg.logError("lib::setNonblock failed");
        handleException();
    }

    for (;;) {
        ssize_t bytes;

        bytes = splice(mTfd, NULL, mPfd1, NULL, pageSize, SPLICE_F_MOVE);
        if (bytes <= 0) {
            break;
        }
        else {
            // Can there be a short splice read?
            if (bytes != pageSize) {
                logg.logError("splice short read");
                handleException();
            }
            // Will be read by gatord-external
        }
    }

    {
        // Read any slop
        ssize_t bytes;
        size_t size;
        char buf[1 << 16];

        if (sizeof(buf) < static_cast<size_t>(pageSize)) {
            logg.logError("ftrace slop buffer is too small");
            handleException();
        }
        for (;;) {
            bytes = read(mTfd, buf, sizeof(buf));
            if (bytes == 0) {
                logg.logError("ftrace read unexpectedly returned 0");
                handleException();
            }
            else if (bytes < 0) {
                if (errno != EAGAIN) {
                    logg.logError("reading slop from ftrace failed");
                    handleException();
                }
                break;
            }
            else {
                size = bytes;
                bytes = write(mPfd1, buf, size);
                if (bytes != static_cast<ssize_t>(size)) {
                    logg.logError("writing slop to ftrace pipe failed");
                    handleException();
                }
            }
        }
    }

    close(mTfd);
    close(mPfd1);
    // Intentionally don't close mPfd0 as it is used after this thread is exited to read the slop
}

FtraceDriver::FtraceDriver(bool useForTracepoints, size_t numberOfCores)
        : SimpleDriver("Ftrace"),
          mValues(NULL),
          mBarrier(),
          mTracingOn(0),
          mSupported(false),
          mMonotonicRawSupport(false),
          mUseForTracepoints(useForTracepoints),
          mNumberOfCores(numberOfCores)
{
}

FtraceDriver::~FtraceDriver()
{
    delete[] mValues;
}

void FtraceDriver::readEvents(mxml_node_t * const xml)
{
    // Check the kernel version
    struct utsname utsname;
    if (uname(&utsname) != 0) {
        logg.logError("uname failed");
        handleException();
    }

    // The perf clock was added in 3.10
    const int kernelVersion = lib::parseLinuxVersion(utsname);
    if (kernelVersion < KERNEL_VERSION(3, 10, 0)) {
        mSupported = false;
        logg.logSetup("Ftrace is disabled\nFor full ftrace functionality please upgrade to Linux 3.10 or later. With user space gator and Linux prior to 3.10, ftrace counters with the tracepoint and arg attributes will be available.");
        return;
    }
    mMonotonicRawSupport = kernelVersion >= KERNEL_VERSION(4, 2, 0);

    // Is debugfs or tracefs available?
    if (access(TRACING_PATH, R_OK) != 0) {
        mSupported = false;
        logg.logSetup("Ftrace is disabled\nUnable to locate the tracing directory");
        return;
    }

    if (geteuid() != 0) {
        mSupported = false;
        logg.logSetup("Ftrace is disabled\nFtrace is not supported when running non-root");
        return;
    }

    mSupported = true;

    mxml_node_t *node = xml;
    int count = 0;
    while (true) {
        node = mxmlFindElement(node, xml, "event", NULL, NULL, MXML_DESCEND);
        if (node == NULL) {
            break;
        }
        const char *counter = mxmlElementGetAttr(node, "counter");
        if (counter == NULL) {
            continue;
        }

        if (strncmp(counter, "ftrace_", 7) != 0) {
            continue;
        }

        const char *regex = mxmlElementGetAttr(node, "regex");
        if (regex == NULL) {
            logg.logError("The regex counter %s is missing the required regex attribute", counter);
            handleException();
        }

        const char *tracepoint = mxmlElementGetAttr(node, "tracepoint");
        const char *enable = mxmlElementGetAttr(node, "enable");
        if (enable == NULL) {
            enable = tracepoint;
        }
        if (!mUseForTracepoints && tracepoint != NULL) {
            logg.logMessage("Not using ftrace for counter %s", counter);
            continue;
        }
        if (enable != NULL) {
            char buf[1 << 10];
            snprintf(buf, sizeof(buf), EVENTS_PATH "/%s/enable", enable);
            if (access(buf, W_OK) != 0) {
                logg.logSetup("%s is disabled\n%s was not found", counter, buf);
                continue;
            }
        }

        logg.logMessage("Using ftrace for %s", counter);
        setCounters(new FtraceCounter(getCounters(), counter, enable));
        ++count;
    }

    mValues = new int64_t[2 * count];
}

std::pair<std::vector<int>, bool> FtraceDriver::prepare()
{
    if (gSessionData.mFtraceRaw) {
        // Don't want the performace impact of sending all formats so gator only sends it for the enabled counters. This means other counters need to be disabled
        if (lib::writeCStringToFile(TRACING_PATH "/events/enable", "0") != 0) {
            logg.logError("Unable to turn off all events");
            handleException();
        }
    }

    for (FtraceCounter *counter = static_cast<FtraceCounter *>(getCounters()); counter != NULL;
            counter = static_cast<FtraceCounter *>(counter->getNext())) {
        if (!counter->isEnabled()) {
            continue;
        }
        counter->prepare();
    }

    if (lib::readIntFromFile(TRACING_PATH "/tracing_on", mTracingOn)) {
        logg.logError("Unable to read if ftrace is enabled");
        handleException();
    }

    if (lib::writeCStringToFile(TRACING_PATH "/tracing_on", "0") != 0) {
        logg.logError("Unable to turn ftrace off before truncating the buffer");
        handleException();
    }

    {
        int fd;
        // The below call can be slow on loaded high-core count systems.
        fd = open(TRACING_PATH "/trace", O_WRONLY | O_TRUNC | O_CLOEXEC, 0666);
        if (fd < 0) {
            logg.logError("Unable truncate ftrace buffer: %s", strerror(errno));
            handleException();
        }
        close(fd);
    }

    const char * const trace_clock_path = TRACING_PATH "/trace_clock";
    const char * const clock = mMonotonicRawSupport ? "mono_raw" : "perf";
    const char * const clock_selected = mMonotonicRawSupport ? "[mono_raw]" : "[perf]";
    const size_t max_trace_clock_file_length = 200;
    ssize_t trace_clock_file_length;
    char trace_clock_file_content[max_trace_clock_file_length + 1] = { 0 };
    bool must_switch_clock = true;
    // Only write to /trace_clock if the clock actually needs changing,
    // as changing trace_clock can be extremely expensive, especially on large
    // core count systems. The idea is that hopefully only on the first
    // capture, the trace clock needs to be changed. On subsequent captures,
    // the right clock is already being used.
    int fd = open(trace_clock_path, O_RDONLY);
    if (fd < 0) {
        logg.logError("Couldn't open %s", trace_clock_path);
        handleException();
    }
    if ((trace_clock_file_length = ::read(fd, trace_clock_file_content, max_trace_clock_file_length - 1)) < 0) {
        logg.logError("Couldn't read from %s", trace_clock_path);
        close(fd);
        handleException();
    }
    close(fd);
    trace_clock_file_content[trace_clock_file_length] = 0;
    if (::strstr(trace_clock_file_content, clock_selected)) {
        // the right clock was already selected :)
        must_switch_clock = false;
    }

    // Writing to trace_clock can be very slow on loaded high core count
    // systems.
    if (must_switch_clock && lib::writeCStringToFile(TRACING_PATH "/trace_clock", clock) != 0) {
        logg.logError("Unable to switch ftrace to the %s clock, please ensure you are running Linux %s or later", clock, mMonotonicRawSupport ? "4.2" : "3.10");
        handleException();
    }

    if (!gSessionData.mFtraceRaw) {
        const int fd = open(TRACING_PATH "/trace_pipe", O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            logg.logError("Unable to open trace_pipe");
            handleException();
        }
        return {{fd},true};
    }

    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = handlerUsr1;
    if (sigaction(SIGUSR1, &act, NULL) != 0) {
        logg.logError("sigaction failed");
        handleException();
    }

    pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) {
        logg.logError("sysconf PAGESIZE failed");
        handleException();
    }

    mBarrier.init(mNumberOfCores + 1);

    std::pair<std::vector<int>, bool> result {{}, false};
    for (size_t cpu = 0; cpu < mNumberOfCores; ++cpu) {
        int pfd[2];
        if (pipe2(pfd, O_CLOEXEC) != 0) {
            logg.logError("pipe2 failed, %s (%i)", strerror(errno), errno);
            handleException();
        }

        char buf[64];
        snprintf(buf, sizeof(buf), TRACING_PATH "/per_cpu/cpu%zu/trace_pipe_raw", cpu);
        const int tfd = open(buf, O_RDONLY | O_CLOEXEC);
        (new FtraceReader(&mBarrier, cpu, tfd, pfd[0], pfd[1]))->start();
        result.first.push_back(pfd[0]);
    }

    return result;
}

void FtraceDriver::start()
{
    if (lib::writeCStringToFile(TRACING_PATH "/tracing_on", "1") != 0) {
        logg.logError("Unable to turn ftrace on");
        handleException();
    }

    if (gSessionData.mFtraceRaw) {
        mBarrier.wait();
    }
}

std::vector<int> FtraceDriver::stop()
{
    lib::writeIntToFile(TRACING_PATH "/tracing_on", mTracingOn);

    for (FtraceCounter *counter = static_cast<FtraceCounter *>(getCounters()); counter != NULL;
            counter = static_cast<FtraceCounter *>(counter->getNext())) {
        if (!counter->isEnabled()) {
            continue;
        }
        counter->stop();
    }

    std::vector<int> fds;
    if (gSessionData.mFtraceRaw) {
        for (FtraceReader *reader = FtraceReader::getHead(); reader != NULL; reader = reader->getNext()) {
            reader->interrupt();
            fds.push_back(reader->getPfd0());
        }
        for (FtraceReader *reader = FtraceReader::getHead(); reader != NULL; reader = reader->getNext()) {
            reader->join();
        }
    }
    return fds;
}

bool FtraceDriver::readTracepointFormats(const uint64_t currTime, IPerfAttrsConsumer & attrsConsumer, DynBuf * const printb,
                                         DynBuf * const b)
{
    if (!gSessionData.mFtraceRaw) {
        return true;
    }

    if (!printb->printf(EVENTS_PATH "/header_page")) {
        logg.logMessage("DynBuf::printf failed");
        return false;
    }
    if (!b->read(printb->getBuf())) {
        logg.logMessage("DynBuf::read failed");
        return false;
    }
    attrsConsumer.marshalHeaderPage(currTime, b->getBuf());

    if (!printb->printf(EVENTS_PATH "/header_event")) {
        logg.logMessage("DynBuf::printf failed");
        return false;
    }
    if (!b->read(printb->getBuf())) {
        logg.logMessage("DynBuf::read failed");
        return false;
    }
    attrsConsumer.marshalHeaderEvent(currTime, b->getBuf());

    DIR *dir = opendir(EVENTS_PATH "/ftrace");
    if (dir == NULL) {
        logg.logError("Unable to open events ftrace folder");
        handleException();
    }
    struct dirent *dirent;
    while ((dirent = readdir(dir)) != NULL) {
        if (dirent->d_name[0] == '.' || dirent->d_type != DT_DIR) {
            continue;
        }
        if (!printb->printf(EVENTS_PATH "/ftrace/%s/format", dirent->d_name)) {
            logg.logMessage("DynBuf::printf failed");
            return false;
        }
        if (!b->read(printb->getBuf())) {
            logg.logMessage("DynBuf::read failed");
            return false;
        }
        attrsConsumer.marshalFormat(currTime, b->getLength(), b->getBuf());
    }
    closedir(dir);

    for (FtraceCounter *counter = static_cast<FtraceCounter *>(getCounters()); counter != NULL;
            counter = static_cast<FtraceCounter *>(counter->getNext())) {
        if (!counter->isEnabled()) {
            continue;
        }
        counter->readTracepointFormat(currTime, attrsConsumer);
    }

    return true;
}
