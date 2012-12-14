/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef	__FIFO_H__
#define	__FIFO_H__

#include <semaphore.h>

class Fifo {
public:
	Fifo(int singleBufferSize, int totalBufferSize);
	~Fifo();
	int numBytesFilled() const;
	bool isEmpty() const;
	bool isFull() const;
	bool willFill(int additional) const;
	char* start() const;
	char* write(int length);
	char* read(int *const length);

private:
	int		mSingleBufferSize, mWrite, mRead, mReadCommit, mRaggedEnd, mWrapThreshold;
	sem_t	mWaitForSpaceSem, mWaitForDataSem;
	char*	mBuffer;
	bool	mEnd;
};

#endif 	//__FIFO_H__
