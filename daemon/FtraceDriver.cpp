/* Copyright (C) 2014-2023 by Arm Limited. All rights reserved. */

#include "FtraceDriver.h"

#include "Config.h"
#include "DynBuf.h"
#include "Logging.h"
#include "PrimarySourceProvider.h"
#include "SessionData.h"
#include "lib/FileDescriptor.h"
#include "lib/String.h"
#include "lib/Syscall.h"
#include "lib/Utils.h"
#include "linux/Tracepoints.h"
#include "linux/perf/IPerfAttrsConsumer.h"

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

#include <dirent.h>
#include <fcntl.h>
#include <regex.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

using namespace std::chrono_literals;

static constexpr auto FTRACE_SLOP_READ_TIMEOUT_DURATION = 2s;

Barrier::Barrier() : mMutex(), mCond(), mCount(0)
{
    pthread_mutex_init(&mMutex, nullptr);
    pthread_cond_init(&mCond, nullptr);
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

namespace {
    // arbitrary large buffer size for printf_str_t bufffers containing tracefs paths
    constexpr std::size_t tracefs_path_buffer_size = 2048;

    class FtraceCounter : public DriverCounter {
    public:
        FtraceCounter(DriverCounter * next,
                      const TraceFsConstants & traceFsConstants,
                      const char * name,
                      const char * enable);
        ~FtraceCounter() override;

        // Intentionally unimplemented
        FtraceCounter(const FtraceCounter &) = delete;
        FtraceCounter & operator=(const FtraceCounter &) = delete;
        FtraceCounter(FtraceCounter &&) = delete;
        FtraceCounter & operator=(FtraceCounter &&) = delete;

        bool readTracepointFormat(IPerfAttrsConsumer & attrsConsumer);

        virtual void prepare();
        virtual void stop();

        virtual void readInitial(size_t /*cpu*/, std::function<void(int, int, std::int64_t)> const & /*consumer*/) {}

    private:
        const TraceFsConstants & traceFsConstants;
        char * const mEnable;
        int mWasEnabled {};
    };

    class CpuFrequencyFtraceCounter : public FtraceCounter {
    public:
        CpuFrequencyFtraceCounter(DriverCounter * next,
                                  const TraceFsConstants & traceFsConstants,
                                  const char * name,
                                  const char * enable,
                                  bool use_cpuinfo)
            : FtraceCounter(next, traceFsConstants, name, enable), use_cpuinfo(use_cpuinfo)
        {
        }

        void readInitial(size_t cpu, std::function<void(int, int, std::int64_t)> const & consumer) override
        {
            constexpr std::size_t buffer_size = 128;
            constexpr std::int64_t freq_multiplier = 1000;

            char const * const pattern = (use_cpuinfo ? "/sys/devices/system/cpu/cpu%zu/cpufreq/cpuinfo_cur_freq"
                                                      : "/sys/devices/system/cpu/cpu%zu/cpufreq/scaling_cur_freq");

            lib::printf_str_t<buffer_size> buf {pattern, cpu};
            std::int64_t freq;
            if (lib::readInt64FromFile(buf, freq) != 0) {
                freq = 0;
            }
            consumer(getKey(), int(cpu), freq_multiplier * freq);
        }

    private:
        bool use_cpuinfo;
    };

    FtraceCounter::FtraceCounter(DriverCounter * next,
                                 const TraceFsConstants & traceFsConstants,
                                 const char * name,
                                 const char * enable)
        : DriverCounter(next, name),
          traceFsConstants(traceFsConstants),
          mEnable(enable == nullptr ? nullptr : strdup(enable))
    {
    }

    FtraceCounter::~FtraceCounter()
    {
        if (mEnable != nullptr) {
            free(mEnable);
        }
    }

    void FtraceCounter::prepare()
    {
        if (mEnable == nullptr) {
            if (gSessionData.mFtraceRaw) {
                LOG_ERROR("The ftrace counter %s is not compatible with the more efficient ftrace collection as it is "
                          "missing the enable attribute. Please either add the enable attribute to the counter in "
                          "events XML or disable the counter in counter configuration.",
                          getName());
                handleException();
            }
            return;
        }

        lib::printf_str_t<tracefs_path_buffer_size> buf {"%s/%s/enable", traceFsConstants.path__events, mEnable};
        if ((lib::readIntFromFile(buf, mWasEnabled) != 0) || (lib::writeIntToFile(buf, 1) != 0)) {
            LOG_ERROR("Unable to read or write to %s", buf.c_str());
            handleException();
        }
    }

    void FtraceCounter::stop()
    {
        if (mEnable == nullptr) {
            return;
        }

        lib::printf_str_t<tracefs_path_buffer_size> buf {"%s/%s/enable", traceFsConstants.path__events, mEnable};
        lib::writeIntToFile(buf, mWasEnabled);
    }

    bool FtraceCounter::readTracepointFormat(IPerfAttrsConsumer & attrsConsumer)
    {
        return ::readTracepointFormat(attrsConsumer, traceFsConstants.path__events, mEnable);
    }

    void handlerUsr1(int signum)
    {
        (void) signum;

        // Although this signal handler does nothing, SIG_IGN doesn't interrupt splice in all cases
    }

    class FtraceReader {
    public:
        //NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        FtraceReader(Barrier * const barrier, int cpu, int tfd, int pfd0, int pfd1, ssize_t pageSize)
            : mNext(mHead), mBarrier(barrier), mCpu(cpu), mTfd(tfd), mPfd0(pfd0), mPfd1(pfd1), pageSize(pageSize)
        {
            mHead = this;
        }

        void start();
        bool interrupt();
        [[nodiscard]] bool join() const;

        static FtraceReader * getHead() { return mHead; }
        [[nodiscard]] FtraceReader * getNext() const { return mNext; }
        [[nodiscard]] int getPfd0() const { return mPfd0; }

    private:
        static FtraceReader * mHead;
        FtraceReader * const mNext;
        Barrier * const mBarrier;
        pthread_t mThread {};
        const int mCpu;
        const int mTfd;
        const int mPfd0;
        const int mPfd1;
        std::atomic_bool mSessionIsActive {true};
        ssize_t pageSize;

        static void * runStatic(void * arg);
        void run();
    };

    FtraceReader * FtraceReader::mHead;

    void FtraceReader::start()
    {
        if (pthread_create(&mThread, nullptr, runStatic, this) != 0) {
            LOG_ERROR("Unable to start the ftraceReader thread");
            handleException();
        }
    }

    bool FtraceReader::interrupt()
    {
        mSessionIsActive = false;
        return pthread_kill(mThread, SIGUSR1) == 0;
    }

    bool FtraceReader::join() const
    {
        return pthread_join(mThread, nullptr) == 0;
    }

    void * FtraceReader::runStatic(void * arg)
    {
        auto * const ftraceReader = static_cast<FtraceReader *>(arg);
        ftraceReader->run();
        return nullptr;
    }

#ifndef SPLICE_F_MOVE

#include <sys/syscall.h>

// Pre Android-21 does not define splice
#define SPLICE_F_MOVE 1

    static ssize_t sys_splice(int fd_in, loff_t * off_in, int fd_out, loff_t * off_out, size_t len, unsigned int flags)
    {
        return syscall(__NR_splice, fd_in, off_in, fd_out, off_out, len, flags);
    }

#define splice(fd_in, off_in, fd_out, off_out, len, flags) sys_splice(fd_in, off_in, fd_out, off_out, len, flags)

#endif

    //NOLINTNEXTLINE(readability-function-cognitive-complexity)
    void FtraceReader::run()
    {
        {
            constexpr std::size_t comm_length = 16;
            lib::printf_str_t<comm_length> buf {"gatord-reader%02i", mCpu};
            prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&buf), 0, 0, 0);
        }

        // Gator runs at a high priority, reset the priority to the default
        if (setpriority(PRIO_PROCESS, lib::gettid(), 0) == -1) {
            LOG_ERROR("setpriority failed");
            handleException();
        }

        mBarrier->wait();

        // Use a secondary internal pipe here to break a lock dependency between the reader and writer ends.
        //
        // The splice syscall holds a lock on the output pipe (mPfd0/1) which prevents ExternalSource from
        // processing the read end. If the splice syscall sleeps while holding the lock (e.g. waiting to fill
        // a page but the capture has ended) gator-child will deadlock. The secondary pipe avoids this.

        std::array<int, 2> internal_pipe;
        if (pipe2(internal_pipe.data(), O_CLOEXEC) < 0) {
            LOG_ERROR("Failed to open a pipe to allow splicing from the ftrace buffer. Errno %d", errno);
            handleException();
        }

        while (mSessionIsActive) {
            const ssize_t bytes = splice(mTfd, nullptr, internal_pipe[1], nullptr, pageSize, SPLICE_F_MOVE);
            if (bytes == 0) {
                constexpr int sleep_timeout = 100'000;
                constexpr int num_times_to_wait = 10;
                // we can get here after the ftrace pipe has been closed but before the rest of gator
                // has had a chance to respond to the target app closing, so the interrupt() method
                // may not have been called. Wait for a bit to let it catch up.
                // If we blindly exit the loop without waiting then there's a chance that the SIGUSR1
                // signal could arrive during the splice syscall that reads the slop, causing an
                // incomplete read, and resulting in gator exiting with a non-zero error code.
                for (int i = 0; i < num_times_to_wait && mSessionIsActive; ++i) {
                    usleep(sleep_timeout);
                }

                if (mSessionIsActive) {
                    LOG_DEBUG("FTrace pipe has ended but session still seems to be active.");
                }
                break;
            }

            if (bytes < 0) {
                if (errno != EINTR) {
                    LOG_ERROR("splice failed");
                    handleException();
                }
            }
            else {
                // Can there be a short splice read?
                if (bytes != pageSize) {
                    LOG_ERROR("splice short read");
                    handleException();
                }
                // Will be read by gatord-external
                auto sent = splice(internal_pipe[0], nullptr, mPfd1, nullptr, pageSize, SPLICE_F_MOVE);
                if (sent != bytes) {
                    LOG_ERROR("splice failed when sending data to the external event reader");
                    handleException();
                }
            }
        }

        if (!lib::setNonblock(mTfd)) {
            LOG_ERROR("lib::setNonblock failed");
            handleException();
        }

        {
            // Read any slop
            std::array<char, 65536> buf {};
            ssize_t bytes;
            size_t size;

            const auto end_time = std::chrono::steady_clock::now() + FTRACE_SLOP_READ_TIMEOUT_DURATION;
            while (std::chrono::steady_clock::now() < end_time) {
                bytes = read(mTfd, buf.data(), buf.size());
                if (bytes <= 0) {
                    LOG_TRACE("ftrace read finished with result [%zd]", bytes);
                    break;
                }

                size = bytes;
                bytes = write(mPfd1, buf.data(), size);
                if (bytes != static_cast<ssize_t>(size)) {
                    LOG_ERROR("Writing to ftrace pipe failed: fd:%d, size: %zu, bytes: %zd", mPfd1, size, bytes);
                    if (bytes == -1) {
                        LOG_ERROR("ftrace write errno: %d", errno);
                    }
                    handleException();
                }
            }
        }

        close(internal_pipe[0]);
        close(internal_pipe[1]);
        close(mTfd);
        close(mPfd1);
        // Intentionally don't close mPfd0 as it is used after this thread is exited to read the slop
    }
}

FtraceDriver::FtraceDriver(const TraceFsConstants & traceFsConstants,
                           bool use_for_general_tracepoints,
                           bool use_ftrace_for_cpu_frequency,
                           size_t numberOfCores)
    : SimpleDriver("Ftrace"),
      traceFsConstants(traceFsConstants),
      mBarrier(),
      mTracingOn(0),
      mSupported(false),
      mMonotonicRawSupport(false),
      mUseForGeneralTracepoints(use_for_general_tracepoints),
      mUseForCpuFrequency(use_ftrace_for_cpu_frequency),
      mNumberOfCores(numberOfCores)
{
}

void FtraceDriver::readEvents(mxml_node_t * const xml)
{
    // Check the kernel version
    struct utsname utsname;
    if (uname(&utsname) != 0) {
        LOG_ERROR("uname failed");
        handleException();
    }

    // The perf clock was added in 3.10
    auto const kernelVersion = lib::parseLinuxVersion(utsname);
    if (kernelVersion < KERNEL_VERSION(3U, 10U, 0U)) {
        mSupported = false;
        LOG_SETUP("Ftrace is disabled\nFor full ftrace functionality please upgrade to Linux 3.10 or later. With "
                  "user space "
                  "gator and Linux prior to 3.10, ftrace counters with the tracepoint and arg attributes will be "
                  "available.");
        return;
    }
    mMonotonicRawSupport = kernelVersion >= KERNEL_VERSION(4U, 2U, 0U);

    // Is debugfs or tracefs available?
    if (::access(traceFsConstants.path, R_OK) != 0) {
        mSupported = false;
        LOG_SETUP("Ftrace is disabled\nUnable to locate the tracing directory");
        return;
    }

    if (geteuid() != 0) {
        mSupported = false;
        LOG_SETUP("Ftrace is disabled\nFtrace is not supported when running non-root");
        return;
    }

    mSupported = true;

    mxml_node_t * node = xml;
    while (true) {
        node = mxmlFindElement(node, xml, "event", nullptr, nullptr, MXML_DESCEND);
        if (node == nullptr) {
            break;
        }
        const char * counter = mxmlElementGetAttr(node, "counter");
        if (counter == nullptr) {
            continue;
        }

        if (strncmp(counter, "ftrace_", 7) != 0) {
            continue;
        }

        const char * regex = mxmlElementGetAttr(node, "regex");
        const char * tracepoint = mxmlElementGetAttr(node, "tracepoint");
        const char * enable = mxmlElementGetAttr(node, "enable");
        if (enable == nullptr) {
            enable = tracepoint;
        }
        const bool is_cpu_frequency = ((tracepoint != nullptr) && (strcmp(tracepoint, "power/cpu_frequency") == 0)
                                       && (strcmp(counter, "ftrace_power_cpu_frequency") == 0));

        if ((regex == nullptr) && !is_cpu_frequency) {
            LOG_ERROR("The regex counter %s is missing the required regex attribute", counter);
            handleException();
        }

        if ((!mUseForGeneralTracepoints) && (tracepoint != nullptr) && !is_cpu_frequency) {
            LOG_DEBUG("Not using ftrace for counter %s", counter);
            continue;
        }
        if ((!mUseForCpuFrequency) && is_cpu_frequency) {
            LOG_DEBUG("Not using ftrace for counter %s", counter);
            continue;
        }
        if (enable != nullptr) {
            lib::printf_str_t<tracefs_path_buffer_size> buf {"%s/%s/enable", traceFsConstants.path__events, enable};
            if (::access(buf, W_OK) != 0) {
                LOG_SETUP("%s is disabled\n%s was not found", counter, buf.c_str());
                continue;
            }
        }

        LOG_DEBUG("Using ftrace for %s", counter);
        if (is_cpu_frequency) {
            bool const has_cpuinfo = (::access("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq", R_OK) == 0);
            bool const has_scaling = (::access("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", R_OK) == 0);
            if (has_cpuinfo || has_scaling) {
                setCounters(
                    new CpuFrequencyFtraceCounter(getCounters(), traceFsConstants, counter, enable, has_cpuinfo));
            }
        }
        else {
            setCounters(new FtraceCounter(getCounters(), traceFsConstants, counter, enable));
        }
    }
}

std::pair<std::vector<int>, bool> FtraceDriver::prepare()
{
    if (gSessionData.mFtraceRaw) {
        // Don't want the performace impact of sending all formats so gator only sends it for the enabled counters. This means other counters need to be disabled
        if (lib::writeCStringToFile(traceFsConstants.path__events__enable, "0") != 0) {
            LOG_ERROR("Unable to turn off all events");
            handleException();
        }
    }

    for (auto * counter = static_cast<FtraceCounter *>(getCounters()); counter != nullptr;
         counter = static_cast<FtraceCounter *>(counter->getNext())) {
        if (!counter->isEnabled()) {
            continue;
        }
        counter->prepare();
    }

    if (lib::readIntFromFile(traceFsConstants.path__tracing_on, mTracingOn) != 0) {
        LOG_ERROR("Unable to read if ftrace is enabled");
        handleException();
    }

    if (lib::writeCStringToFile(traceFsConstants.path__tracing_on, "0") != 0) {
        LOG_ERROR("Unable to turn ftrace off before truncating the buffer");
        handleException();
    }

    {
        int fd {};
        // The below call can be slow on loaded high-core count systems.
        // NOLINTNEXTLINE(hicpp-signed-bitwise)
        fd = ::open(traceFsConstants.path__trace, O_WRONLY | O_TRUNC | O_CLOEXEC);
        if (fd < 0) {
            LOG_ERROR("Unable truncate ftrace buffer: %s", strerror(errno));
            handleException();
        }
        close(fd);
    }

    const char * const clock = mMonotonicRawSupport ? "mono_raw" : "perf";
    const char * const clock_selected = mMonotonicRawSupport ? "[mono_raw]" : "[perf]";
    const size_t max_trace_clock_file_length = 200;
    ssize_t trace_clock_file_length;
    char trace_clock_file_content[max_trace_clock_file_length + 1] = {0};
    bool must_switch_clock = true;
    // Only write to /trace_clock if the clock actually needs changing,
    // as changing trace_clock can be extremely expensive, especially on large
    // core count systems. The idea is that hopefully only on the first
    // capture, the trace clock needs to be changed. On subsequent captures,
    // the right clock is already being used.
    int fd = ::open(traceFsConstants.path__trace_clock, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        LOG_ERROR("Couldn't open %s", traceFsConstants.path__trace_clock);
        handleException();
    }
    if ((trace_clock_file_length = ::read(fd, trace_clock_file_content, max_trace_clock_file_length - 1)) < 0) {
        LOG_ERROR("Couldn't read from %s", traceFsConstants.path__trace_clock);
        close(fd);
        handleException();
    }
    close(fd);
    trace_clock_file_content[trace_clock_file_length] = 0;
    if (::strstr(trace_clock_file_content, clock_selected) != nullptr) {
        // the right clock was already selected :)
        must_switch_clock = false;
    }

    // Writing to trace_clock can be very slow on loaded high core count
    // systems.
    if (must_switch_clock && lib::writeCStringToFile(traceFsConstants.path__trace_clock, clock) != 0) {
        LOG_ERROR("Unable to switch ftrace to the %s clock, please ensure you are running Linux %s or later",
                  clock,
                  mMonotonicRawSupport ? "4.2" : "3.10");
        handleException();
    }

    if (!gSessionData.mFtraceRaw) {
        const int fd = ::open(traceFsConstants.path__trace_pipe, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            LOG_ERROR("Unable to open trace_pipe");
            handleException();
        }
        return {{fd}, true};
    }

    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = handlerUsr1;
    if (sigaction(SIGUSR1, &act, nullptr) != 0) {
        LOG_ERROR("sigaction failed");
        handleException();
    }

    auto pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0) {
        LOG_ERROR("sysconf PAGESIZE failed");
        handleException();
    }

    mBarrier.init(mNumberOfCores + 1);
    std::pair<std::vector<int>, bool> result {{}, false};
    for (size_t cpu = 0; cpu < mNumberOfCores; ++cpu) {
        std::array<int, 2> pfd;
        if (pipe2(pfd.data(), O_CLOEXEC) != 0) {
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            LOG_ERROR("pipe2 failed, %s (%i)", strerror(errno), errno);
            handleException();
        }

        lib::printf_str_t<tracefs_path_buffer_size> buf {"%s/per_cpu/cpu%zu/trace_pipe_raw",
                                                         traceFsConstants.path,
                                                         cpu};
        const int tfd = ::open(buf, O_RDONLY | O_CLOEXEC);
        (new FtraceReader(&mBarrier, cpu, tfd, pfd[0], pfd[1], pageSize))->start();
        result.first.push_back(pfd[0]);
    }

    return result;
}

void FtraceDriver::start(std::function<void(int, int, std::int64_t)> initialValuesConsumer)
{
    if (lib::writeCStringToFile(traceFsConstants.path__tracing_on, "1") != 0) {
        LOG_ERROR("Unable to turn ftrace on");
        handleException();
    }

    for (auto * counter = static_cast<FtraceCounter *>(getCounters()); counter != nullptr;
         counter = static_cast<FtraceCounter *>(counter->getNext())) {
        if (!counter->isEnabled()) {
            continue;
        }
        for (size_t cpu = 0; cpu < mNumberOfCores; ++cpu) {
            counter->readInitial(cpu, initialValuesConsumer);
        }
    }

    if (gSessionData.mFtraceRaw) {
        mBarrier.wait();
    }
}

std::vector<int> FtraceDriver::requestStop()
{
    lib::writeIntToFile(traceFsConstants.path__tracing_on, mTracingOn);

    for (auto * counter = static_cast<FtraceCounter *>(getCounters()); counter != nullptr;
         counter = static_cast<FtraceCounter *>(counter->getNext())) {
        if (!counter->isEnabled()) {
            continue;
        }
        counter->stop();
    }

    std::vector<int> fds;
    if (gSessionData.mFtraceRaw) {
        for (FtraceReader * reader = FtraceReader::getHead(); reader != nullptr; reader = reader->getNext()) {
            reader->interrupt();
            fds.push_back(reader->getPfd0());
        }
    }
    return fds;
}

//NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void FtraceDriver::stop()
{
    if (gSessionData.mFtraceRaw) {
        for (FtraceReader * reader = FtraceReader::getHead(); reader != nullptr; reader = reader->getNext()) {
            if (!reader->join()) {
                LOG_WARNING("Failed to wait for FtraceReader to finish. It's possible the thread has already ended.");
            }
        }
    }
}

bool FtraceDriver::readTracepointFormats(IPerfAttrsConsumer & attrsConsumer, DynBuf * const printb, DynBuf * const b)
{
    if (!gSessionData.mFtraceRaw) {
        return true;
    }

    if (!printb->printf("%s/header_page", traceFsConstants.path__events)) {
        LOG_DEBUG("DynBuf::printf failed");
        return false;
    }
    if (!b->read(printb->getBuf())) {
        LOG_DEBUG("DynBuf::read failed");
        return false;
    }
    attrsConsumer.marshalHeaderPage(b->getBuf());

    if (!printb->printf("%s/header_event", traceFsConstants.path__events)) {
        LOG_DEBUG("DynBuf::printf failed");
        return false;
    }
    if (!b->read(printb->getBuf())) {
        LOG_DEBUG("DynBuf::read failed");
        return false;
    }
    attrsConsumer.marshalHeaderEvent(b->getBuf());

    std::unique_ptr<DIR, int (*)(DIR *)> dir {opendir(traceFsConstants.path__events__ftrace), &closedir};
    if (dir == nullptr) {
        LOG_ERROR("Unable to open events ftrace folder");
        handleException();
    }
    struct dirent * dirent;
    while ((dirent = readdir(dir.get())) != nullptr) {
        if (dirent->d_name[0] == '.' || dirent->d_type != DT_DIR) {
            continue;
        }
        if (!printb->printf("%s/%s/format", traceFsConstants.path__events__ftrace, dirent->d_name)) {
            LOG_DEBUG("DynBuf::printf failed");
            return false;
        }
        if (!b->read(printb->getBuf())) {
            LOG_DEBUG("DynBuf::read failed");
            return false;
        }
        attrsConsumer.marshalFormat(b->getLength(), b->getBuf());
    }

    for (auto * counter = static_cast<FtraceCounter *>(getCounters()); counter != nullptr;
         counter = static_cast<FtraceCounter *>(counter->getNext())) {
        if (!counter->isEnabled()) {
            continue;
        }
        counter->readTracepointFormat(attrsConsumer);
    }

    return true;
}
