/* Copyright (C) 2010-2021 by Arm Limited. All rights reserved. */

#ifndef SESSION_DATA_H
#define SESSION_DATA_H

#include "Config.h"
#include "Configuration.h"
#include "Constant.h"
#include "Counter.h"
#include "GatorCLIFlags.h"
#include "ProtocolVersion.h"
#include "lib/SharedMemory.h"
#include "mxml/mxml.h"

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <semaphore.h>
#include <set>
#include <string>
#include <vector>

#define NS_PER_S 1000000000LL
#define NS_PER_MS 1000000LL
#define NS_PER_US 1000LL

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
    char mMaliUtgardCounters[1 << 12];
    size_t mMaliMidgardCountersSize {0};
    char mMaliMidgardCounters[1 << 13];
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

    const char * mConfigurationXMLPath {nullptr};
    const char * mSessionXMLPath {nullptr};
    const char * mEventsXMLPath {nullptr};
    const char * mEventsXMLAppend {nullptr};
    const char * mTargetPath {nullptr};
    const char * mAPCDir {nullptr};
    const char * mCaptureWorkingDir {nullptr};
    const char * mCaptureUser {nullptr};
    const char * mWaitForProcessCommand {nullptr};
    uint64_t mLiveRate {0};
    uint64_t parameterSetFlag {0};
    int mAndroidApiLevel {0};
    int mBacktraceDepth {0};
    // number of MB to use for the entire collection buffer
    int mTotalBufferSize {0};
    int mSampleRate {0};
    int mDuration {0};
    int mPageSize {0};
    int mAnnotateStart {0};
    int mPerfMmapSizeInPages {0};
    int mSpeSampleRate {-1};
    bool mStopOnExit {false};
    bool mWaitingOnCommand {false};
    bool mLocalCapture {false};
    // halt processing of the driver data until profiling is complete or the buffer is filled
    bool mOneShot {false};
    bool mIsEBS {false};
    bool mAllowCommands {false};
    bool mFtraceRaw {false};
    bool mSystemWide {false};
    bool mExcludeKernelEvents {false};

    // PMU Counters
    Counter mCounters[MAX_PERFORMANCE_COUNTERS];
};

extern SessionData gSessionData;
extern const char * const gSrcMd5;

uint64_t getTime();

void logCpuNotFound();

#endif // SESSION_DATA_H
