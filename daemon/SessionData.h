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
#include "PerfDriver.h"

#define PROTOCOL_VERSION 22
// Differentiates development versions (timestamp) from release versions
#define PROTOCOL_DEV 1000

#define NS_PER_S 1000000000LL
#define NS_PER_MS 1000000LL
#define NS_PER_US 1000LL

struct ImageLinkList {
	char* path;
	struct ImageLinkList *next;
};

class SharedData {
public:
	SharedData();

	int mCpuIds[NR_CPUS];
	size_t mMaliUtgardCountersSize;
	char mMaliUtgardCounters[1<<12];

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
	void parseSessionXML(char* xmlString);
	void readModel();
	void readCpuInfo();

	SharedData *mSharedData;

	PolledDriver *mUsDrivers[5];
	KMod mKmod;
	PerfDriver mPerf;
	MaliVideoDriver mMaliVideo;
	// Intentionally above FtraceDriver as drivers are initialized in reverse order AtraceDriver references AtraceDriver
	AtraceDriver mAtraceDriver;
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
	void initialize();

	// Intentionally unimplemented
	SessionData(const SessionData &);
	SessionData &operator=(const SessionData &);
};

extern SessionData* gSessionData;
extern const char *const gSrcMd5;

uint64_t getTime();
int getEventKey();
int pipe_cloexec(int pipefd[2]);
FILE *fopen_cloexec(const char *path, const char *mode);
bool setNonblock(const int fd);
bool writeAll(const int fd, const void *const buf, const size_t pos);
bool readAll(const int fd, void *const buf, const size_t count);

#endif // SESSION_DATA_H
