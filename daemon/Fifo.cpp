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

// bufferSize is the amount of data to be filled
// singleBufferSize is the maximum size that may be filled during a single write
// (bufferSize + singleBufferSize) will be allocated
Fifo::Fifo(int singleBufferSize, int bufferSize) {
	mWrite = mRead = mReadCommit = mRaggedEnd = 0;
	mWrapThreshold = bufferSize;
	mSingleBufferSize = singleBufferSize;
	mBuffer = (char*)valloc(bufferSize + singleBufferSize);
	mEnd = false;

	if (mBuffer == NULL) {
		logg->logError(__FILE__, __LINE__, "failed to allocate %d bytes", bufferSize + singleBufferSize);
		handleException();
	}

	if (sem_init(&mWaitForSpaceSem, 0, 0) || sem_init(&mWaitForDataSem, 0, 0)) {
		logg->logError(__FILE__, __LINE__, "sem_init() failed");
		handleException();
	}
}

Fifo::~Fifo() {
	free(mBuffer);
}

int Fifo::numBytesFilled() {
	return mWrite - mRead + mRaggedEnd;
}

char* Fifo::start() {
	return mBuffer;
}

bool Fifo::isEmpty() {
	return mRead == mWrite;
}

bool Fifo::isFull() {
	return willFill(0);
}

// Determines if the buffer will fill assuming 'additional' bytes will be added to the buffer
// comparisons use '<', read and write pointers must never equal when not empty
// 'full' means there is less than singleBufferSize bytes available; it does not mean there are zero bytes available
bool Fifo::willFill(int additional) {
	if (mWrite > mRead) {
		if (numBytesFilled() + additional < mWrapThreshold) {
			return false;
		}
	} else {
		if (numBytesFilled() + additional < mWrapThreshold - mSingleBufferSize) {
			return false;
		}
	}
	return true;
}

// This function will stall until contiguous singleBufferSize bytes are available
char* Fifo::write(int length) {
	if (length <= 0) {
		length = 0;
		mEnd = true;
	}

	// update the write pointer
	mWrite += length;

	// handle the wrap-around
	if (mWrite >= mWrapThreshold) {
		mRaggedEnd = mWrite;
		mWrite = 0;
	}

	// send a notification that data is ready
	sem_post(&mWaitForDataSem);

	// wait for space
	while (isFull()) {
		sem_wait(&mWaitForSpaceSem);
	}

	return &mBuffer[mWrite];
}

// This function will stall until data is available
char* Fifo::read(int* length) {
	// update the read pointer now that the data has been handled
	mRead = mReadCommit;

	// handle the wrap-around
	if (mRead >= mWrapThreshold) {
		mRaggedEnd = mRead = mReadCommit = 0;
	}

	// send a notification that data is free (space is available)
	sem_post(&mWaitForSpaceSem);

	// wait for data
	while (isEmpty() && !mEnd) {
		sem_wait(&mWaitForDataSem);
	}

	// obtain the length
	do {
		mReadCommit = mRaggedEnd ? mRaggedEnd : mWrite;
		*length = mReadCommit - mRead;
	} while (*length < 0); // plugs race condition without using semaphores

	return &mBuffer[mRead];
}
