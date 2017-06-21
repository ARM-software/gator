/**
 * Copyright (C) ARM Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SESSION_DATA_H
#define SESSION_DATA_H

#include <list>
#include <memory>
#include <string>
#include <stdint.h>

#include "ClassBoilerPlate.h"
#include "AtraceDriver.h"
#include "CCNDriver.h"
#include "Config.h"
#include "Counter.h"
#include "ExternalDriver.h"
#include "FtraceDriver.h"
#include "KMod.h"
#include "MaliVideoDriver.h"
#include "MidgardDriver.h"
#include "PerfDriver.h"
#include "TtraceDriver.h"
#include "mali_userspace/MaliHwCntrDriver.h"

#define PROTOCOL_VERSION 630
// Differentiates development versions (timestamp) from release versions
#define PROTOCOL_DEV 10000000

#define NS_PER_S 1000000000LL
#define NS_PER_MS 1000000LL
#define NS_PER_US 1000LL

extern const char MALI_GRAPHICS[];
extern const size_t MALI_GRAPHICS_SIZE;

class PrimarySourceProvider;
class PolledDriver;

class GatorCpu
{
public:
    GatorCpu(const char * const coreName, const char * const pmncName, const char * const dtName, const int cpuid,
             const int pmncCounters);

    static GatorCpu *getHead()
    {
        return mHead;
    }

    GatorCpu *getNext() const
    {
        return mNext;
    }

    const char *getCoreName() const
    {
        return mCoreName;
    }

    const char *getPmncName() const
    {
        return mPmncName;
    }

    const char *getDtName() const
    {
        return mDtName;
    }

    int getCpuid() const
    {
        return mCpuid;
    }

    int getPmncCounters() const
    {
        return mPmncCounters;
    }

    void setType(int type)
    {
        mType = type;
    }

    int getType() const
    {
        return mType;
    }

    bool isTypeValid() const
    {
        return mType != -1;
    }

    static GatorCpu *find(const char * const name);

    static GatorCpu *find(const int cpuid);

private:
    static GatorCpu *mHead;
    GatorCpu * const mNext;
    const char * const mCoreName;
    const char * const mPmncName;
    const char * const mDtName;
    const int mCpuid;
    const int mPmncCounters;
    int mType;
};

class UncorePmu
{
public:
    UncorePmu(const char * const coreName, const char * const pmncName, const int pmncCounters,
              const bool hasCyclesCounter);

    static UncorePmu *getHead()
    {
        return mHead;
    }

    UncorePmu *getNext() const
    {
        return mNext;
    }

    const char *getCoreName() const
    {
        return mCoreName;
    }

    const char *getPmncName() const
    {
        return mPmncName;
    }

    int getPmncCounters() const
    {
        return mPmncCounters;
    }

    bool getHasCyclesCounter() const
    {
        return mHasCyclesCounter;
    }

    void setType(int type)
    {
        mType = type;
    }

    int getType() const
    {
        return mType;
    }

    bool isTypeValid() const
    {
        return mType != -1;
    }

    static UncorePmu *find(const char * const name);

private:
    static UncorePmu *mHead;
    UncorePmu * const mNext;
    const char * const mCoreName;
    const char * const mPmncName;
    const int mPmncCounters;
    const bool mHasCyclesCounter;
    int mType;
};

class SharedData
{
public:
    SharedData();

    int mCpuIds[NR_CPUS];
    int mClusterIds[NR_CPUS];
    const GatorCpu *mClusters[CLUSTER_COUNT];
    int mClusterCount;
    size_t mMaliUtgardCountersSize;
    char mMaliUtgardCounters[1 << 12];
    size_t mMaliMidgardCountersSize;
    char mMaliMidgardCounters[1 << 13];
    bool mClustersAccurate;

private:
    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(SharedData);
};

class SessionData
{
public:
    static const size_t MAX_STRING_LEN = 80;

    SessionData();
    ~SessionData();

    void initialize();
    void parseSessionXML(char* xmlString);
    void readModel();
    void readCpuInfo();
    void updateClusterIds();

    std::unique_ptr<PrimarySourceProvider> mPrimarySource;
    SharedData *mSharedData;
    MaliVideoDriver mMaliVideo;
    mali_userspace::MaliHwCntrDriver mMaliHwCntrs;
    MidgardDriver mMidgard;
    // Intentionally above FtraceDriver as drivers are initialized in reverse order AtraceDriver and TtraceDriver references FtraceDriver
    AtraceDriver mAtraceDriver;
    TtraceDriver mTtraceDriver;
    FtraceDriver mFtraceDriver;
    ExternalDriver mExternalDriver;
    CCNDriver mCcnDriver;

    char mCoreName[MAX_STRING_LEN];
    std::list<std::string> mImages;
    char *mConfigurationXMLPath;
    char *mSessionXMLPath;
    char *mEventsXMLPath;
    char *mEventsXMLAppend;
    char *mTargetPath;
    char *mAPCDir;
    char *mCaptureWorkingDir;
    char *mCaptureCommand;
    char *mCaptureUser;

    bool mWaitingOnCommand;
    bool mSessionIsActive;
    bool mLocalCapture;
    // halt processing of the driver data until profiling is complete or the buffer is filled
    bool mOneShot;
    bool mIsEBS;
    bool mSentSummary;
    bool mAllowCommands;
    bool mFtraceRaw;
    int mAndroidApiLevel;

    int64_t mMonotonicStarted;
    int mBacktraceDepth;
    // number of MB to use for the entire collection buffer
    int mTotalBufferSize;
    int mSampleRate;
    int64_t mLiveRate;
    int mDuration;
    int mCores;
    int mPageSize;
    int mMaxCpuId;
    int mAnnotateStart;

    // PMU Counters
    char *mCountersError;
    Counter mCounters[MAX_PERFORMANCE_COUNTERS];

private:
    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(SessionData);
};

extern SessionData gSessionData;
extern const char * const gSrcMd5;

uint64_t getTime();
int getEventKey();
int pipe_cloexec(int pipefd[2]);
FILE *fopen_cloexec(const char *path, const char *mode);
bool setNonblock(const int fd);
bool writeAll(const int fd, const void * const buf, const size_t pos);
bool readAll(const int fd, void * const buf, const size_t count);
void logCpuNotFound();

// From include/generated/uapi/linux/version.h
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))

bool getLinuxVersion(int version[3]);

const char *mxmlWhitespaceCB(mxml_node_t *node, int where);

void copyMxmlElementAttrs(mxml_node_t *dest, mxml_node_t *src);

#endif // SESSION_DATA_H
