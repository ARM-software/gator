/**
 * Copyright (C) ARM Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "SessionData.h"

#include <algorithm>

#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "DiskIODriver.h"
#include "FSDriver.h"
#include "HwmonDriver.h"
#include "Logging.h"
#include "MemInfoDriver.h"
#include "NetDriver.h"
#include "OlyUtility.h"
#include "PrimarySourceProvider.h"
#include "PolledDriver.h"
#include "SessionXML.h"

#include "lib/Time.h"

#include "mali_userspace/MaliInstanceLocator.h"

#define CORE_NAME_UNKNOWN "unknown"

const char MALI_GRAPHICS[] = "\0mali_thirdparty_server";
const size_t MALI_GRAPHICS_SIZE = sizeof(MALI_GRAPHICS);

SessionData gSessionData;

GatorCpu *GatorCpu::mHead;

GatorCpu::GatorCpu(const char * const coreName, const char * const pmncName, const char * const dtName, const int cpuid,
                   const int pmncCounters)
        : mNext(mHead),
          mCoreName(coreName),
          mPmncName(pmncName),
          mDtName(dtName),
          mCpuid(cpuid),
          mPmncCounters(pmncCounters),
          mType(-1)
{
    mHead = this;
}

static const char OLD_PMU_PREFIX[] = "ARMv7 Cortex-";
static const char NEW_PMU_PREFIX[] = "ARMv7_Cortex_";

GatorCpu *GatorCpu::find(const char * const name)
{
    GatorCpu *gatorCpu;

    for (gatorCpu = mHead; gatorCpu != NULL; gatorCpu = gatorCpu->mNext) {
        if (strcasecmp(gatorCpu->mPmncName, name) == 0 ||
        // Do these names match but have the old vs new prefix?
                ((strncasecmp(name, OLD_PMU_PREFIX, sizeof(OLD_PMU_PREFIX) - 1) == 0
                        && strncasecmp(gatorCpu->mPmncName, NEW_PMU_PREFIX, sizeof(NEW_PMU_PREFIX) - 1) == 0
                        && strcasecmp(name + sizeof(OLD_PMU_PREFIX) - 1,
                                      gatorCpu->mPmncName + sizeof(NEW_PMU_PREFIX) - 1) == 0))) {
            break;
        }
    }

    return gatorCpu;
}

GatorCpu *GatorCpu::find(const int cpuid)
{
    GatorCpu *gatorCpu;

    for (gatorCpu = mHead; gatorCpu != NULL; gatorCpu = gatorCpu->mNext) {
        if (gatorCpu->mCpuid == cpuid) {
            break;
        }
    }

    return gatorCpu;
}

UncorePmu *UncorePmu::mHead;

UncorePmu::UncorePmu(const char * const coreName, const char * const pmncName, const int pmncCounters,
                     const bool hasCyclesCounter)
        : mNext(mHead),
          mCoreName(coreName),
          mPmncName(pmncName),
          mPmncCounters(pmncCounters),
          mHasCyclesCounter(hasCyclesCounter),
          mType(-1)
{
    mHead = this;
}

UncorePmu *UncorePmu::find(const char * const name)
{
    UncorePmu *gatorCpu;

    for (gatorCpu = mHead; gatorCpu != NULL; gatorCpu = gatorCpu->mNext) {
        if (strcasecmp(name, gatorCpu->mPmncName) == 0) {
            break;
        }
    }

    return gatorCpu;
}

SharedData::SharedData()
        : mCpuIds(),
          mClusterIds(),
          mClusters(),
          mClusterCount(0),
          mMaliUtgardCountersSize(0),
          mMaliUtgardCounters(),
          mMaliMidgardCountersSize(0),
          mMaliMidgardCounters(),
          mClustersAccurate(false)
{
    memset(mCpuIds, -1, sizeof(mCpuIds));
}

SessionData::SessionData()
        : mPrimarySource(),
          mSharedData(),
          mMaliVideo(),
          mMaliHwCntrs(),
          mMidgard(),
          mAtraceDriver(),
          mTtraceDriver(),
          mFtraceDriver(),
          mExternalDriver(),
          mCcnDriver(),
          mCoreName(),
          mImages(),
          mConfigurationXMLPath(),
          mSessionXMLPath(),
          mEventsXMLPath(),
          mEventsXMLAppend(),
          mTargetPath(),
          mAPCDir(),
          mCaptureWorkingDir(),
          mCaptureCommand(),
          mCaptureUser(),
          mWaitingOnCommand(),
          mSessionIsActive(),
          mLocalCapture(),
          mOneShot(),
          mIsEBS(),
          mSentSummary(),
          mAllowCommands(),
          mFtraceRaw(),
          mAndroidApiLevel(),
          mMonotonicStarted(),
          mBacktraceDepth(),
          mTotalBufferSize(),
          mSampleRate(),
          mLiveRate(),
          mDuration(),
          mCores(),
          mPageSize(),
          mMaxCpuId(),
          mAnnotateStart(),
          mCountersError(),
          mCounters()
{
}

SessionData::~SessionData()
{
}

static long getMaxCoreNum()
{
    DIR *dir = opendir("/sys/devices/system/cpu");
    if (dir == NULL) {
        logg.logError("Unable to determine the number of cores on the target, opendir failed");
        handleException();
    }

    long maxCoreNum = -1;
    struct dirent *dirent;
    while ((dirent = readdir(dir)) != NULL) {
        if (strncmp(dirent->d_name, "cpu", 3) == 0) {
            long coreNum;
            if (stringToLong(&coreNum, dirent->d_name + 3, 10) && (coreNum >= maxCoreNum)) {
                maxCoreNum = coreNum + 1;
            }
        }
    }
    closedir(dir);

    if (maxCoreNum < 1) {
        logg.logError("Unable to determine the number of cores on the target, no cpu# directories found");
        handleException();
    }

    if (maxCoreNum >= NR_CPUS) {
        logg.logError("Too many cores on the target, please increase NR_CPUS in Config.h");
        handleException();
    }

    return maxCoreNum;
}

void SessionData::initialize()
{
    mSharedData = reinterpret_cast<SharedData *>(mmap(NULL, sizeof(*mSharedData), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    if (mSharedData == MAP_FAILED) {
        logg.logError("Unable to mmap shared memory for cpuids");
        handleException();
    }

    // Use placement new to construct but not allocate the object
    new (reinterpret_cast<void *>(mSharedData)) SharedData();

    mWaitingOnCommand = false;
    mSessionIsActive = false;
    mLocalCapture = false;
    mOneShot = false;
    mSentSummary = false;
    mAllowCommands = false;
    mFtraceRaw = false;
    strcpy(mCoreName, CORE_NAME_UNKNOWN);
    readModel();
    readCpuInfo();
    mImages.clear();
    mConfigurationXMLPath = NULL;
    mSessionXMLPath = NULL;
    mEventsXMLPath = NULL;
    mEventsXMLAppend = NULL;
    mTargetPath = NULL;
    mAPCDir = NULL;
    mCaptureWorkingDir = NULL;
    mCaptureCommand = NULL;
    mCaptureUser = NULL;
    mSampleRate = 0;
    mLiveRate = 0;
    mDuration = 0;
    mMonotonicStarted = -1;
    mBacktraceDepth = 0;
    mTotalBufferSize = 0;
    mCores = static_cast<int>(getMaxCoreNum());
    long l = sysconf(_SC_PAGE_SIZE);
    if (l < 0) {
        logg.logError("Unable to obtain the page size");
        handleException();
    }
    mPageSize = static_cast<int>(l);
    mAnnotateStart = -1;
}

void SessionData::parseSessionXML(char* xmlString)
{
    SessionXML session(xmlString);
    session.parse();

    // Set session data values - use prime numbers just below the desired value to reduce the chance of events firing at the same time
    if (strcmp(session.parameters.sample_rate, "high") == 0) {
        mSampleRate = 10007; // 10000
    }
    else if (strcmp(session.parameters.sample_rate, "normal") == 0) {
        mSampleRate = 1009; // 1000
    }
    else if (strcmp(session.parameters.sample_rate, "low") == 0) {
        mSampleRate = 101; // 100
    }
    else if (strcmp(session.parameters.sample_rate, "none") == 0) {
        mSampleRate = 0;
    }
    else {
        logg.logError("Invalid sample rate (%s) in session xml.", session.parameters.sample_rate);
        handleException();
    }
    mBacktraceDepth = session.parameters.call_stack_unwinding == true ? 128 : 0;

    // Determine buffer size (in MB) based on buffer mode
    mOneShot = true;
    if (strcmp(session.parameters.buffer_mode, "streaming") == 0) {
        mOneShot = false;
        mTotalBufferSize = 1;
    }
    else if (strcmp(session.parameters.buffer_mode, "small") == 0) {
        mTotalBufferSize = 1;
    }
    else if (strcmp(session.parameters.buffer_mode, "normal") == 0) {
        mTotalBufferSize = 4;
    }
    else if (strcmp(session.parameters.buffer_mode, "large") == 0) {
        mTotalBufferSize = 16;
    }
    else {
        logg.logError("Invalid value for buffer mode in session xml.");
        handleException();
    }

    // Convert milli- to nanoseconds
    mLiveRate = session.parameters.live_rate * 1000000ll;
    if (mLiveRate > 0 && mLocalCapture) {
        logg.logMessage("Local capture is not compatable with live, disabling live");
        mLiveRate = 0;
    }

    if (!mAllowCommands && (mCaptureCommand != NULL)) {
        logg.logError("Running a command during a capture is not currently allowed. Please restart gatord with the -a flag.");
        handleException();
    }
}

void SessionData::readModel()
{
    FILE *fh = fopen_cloexec("/proc/device-tree/model", "rb");
    if (fh == NULL) {
        return;
    }

    char buf[256];
    if (fgets(buf, sizeof(buf), fh) != NULL) {
        strcpy(mCoreName, buf);
    }

    fclose(fh);
}

static void setImplementer(int * const cpuId, const int implementer)
{
    if (*cpuId == -1) {
        *cpuId = 0;
    }
    *cpuId |= implementer << 12;
}

static void setPart(int * const cpuId, const int part)
{
    if (*cpuId == -1) {
        *cpuId = 0;
    }
    *cpuId |= part;
}

static const char HARDWARE[] = "Hardware";
static const char CPU_IMPLEMENTER[] = "CPU implementer";
static const char CPU_PART[] = "CPU part";
static const char PROCESSOR[] = "processor";

void SessionData::readCpuInfo()
{
    char temp[256]; // arbitrarily large amount
    mMaxCpuId = -1;

    FILE *f = fopen_cloexec("/proc/cpuinfo", "r");
    if (f == NULL) {
        logg.logMessage("Error opening /proc/cpuinfo\n"
                "The core name in the captured xml file will be 'unknown'.");
        return;
    }

    bool foundCoreName = (strcmp(mCoreName, CORE_NAME_UNKNOWN) != 0);
    int processor = -1;
    int minProcessor = NR_CPUS;
    int maxProcessor = 0;
    bool foundProcessorInSection = false;
    bool invalidFormat = false;
    while (fgets(temp, sizeof(temp), f)) {
        const size_t len = strlen(temp);

        if (len > 0) {
            // Replace the line feed with a null
            temp[len - 1] = '\0';
        }

        logg.logMessage("cpuinfo: %s", temp);

        if (len == 1) {
            // New section, clear the processor. Streamline will not know the cpus if the pre Linux 3.8 format of cpuinfo is encountered but also that no incorrect information will be transmitted.
            processor = -1;
            foundProcessorInSection = false;
            continue;
        }

        const bool foundHardware = !foundCoreName && strncmp(temp, HARDWARE, sizeof(HARDWARE) - 1) == 0;
        const bool foundCPUImplementer = strncmp(temp, CPU_IMPLEMENTER, sizeof(CPU_IMPLEMENTER) - 1) == 0;
        const bool foundCPUPart = strncmp(temp, CPU_PART, sizeof(CPU_PART) - 1) == 0;
        const bool foundProcessor = strncmp(temp, PROCESSOR, sizeof(PROCESSOR) - 1) == 0;
        if (foundHardware || foundCPUImplementer || foundCPUPart || foundProcessor) {
            char* position = strchr(temp, ':');
            if (position == NULL || static_cast<unsigned int>(position - temp) + 2 >= strlen(temp)) {
                logg.logMessage("Unknown format of /proc/cpuinfo\n"
                        "The core name in the captured xml file will be 'unknown'.");
                return;
            }
            position += 2;

            if (foundHardware) {
                strncpy(mCoreName, position, sizeof(mCoreName));
                mCoreName[sizeof(mCoreName) - 1] = 0; // strncpy does not guarantee a null-terminated string
                foundCoreName = true;
            }

            if (foundCPUImplementer) {
                int implementer;
                if (!stringToInt(&implementer, position, 0)) {
                    // Do nothing
                }
                else if (processor >= NR_CPUS) {
                    logg.logMessage("Too many processors, please increase NR_CPUS");
                }
                else if (processor >= 0) {
                    setImplementer(&mSharedData->mCpuIds[processor], implementer);
                }
                else {
                    setImplementer(&mMaxCpuId, implementer);
                    invalidFormat = true;
                }
            }

            if (foundCPUPart) {
                int cpuId;
                if (!stringToInt(&cpuId, position, 0)) {
                    // Do nothing
                }
                else if (processor >= NR_CPUS) {
                    logg.logMessage("Too many processors, please increase NR_CPUS");
                }
                else if (processor >= 0) {
                    setPart(&mSharedData->mCpuIds[processor], cpuId);
                }
                else {
                    setPart(&mMaxCpuId, cpuId);
                    invalidFormat = true;
                }
            }

            if (foundProcessor) {
                int processorId = -1;
                const bool converted = stringToInt(&processorId, position, 0);

                // update min and max processor ids
                if (converted) {
                    minProcessor = (processorId < minProcessor ? processorId : minProcessor);
                    maxProcessor = (processorId > maxProcessor ? processorId : maxProcessor);
                }

                if (foundProcessorInSection) {
                    // Found a second processor in this section, ignore them all
                    processor = -1;
                    invalidFormat = true;
                }
                else if (converted) {
                    processor = processorId;
                    foundProcessorInSection = true;
                }
            }
        }
    }

    if (invalidFormat && (mMaxCpuId != -1) && (minProcessor <= maxProcessor)) {
        minProcessor = (minProcessor > 0 ? minProcessor : 0);
        maxProcessor = (maxProcessor < NR_CPUS ? maxProcessor + 1 : NR_CPUS);

        for (processor = minProcessor; processor < maxProcessor; ++processor) {
            if (mSharedData->mCpuIds[processor] == -1) {
                logg.logMessage("Setting global CPUID 0x%x for processors %i ", mMaxCpuId, processor);
                mSharedData->mCpuIds[processor] = mMaxCpuId;
            }
        }
    }

    updateClusterIds();

    if (!foundCoreName) {
        logg.logMessage("Could not determine core name from /proc/cpuinfo\n"
                "The core name in the captured xml file will be 'unknown'.");
    }
    fclose(f);
}

static int clusterCompare(const void *a, const void *b)
{
    const GatorCpu * const * const lhs = reinterpret_cast<const GatorCpu * const *>(a);
    const GatorCpu * const * const rhs = reinterpret_cast<const GatorCpu * const *>(b);

    return (*rhs)->getCpuid() - (*lhs)->getCpuid();
}

void SessionData::updateClusterIds()
{
    qsort(&mSharedData->mClusters, mSharedData->mClusterCount, sizeof(*mSharedData->mClusters), clusterCompare);
    mSharedData->mClustersAccurate = true;

    int lastClusterId = -1;
    for (int i = 0; i < NR_CPUS; ++i) {
        // If this does not have the full topology in /proc/cpuinfo, mCpuIds[0] may not have the 1 CPU part emitted - this guarantees it's in mMaxCpuId
        if (mSharedData->mCpuIds[i] > mMaxCpuId) {
            mMaxCpuId = mSharedData->mCpuIds[i];
        }

        int clusterId = -1;
        for (int j = 0; j < std::min(mSharedData->mClusterCount, ARRAY_LENGTH(mSharedData->mClusters)); ++j) {
            const int cpuId = mSharedData->mClusters[j]->getCpuid();
            if (mSharedData->mCpuIds[i] == cpuId) {
                clusterId = j;
            }
        }
        if (i < mCores && clusterId == -1) {
            // No corresponding cluster found for this CPU, most likely this is a big LITTLE system without multi-PMU support
            // assume it belongs to the last cluster seen
            mSharedData->mClusterIds[i] = lastClusterId;
            mSharedData->mClustersAccurate = false;
        }
        else {
            mSharedData->mClusterIds[i] = clusterId;
            lastClusterId = clusterId;
        }
    }
}

uint64_t getTime()
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        logg.logError("Failed to get uptime");
        handleException();
    }
    return (NS_PER_S * ts.tv_sec + ts.tv_nsec);
}

int getEventKey()
{
    // key 0 is reserved as a timestamp
    // key 1 is reserved as the marker for thread specific counters
    // key 2 is reserved as the marker for core
    // Odd keys are assigned by the driver, even keys by the daemon
    static int key = 4;

    const int ret = key;
    key += 2;
    return ret;
}

int pipe_cloexec(int pipefd[2])
{
    if (pipe(pipefd) != 0) {
        return -1;
    }

    int fdf;
    if (((fdf = fcntl(pipefd[0], F_GETFD)) == -1) || (fcntl(pipefd[0], F_SETFD, fdf | FD_CLOEXEC) != 0)
            || ((fdf = fcntl(pipefd[1], F_GETFD)) == -1) || (fcntl(pipefd[1], F_SETFD, fdf | FD_CLOEXEC) != 0)) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    return 0;
}

FILE *fopen_cloexec(const char *path, const char *mode)
{
    FILE *fh = fopen(path, mode);
    if (fh == NULL) {
        return NULL;
    }
    int fd = fileno(fh);
    int fdf = fcntl(fd, F_GETFD);
    if ((fdf == -1) || (fcntl(fd, F_SETFD, fdf | FD_CLOEXEC) != 0)) {
        fclose(fh);
        return NULL;
    }
    return fh;
}

bool setNonblock(const int fd)
{
    int flags;

    flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        logg.logMessage("fcntl getfl failed");
        return false;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        logg.logMessage("fcntl setfl failed");
        return false;
    }

    return true;
}

bool writeAll(const int fd, const void * const buf, const size_t pos)
{
    size_t written = 0;
    while (written < pos) {
        ssize_t bytes = write(fd, reinterpret_cast<const uint8_t *>(buf) + written, pos - written);
        if (bytes <= 0) {
            logg.logMessage("write failed");
            return false;
        }
        written += bytes;
    }

    return true;
}

bool readAll(const int fd, void * const buf, const size_t count)
{
    size_t pos = 0;
    while (pos < count) {
        ssize_t bytes = read(fd, reinterpret_cast<uint8_t *>(buf) + pos, count - pos);
        if (bytes <= 0) {
            logg.logMessage("read failed");
            return false;
        }
        pos += bytes;
    }

    return true;
}

bool getLinuxVersion(int version[3])
{
    // Check the kernel version
    struct utsname utsname;
    if (uname(&utsname) != 0) {
        logg.logMessage("uname failed");
        return false;
    }

    version[0] = 0;
    version[1] = 0;
    version[2] = 0;

    int part = 0;
    char *ch = utsname.release;
    while (*ch >= '0' && *ch <= '9' && part < 3) {
        version[part] = 10 * version[part] + *ch - '0';

        ++ch;
        if (*ch == '.') {
            ++part;
            ++ch;
        }
    }

    return true;
}
