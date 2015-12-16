/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SESSION_DATA_H
#define SESSION_DATA_H

#include <stdint.h>

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

#define PROTOCOL_VERSION 231
// Differentiates development versions (timestamp) from release versions
#define PROTOCOL_DEV 10000000

#define NS_PER_S 1000000000LL
#define NS_PER_MS 1000000LL
#define NS_PER_US 1000LL

extern const char MALI_GRAPHICS[];
extern const size_t MALI_GRAPHICS_SIZE;

struct ImageLinkList {
	char* path;
	struct ImageLinkList *next;
};

class GatorCpu {
public:
	GatorCpu(const char *const coreName, const char *const pmncName, const char *const dtName, const int cpuid, const int pmncCounters);

	static GatorCpu *getHead() {
		return mHead;
	}

	GatorCpu *getNext() const {
		return mNext;
	}

	const char *getCoreName() const {
		return mCoreName;
	}

	const char *getPmncName() const {
		return mPmncName;
	}

	const char *getDtName() const {
		return mDtName;
	}

	int getCpuid() const {
		return mCpuid;
	}

	int getPmncCounters() const {
		return mPmncCounters;
	}

	static GatorCpu *find(const char *const name);

	static GatorCpu *find(const int cpuid);

private:
	static GatorCpu *mHead;
	GatorCpu *const mNext;
	const char *const mCoreName;
	const char *const mPmncName;
	const char *const mDtName;
	const int mCpuid;
	const int mPmncCounters;
};

class UncorePmu {
public:
	UncorePmu(const char *const coreName, const char *const pmncName, const int pmncCounters, const bool hasCyclesCounter);

	static UncorePmu *getHead() {
		return mHead;
	}

	UncorePmu *getNext() const {
		return mNext;
	}

	const char *getCoreName() const {
		return mCoreName;
	}

	const char *getPmncName() const {
		return mPmncName;
	}

	int getPmncCounters() const {
		return mPmncCounters;
	}

	bool getHasCyclesCounter() const {
		return mHasCyclesCounter;
	}

	static UncorePmu *find(const char *const name);

private:
	static UncorePmu *mHead;
	UncorePmu *const mNext;
	const char *const mCoreName;
	const char *const mPmncName;
	const int mPmncCounters;
	const bool mHasCyclesCounter;
};

class SharedData {
public:
	SharedData();

	int mCpuIds[NR_CPUS];
	size_t mMaliUtgardCountersSize;
	char mMaliUtgardCounters[1<<12];
	size_t mMaliMidgardCountersSize;
	char mMaliMidgardCounters[1<<13];

private:
	// Intentionally unimplemented
	SharedData(const SharedData &);
	SharedData &operator=(const SharedData &);
};

class SessionData {
public:
	static const size_t MAX_STRING_LEN = 80;

	SessionData();
	~SessionData();

	void initialize();
	void parseSessionXML(char* xmlString);
	void readModel();
	void readCpuInfo();

	SharedData *mSharedData;

	PolledDriver *mUsDrivers[5];
	KMod mKmod;
	PerfDriver mPerf;
	MaliVideoDriver mMaliVideo;
	MidgardDriver mMidgard;
	// Intentionally above FtraceDriver as drivers are initialized in reverse order AtraceDriver and TtraceDriver references FtraceDriver
	AtraceDriver mAtraceDriver;
	TtraceDriver mTtraceDriver;
	FtraceDriver mFtraceDriver;
	ExternalDriver mExternalDriver;
	CCNDriver mCcnDriver;

	char mCoreName[MAX_STRING_LEN];
	struct ImageLinkList *mImages;
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
	SessionData(const SessionData &);
	SessionData &operator=(const SessionData &);
};

extern SessionData gSessionData;
extern const char *const gSrcMd5;

uint64_t getTime();
int getEventKey();
int pipe_cloexec(int pipefd[2]);
FILE *fopen_cloexec(const char *path, const char *mode);
bool setNonblock(const int fd);
bool writeAll(const int fd, const void *const buf, const size_t pos);
bool readAll(const int fd, void *const buf, const size_t count);
void logCpuNotFound();

// From include/generated/uapi/linux/version.h
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))

bool getLinuxVersion(int version[3]);

#endif // SESSION_DATA_H
