/* Copyright (C) 2010-2024 by Arm Limited. All rights reserved. */

#include "SessionData.h"

#include "Configuration.h"
#include "GatorCLIFlags.h"
#include "Logging.h"
#include "SessionXML.h"
#include "lib/SharedMemory.h"

#include <cstring>

#include <unistd.h>

const char MALI_GRAPHICS[] = "\0mali_thirdparty_server";
// Size of the MALI_GRAPHICS socket path, including the null-terminator
const size_t MALI_GRAPHICS_SIZE = sizeof(MALI_GRAPHICS);

SessionData gSessionData;

void SessionData::initialize()
{
    mSharedData = shared_memory::make_unique<SharedData>();
    mWaitingOnCommand = false;
    mLocalCapture = false;
    mOneShot = false;
    mAllowCommands = false;
    mFtraceRaw = false;
    mCaptureOperationMode = CaptureOperationMode::application_default;
    mExcludeKernelEvents = false;
    mEnableOffCpuSampling = false;
    mImages.clear();
    mConfigurationXMLPath = nullptr;
    mSessionXMLPath = nullptr;
    mEventsXMLPath = nullptr;
    mEventsXMLAppend = nullptr;
    mTargetPath = nullptr;
    mAPCDir = nullptr;
    mCaptureWorkingDir = nullptr;
    mCaptureUser = nullptr;
    mSampleRate = none;
    mSampleRateGpu = none;
    mLiveRate = 0;
    mDuration = 0;
    mBacktraceDepth = 0;
    mTotalBufferSize = 0;
    long l = sysconf(_SC_PAGE_SIZE);
    if (l < 0) {
        LOG_ERROR("Unable to obtain the page size");
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
            mSampleRate = high;
            mSampleRateGpu = mSampleRate;
        }
        else if (strcmp(session.parameters.sample_rate, "normal") == 0) {
            mSampleRate = normal;

            // sample rate will be doubled in normal mode for gpuid >= Valhall
            mSampleRateGpu = normal_x2;
        }
        else if (strcmp(session.parameters.sample_rate, "low") == 0) {
            mSampleRate = low;
            mSampleRateGpu = mSampleRate;
        }
        else if (strcmp(session.parameters.sample_rate, "none") == 0) {
            mSampleRate = none;
            mSampleRateGpu = mSampleRate;
        }
        else {
            LOG_ERROR("Invalid sample rate (%s) in session xml.", session.parameters.sample_rate);
            handleException();
        }
    }

    if ((gSessionData.parameterSetFlag & USE_CMDLINE_ARG_CALL_STACK_UNWINDING) == 0) {
        // NOLINTNEXTLINE(readability-magic-numbers)
        mBacktraceDepth = session.parameters.call_stack_unwinding ? 128 : 0;
    }

    // Determine buffer size (in MB) based on buffer mode
    mOneShot = true;
    if (strcmp(session.parameters.buffer_mode, "streaming") == 0) {
        mOneShot = false;
        mTotalBufferSize = 1; //NOLINT(readability-magic-numbers)
    }
    else if (strcmp(session.parameters.buffer_mode, "small") == 0) {
        mTotalBufferSize = 16; //NOLINT(readability-magic-numbers)
    }
    else if (strcmp(session.parameters.buffer_mode, "normal") == 0) {
        mTotalBufferSize = 64; //NOLINT(readability-magic-numbers)
    }
    else if (strcmp(session.parameters.buffer_mode, "large") == 0) {
        mTotalBufferSize = 256; //NOLINT(readability-magic-numbers)
    }
    else {
        LOG_ERROR("Invalid value for buffer mode in session xml.");
        handleException();
    }

    mLiveRate = 0;
    if (session.parameters.live_rate > 0) {
        if (mLocalCapture) {
            LOG_DEBUG("Local capture is not compatable with live, disabling live");
        }
        else {
            // Convert milli- to nanoseconds
            mLiveRate = session.parameters.live_rate * 1000000ULL;
        }
    }
    if ((!isCaptureOperationModeSystemWide(mCaptureOperationMode)) && (mWaitForProcessCommand == nullptr)
        && mCaptureCommand.empty() && mPids.empty()) {
        LOG_ERROR("No command specified in Capture and Analysis Options.");
        handleException();
    }

    if ((!mAllowCommands) && (!mCaptureCommand.empty())
        && ((gSessionData.parameterSetFlag & USE_CMDLINE_ARG_CAPTURE_COMMAND) == 0)) {
        LOG_ERROR(
            "Running a command during a capture is not currently allowed. Please restart gatord with the -a flag.");
        handleException();
    }
}
