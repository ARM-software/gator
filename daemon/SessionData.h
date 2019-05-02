/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SESSION_DATA_H
#define SESSION_DATA_H

#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <stdint.h>
#include <vector>
#include <semaphore.h>

#include "ClassBoilerPlate.h"
#include "Config.h"
#include "Configuration.h"
#include "Counter.h"
#include "GatorCLIFlags.h"
#include "lib/SharedMemory.h"

#include "mxml/mxml.h"

#define PROTOCOL_VERSION 690
// Differentiates development versions (timestamp) from release versions
#define PROTOCOL_DEV 10000000

#define NS_PER_S 1000000000LL
#define NS_PER_MS 1000000LL
#define NS_PER_US 1000LL

extern const char MALI_GRAPHICS[];
extern const size_t MALI_GRAPHICS_SIZE;

class SharedData
{
public:
    SharedData();

    size_t mMaliUtgardCountersSize;
    char mMaliUtgardCounters[1 << 12];
    size_t mMaliMidgardCountersSize;
    char mMaliMidgardCounters[1 << 13];

private:
    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(SharedData)
    ;
};

class SessionData
{
public:
    static const size_t MAX_STRING_LEN = 80;

    SessionData();
    ~SessionData();

    void initialize();
    void parseSessionXML(char* xmlString);


    shared_memory::unique_ptr<SharedData> mSharedData;

    std::list<std::string> mImages;
    const char *mConfigurationXMLPath;
    const char *mSessionXMLPath;
    const char *mEventsXMLPath;
    const char *mEventsXMLAppend;
    const char *mTargetPath;
    const char *mAPCDir;
    const char *mCaptureWorkingDir;
    std::vector<std::string> mCaptureCommand;
    const char *mCaptureUser;
    const char *mWaitForProcessCommand;
    std::set<int> mPids;
    bool mStopOnExit;

    bool mWaitingOnCommand;
    bool mSessionIsActive;
    bool mLocalCapture;
    // halt processing of the driver data until profiling is complete or the buffer is filled
    bool mOneShot;
    bool mIsEBS;
    bool mSentSummary;
    bool mAllowCommands;
    bool mFtraceRaw;
    bool mSystemWide;
    int mAndroidApiLevel;

    int64_t mMonotonicStarted;
    int mBacktraceDepth;
    // number of MB to use for the entire collection buffer
    int mTotalBufferSize;
    int mSampleRate;
    int64_t mLiveRate;
    int mDuration;
    int mPageSize;
    int mAnnotateStart;
    int64_t parameterSetFlag;
    int mPerfMmapSizeInPages;

    // PMU Counters
    Counter mCounters[MAX_PERFORMANCE_COUNTERS];

    // map used to lookup counter names to events (e.g for cycle counters)
    std::map<std::string, int> globalCounterToEventMap;

private:
    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(SessionData)
    ;
};

extern SessionData gSessionData;
extern const char * const gSrcMd5;

uint64_t getTime();
int getEventKey();
void logCpuNotFound();


const char *mxmlWhitespaceCB(mxml_node_t *node, int where);

void copyMxmlElementAttrs(mxml_node_t *dest, mxml_node_t *src);

#endif // SESSION_DATA_H
