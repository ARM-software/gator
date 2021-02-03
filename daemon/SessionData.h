/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#ifndef SESSION_DATA_H
#define SESSION_DATA_H

#include "Config.h"
#include "Configuration.h"
#include "Constant.h"
#include "Counter.h"
#include "GatorCLIFlags.h"
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

//development version for PROTOCOL_VERSION is of format YYYYMMDD
#define PROTOCOL_VERSION 750
// Differentiates development versions (timestamp) from release versions
#define PROTOCOL_DEV 10000000

#define NS_PER_S 1000000000LL
#define NS_PER_MS 1000000LL
#define NS_PER_US 1000LL

extern const char MALI_GRAPHICS[];
extern const size_t MALI_GRAPHICS_SIZE;

class SharedData {
public:
    SharedData();

    size_t mMaliUtgardCountersSize;
    char mMaliUtgardCounters[1 << 12];
    size_t mMaliMidgardCountersSize;
    char mMaliMidgardCounters[1 << 13];

private:
    // Intentionally unimplemented
    SharedData(const SharedData &) = delete;
    SharedData & operator=(const SharedData &) = delete;
    SharedData(SharedData &&) = delete;
    SharedData & operator=(SharedData &&) = delete;
};

class SessionData {
public:
    static const size_t MAX_STRING_LEN = 80;

    SessionData();

    void initialize();
    void parseSessionXML(char * xmlString);

    shared_memory::unique_ptr<SharedData> mSharedData;

    std::list<std::string> mImages;
    const char * mConfigurationXMLPath;
    const char * mSessionXMLPath;
    const char * mEventsXMLPath;
    const char * mEventsXMLAppend;
    const char * mTargetPath;
    const char * mAPCDir;
    const char * mCaptureWorkingDir;
    std::vector<std::string> mCaptureCommand;
    const char * mCaptureUser;
    const char * mWaitForProcessCommand;
    std::set<int> mPids;
    bool mStopOnExit;

    bool mWaitingOnCommand;
    bool mLocalCapture;
    // halt processing of the driver data until profiling is complete or the buffer is filled
    bool mOneShot;
    bool mIsEBS;
    bool mAllowCommands;
    bool mFtraceRaw;
    bool mSystemWide;
    int mAndroidApiLevel;

    int mBacktraceDepth;
    // number of MB to use for the entire collection buffer
    int mTotalBufferSize;
    int mSampleRate;
    uint64_t mLiveRate;
    int mDuration;
    int mPageSize;
    int mAnnotateStart;
    int64_t parameterSetFlag;
    int mPerfMmapSizeInPages;
    int mSpeSampleRate;

    // PMU Counters
    Counter mCounters[MAX_PERFORMANCE_COUNTERS];

    std::set<Constant> mConstants;

private:
    // Intentionally unimplemented
    SessionData(const SessionData &) = delete;
    SessionData & operator=(const SessionData &) = delete;
    SessionData(SessionData &&) = delete;
    SessionData & operator=(SessionData &&) = delete;
};

extern SessionData gSessionData;
extern const char * const gSrcMd5;

uint64_t getTime();

void logCpuNotFound();

#endif // SESSION_DATA_H
