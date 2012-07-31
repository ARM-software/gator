/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SESSION_DATA_H
#define SESSION_DATA_H

#define MAX_PERFORMANCE_COUNTERS	50
#define MAX_STRING_LEN				80
#define MAX_DESCRIPTION_LEN			400

#define PROTOCOL_VERSION	10
#define PROTOCOL_DEV		1000	// Differentiates development versions (timestamp) from release versions

struct ImageLinkList {
	char* path;
	struct ImageLinkList *next;
};

class SessionData {
public:
	SessionData();
	~SessionData();
	void initialize();
	void initializeCounters();
	void parseSessionXML(char* xmlString);

	char mCoreName[MAX_STRING_LEN];
	struct ImageLinkList *mImages;
	char* mConfigurationXMLPath;
	char* mSessionXMLPath;
	char* mEventsXMLPath;
	char* mTargetPath;
	char* mAPCDir;
	char* mTitle;

	bool mWaitingOnCommand;
	bool mSessionIsActive;
	bool mLocalCapture;
	bool mOneShot;		// halt processing of the driver data until profiling is complete or the buffer is filled
	
	int mBacktraceDepth;
	int mTotalBufferSize;	// number of MB to use for the entire collection buffer
	int mSampleRate;
	int mDuration;
	int mCores;
	int mBytes;

	// PMU Counters
	char mPerfCounterType[MAX_PERFORMANCE_COUNTERS][MAX_STRING_LEN];
	char mPerfCounterTitle[MAX_PERFORMANCE_COUNTERS][MAX_STRING_LEN];
	char mPerfCounterName[MAX_PERFORMANCE_COUNTERS][MAX_STRING_LEN];
	char mPerfCounterDescription[MAX_PERFORMANCE_COUNTERS][MAX_DESCRIPTION_LEN];
	char mPerfCounterOperation[MAX_PERFORMANCE_COUNTERS][MAX_STRING_LEN];
	char mPerfCounterAlias[MAX_PERFORMANCE_COUNTERS][MAX_STRING_LEN];
	char mPerfCounterDisplay[MAX_PERFORMANCE_COUNTERS][MAX_STRING_LEN];
	char mPerfCounterUnits[MAX_PERFORMANCE_COUNTERS][MAX_STRING_LEN];
	int mPerfCounterEnabled[MAX_PERFORMANCE_COUNTERS];
	int mPerfCounterEvent[MAX_PERFORMANCE_COUNTERS];
	int mPerfCounterColor[MAX_PERFORMANCE_COUNTERS];
	int mPerfCounterCount[MAX_PERFORMANCE_COUNTERS];
	int mPerfCounterKey[MAX_PERFORMANCE_COUNTERS];
	bool mPerfCounterPerCPU[MAX_PERFORMANCE_COUNTERS];
	bool mPerfCounterEBSCapable[MAX_PERFORMANCE_COUNTERS];
	bool mPerfCounterLevel[MAX_PERFORMANCE_COUNTERS];
	bool mPerfCounterAverageSelection[MAX_PERFORMANCE_COUNTERS];
};

extern SessionData* gSessionData;

#endif // SESSION_DATA_H
