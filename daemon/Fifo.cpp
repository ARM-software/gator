/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "Fifo.h"
#include "Logging.h"

extern void handleException();

Fifo::Fifo(int numBuffers, int bufferSize) {
	int	which;

	if (numBuffers > FIFO_BUFFER_LIMIT) {
		logg->logError(__FILE__, __LINE__, "Number of fifo buffers exceeds maximum");
		handleException();
	}
	mNumBuffers = numBuffers;
	mBufferSize = bufferSize;
	mWriteCurrent = 0;
	mReadCurrent = mNumBuffers - 1; // (n-1) pipelined

	for (which=0; which<mNumBuffers; which++) {
		// initialized read-to-write sem to 1, so that first wait goes through; write-to-read init'd to 0
		if (sem_init(&mReadToWriteSem[which], 0, 1) ||
			sem_init(&mWriteToReadSem[which], 0, 0)) {
			logg->logError(__FILE__, __LINE__, "sem_init(%d) failed", which);
			handleException();
		}
		// page-align allocate buffers
		mBuffer[which] = (char*)valloc(bufferSize);
		if (mBuffer[which] == NULL) {
			logg->logError(__FILE__, __LINE__, "failed to allocate %d bytes", bufferSize);
			handleException();
		}
		// touch each page to fault it in
		for (int i=0; i<bufferSize; i+= getpagesize()) {
			*mBuffer[which] = 0;
		}
	}
}

Fifo::~Fifo() {
	for (int which=0; which<mNumBuffers; which++) {
		if (mBuffer[which] != NULL) {
			free(mBuffer[which]);
			mBuffer[which] = NULL;
		}
	}
}

int Fifo::depth(void) {
	return mNumBuffers;
}

int Fifo::numReadToWriteBuffersFilled() {
	int value;
	int numFilled = 0;
	for (int which=0; which<mNumBuffers; which++) {
		if (sem_getvalue(&mReadToWriteSem[which], &value) == 0) numFilled += value;
	}
	return numFilled;
}

int Fifo::numWriteToReadBuffersFilled() {
	int value;
	int numFilled = 0;
	for (int which=0; which<mNumBuffers; which++) {
		if (sem_getvalue(&mWriteToReadSem[which], &value) == 0) numFilled += value;
	}
	return numFilled;
}

char* Fifo::start() {
	sem_wait(&mReadToWriteSem[mWriteCurrent]);
	return mBuffer[mWriteCurrent];
}

char* Fifo::write(int length) {
	mLength[mWriteCurrent] = length;
	sem_post(&mWriteToReadSem[mWriteCurrent]);
	mWriteCurrent = (mWriteCurrent + 1) % mNumBuffers;
	sem_wait(&mReadToWriteSem[mWriteCurrent]);
	return mBuffer[mWriteCurrent];
}

char* Fifo::read(int* length) {
	static bool firstTime = true;
	if (!firstTime) {
		sem_post(&mReadToWriteSem[mReadCurrent]);
	}
	firstTime = false;
	mReadCurrent = (mReadCurrent + 1) % mNumBuffers;
	sem_wait(&mWriteToReadSem[mReadCurrent]);
	*length = mLength[mReadCurrent];
	return mBuffer[mReadCurrent];
}
