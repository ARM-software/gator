/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <string.h>
#include "SessionData.h"
#include "SessionXML.h"
#include "Logging.h"

SessionData* gSessionData = NULL;

SessionData::SessionData() {
	initialize();
}

SessionData::~SessionData() {
}

void SessionData::initialize() {
	mWaitingOnCommand = false;
	mSessionIsActive = false;
	mLocalCapture = false;
	mOneShot = false;
	strcpy(mCoreName, "unknown");
	mConfigurationXMLPath = NULL;
	mSessionXMLPath = NULL;
	mEventsXMLPath = NULL;
	mTargetPath = NULL;
	mAPCDir = NULL;
	mSampleRate = 0;
	mDuration = 0;
	mBacktraceDepth = 0;
	mTotalBufferSize = 0;
	mCores = 1;

	initializeCounters();
}

void SessionData::initializeCounters() {
	// PMU Counters
	for (int i = 0; i < MAX_PERFORMANCE_COUNTERS; i++) {
		mPerfCounterType[i][0] = 0;
		mPerfCounterTitle[i][0] = 0;
		mPerfCounterName[i][0] = 0;
		mPerfCounterDescription[i][0] = 0;
		mPerfCounterDisplay[i][0] = 0;
		mPerfCounterUnits[i][0] = 0;
		mPerfCounterEnabled[i] = 0;
		mPerfCounterEvent[i] = 0;
		mPerfCounterColor[i] = 0;
		mPerfCounterKey[i] = 0;
		mPerfCounterCount[i] = 0;
		mPerfCounterPerCPU[i] = false;
		mPerfCounterEBSCapable[i] = false;
		mPerfCounterAverageSelection[i] = false;
	}
}

void SessionData::parseSessionXML(char* xmlString) {
	SessionXML session(xmlString);
	session.parse();

	// Set session data values
	if (strcmp(session.parameters.sample_rate, "high") == 0) {
		gSessionData->mSampleRate = 10000;
	} else if (strcmp(session.parameters.sample_rate, "normal") == 0) {
		gSessionData->mSampleRate = 1000;
	} else if (strcmp(session.parameters.sample_rate, "low") == 0) {
		gSessionData->mSampleRate = 100;
	} else if (strcmp(session.parameters.sample_rate, "none") == 0) {
		gSessionData->mSampleRate = 0;
	} else {
		logg->logError(__FILE__, __LINE__, "Invalid sample rate (%s) in session xml.", session.parameters.sample_rate);
		handleException();
	}
	gSessionData->mBacktraceDepth = session.parameters.call_stack_unwinding == true ? 128 : 0;
	gSessionData->mDuration = session.parameters.duration;

	// Determine buffer size (in MB) based on buffer mode
	gSessionData->mOneShot = true;
	if (strcmp(session.parameters.buffer_mode, "streaming") == 0) {
		gSessionData->mOneShot = false;
		gSessionData->mTotalBufferSize = 1;
	} else if (strcmp(session.parameters.buffer_mode, "small") == 0) {
		gSessionData->mTotalBufferSize = 1;
	} else if (strcmp(session.parameters.buffer_mode, "normal") == 0) {
		gSessionData->mTotalBufferSize = 4;
	} else if (strcmp(session.parameters.buffer_mode, "large") == 0) {
		gSessionData->mTotalBufferSize = 16;
	} else {
		logg->logError(__FILE__, __LINE__, "Invalid value for buffer mode in session xml.");
		handleException();
	}

	gSessionData->mImages = session.parameters.images;
}
