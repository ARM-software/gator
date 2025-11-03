/* Copyright (C) 2010-2025 by Arm Limited. All rights reserved. */

#ifndef SESSION_DATA_H
#define SESSION_DATA_H

#include "Configuration.h"
#include "Constant.h"
#include "Counter.h"
#include "lib/SharedMemory.h"
#include "linux/smmu_identifier.h"

#include <cstdint>
#include <list>
#include <set>
#include <string>
#include <vector>

#include <mxml.h>
#include <semaphore.h>

extern const char MALI_GRAPHICS[];
extern const size_t MALI_GRAPHICS_SIZE;

class SharedData {
public:
    SharedData() = default;
    // Intentionally unimplemented
    SharedData(const SharedData &) = delete;
    SharedData & operator=(const SharedData &) = delete;
    SharedData(SharedData &&) = delete;
    SharedData & operator=(SharedData &&) = delete;

    size_t mMaliUtgardCountersSize {0};
    uint8_t mMaliUtgardCounters[1 << 12];
    size_t mMaliMidgardCountersSize {0};
    uint8_t mMaliMidgardCounters[1 << 13];
};

class SessionData {
public:
    static const size_t MAX_STRING_LEN = 80;

    SessionData() = default;
    // Intentionally unimplemented
    SessionData(const SessionData &) = delete;
    SessionData & operator=(const SessionData &) = delete;
    SessionData(SessionData &&) = delete;
    SessionData & operator=(SessionData &&) = delete;

    void initialize();
    void parseSessionXML(char * xmlString);

    shared_memory::unique_ptr<SharedData> mSharedData {};

    std::list<std::string> mImages {};
    std::vector<std::string> mCaptureCommand {};
    std::set<int> mPids {};
    std::set<Constant> mConstants {};
    std::vector<Counter> mCounters {};
    gator::smmuv3::default_identifiers_t smmu_identifiers {};

    const char * mConfigurationXMLPath {nullptr};
    const char * mSessionXMLPath {nullptr};
    const char * mEventsXMLPath {nullptr};
    const char * mEventsXMLAppend {nullptr};
    const char * mTargetPath {nullptr};
    const char * mAPCDir {nullptr};
    const char * mCaptureWorkingDir {nullptr};
    const char * mCaptureUser {nullptr};
    const char * mWaitForProcessCommand {nullptr};
    const char * mAndroidPackage {nullptr};
    const char * mAndroidActivity {nullptr};
    const char * mAndroidActivityFlags {nullptr};
    uint64_t mLiveRate {0};
    uint64_t parameterSetFlag {0};
    int mBacktraceDepth {0};
    // number of MB to use for the entire collection buffer
    int mTotalBufferSize {0};
    SampleRate mSampleRate {none};
    // sampling rate overriden for some GPUs (see mali_userspace::maliGpuSampleRateIsUpgradeable function)
    SampleRate mSampleRateGpu {none};
    int mDuration {0};
    int mPageSize {0};
    int mAnnotateStart {0};
    int mPerfMmapSizeInPages {0};
    int mSpeSampleRate {-1};
    int mOverrideNoPmuSlots {-1};

    CaptureOperationMode mCaptureOperationMode = CaptureOperationMode::system_wide;
    MetricSamplingMode mMetricSamplingMode = MetricSamplingMode::automatic;

    bool mStopOnExit {false};
    bool mWaitingOnCommand {false};
    bool mLocalCapture {false};
    // halt processing of the driver data until profiling is complete or the buffer is filled
    bool mOneShot {false};
    bool mIsEBS {false};
    bool mAllowCommands {false};
    bool mFtraceRaw {false};
    bool mExcludeKernelEvents {false};
    bool mEnableOffCpuSampling {false};
    bool mLogToFile {false};
    GPUTimelineEnablement mUseGPUTimeline {GPUTimelineEnablement::automatic};
};

extern SessionData gSessionData;
extern const char * const gSrcMd5;
extern const char * const gBuildId;
extern const char * const gCopyrightYear;

void logCpuNotFound();

#endif // SESSION_DATA_H
