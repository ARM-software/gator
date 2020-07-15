/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#include "SessionData.h"

#include "CpuUtils.h"
#include "DiskIODriver.h"
#include "FSDriver.h"
#include "HwmonDriver.h"
#include "Logging.h"
#include "MemInfoDriver.h"
#include "NetDriver.h"
#include "OlyUtility.h"
#include "PrimarySourceProvider.h"
#include "SessionXML.h"
#include "lib/File.h"
#include "lib/Format.h"
#include "lib/Time.h"
#include "mali_userspace/MaliInstanceLocator.h"

#include <algorithm>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

const char MALI_GRAPHICS[] = "\0mali_thirdparty_server";
const size_t MALI_GRAPHICS_SIZE = sizeof(MALI_GRAPHICS);

SessionData gSessionData;

SharedData::SharedData()
    : mMaliUtgardCountersSize(0), mMaliUtgardCounters(), mMaliMidgardCountersSize(0), mMaliMidgardCounters()
{
}

SessionData::SessionData()
    : mSharedData(),
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
      mWaitForProcessCommand(),
      mPids(),
      mStopOnExit(),
      mWaitingOnCommand(),
      mSessionIsActive(),
      mLocalCapture(),
      mOneShot(),
      mIsEBS(),
      mSentSummary(),
      mAllowCommands(),
      mFtraceRaw(),
      mSystemWide(),
      mAndroidApiLevel(),
      mMonotonicStarted(),
      mBacktraceDepth(),
      mTotalBufferSize(),
      mSampleRate(),
      mLiveRate(),
      mDuration(),
      mPageSize(),
      mAnnotateStart(),
      parameterSetFlag(),
      mPerfMmapSizeInPages(),
      mSpeSampleRate(-1),
      mCounters()
{
}

void SessionData::initialize()
{
    mSharedData = shared_memory::make_unique<SharedData>();
    mWaitingOnCommand = false;
    mSessionIsActive = false;
    mLocalCapture = false;
    mOneShot = false;
    mSentSummary = false;
    mAllowCommands = false;
    mFtraceRaw = false;
    mSystemWide = false;
    mImages.clear();
    mConfigurationXMLPath = nullptr;
    mSessionXMLPath = nullptr;
    mEventsXMLPath = nullptr;
    mEventsXMLAppend = nullptr;
    mTargetPath = nullptr;
    mAPCDir = nullptr;
    mCaptureWorkingDir = nullptr;
    mCaptureUser = nullptr;
    mSampleRate = 0;
    mLiveRate = 0;
    mDuration = 0;
    mMonotonicStarted = -1;
    mBacktraceDepth = 0;
    mTotalBufferSize = 0;
    long l = sysconf(_SC_PAGE_SIZE);
    if (l < 0) {
        logg.logError("Unable to obtain the page size");
        handleException();
    }
    mPageSize = static_cast<int>(l);
    mAnnotateStart = -1;
    parameterSetFlag = 0;
}

void SessionData::parseSessionXML(char * xmlString)
{
    SessionXML session(xmlString);
    session.parse();

    // Set session data values - use prime numbers just below the desired value to reduce the chance of events firing at the same time
    if ((gSessionData.parameterSetFlag & USE_CMDLINE_ARG_SAMPLE_RATE) == 0) {
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
    }
    if ((gSessionData.parameterSetFlag & USE_CMDLINE_ARG_CALL_STACK_UNWINDING) == 0) {
        mBacktraceDepth = session.parameters.call_stack_unwinding ? 128 : 0;
    }

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
    mLiveRate = session.parameters.live_rate * 1000000LL;
    if (mLiveRate > 0 && mLocalCapture) {
        logg.logMessage("Local capture is not compatable with live, disabling live");
        mLiveRate = 0;
    }
    if ((!mSystemWide) && (mWaitForProcessCommand == nullptr) && mCaptureCommand.empty() && mPids.empty()) {
        logg.logError("No command specified in Capture & Analysis Options.");
        handleException();
    }

    if ((!mAllowCommands) && (!mCaptureCommand.empty()) &&
        ((gSessionData.parameterSetFlag & USE_CMDLINE_ARG_CAPTURE_COMMAND) == 0)) {
        logg.logError(
            "Running a command during a capture is not currently allowed. Please restart gatord with the -a flag.");
        handleException();
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
