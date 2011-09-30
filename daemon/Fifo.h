/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef	__FIFO_H__
#define	__FIFO_H__

#include <semaphore.h>

// Driver buffer size is 512k, large buffer mode with 2GB of total RAM will allocate 256M
#define FIFO_BUFFER_LIMIT	256*1024/512

class Fifo {
public:
	Fifo(int numBuffers, int bufferSize);
	~Fifo();
	int depth(void);
	int numReadToWriteBuffersFilled();
	int numWriteToReadBuffersFilled();
	int numReadToWriteBuffersEmpty() {return depth() - numReadToWriteBuffersFilled();}
	int numWriteToReadBuffersEmpty() {return depth() - numWriteToReadBuffersFilled();}
	char* start();
	char* write(int length);
	char* read(int* length);

private:
	int		mNumBuffers;
	int		mBufferSize;
	int		mWriteCurrent;
	int		mReadCurrent;
	sem_t	mReadToWriteSem[FIFO_BUFFER_LIMIT];
	sem_t	mWriteToReadSem[FIFO_BUFFER_LIMIT];
	char*	mBuffer[FIFO_BUFFER_LIMIT];
	int		mLength[FIFO_BUFFER_LIMIT];
};

#endif 	//__FIFO_H__
