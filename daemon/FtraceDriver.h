/**
 * Copyright (C) ARM Limited 2014-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FTRACEDRIVER_H
#define FTRACEDRIVER_H

#include <pthread.h>

#include "Driver.h"

class DynBuf;

// The Android NDK doesn't provide an implementation of pthread_barrier_t, so implement our own
class Barrier {
public:
	Barrier();
	~Barrier();

	void init(unsigned int count);
	void wait();

private:
	pthread_mutex_t mMutex;
	pthread_cond_t mCond;
	unsigned int mCount;
};

class FtraceDriver : public SimpleDriver {
public:
	FtraceDriver();
	~FtraceDriver();

	void readEvents(mxml_node_t *const xml);

	bool prepare(int *const ftraceFds);
	void start();
	void stop(int *const ftraceFds);
	bool readTracepointFormats(const uint64_t currTime, Buffer *const buffer, DynBuf *const printb, DynBuf *const b);

	bool isSupported() const { return mSupported; }

private:
	int64_t *mValues;
	Barrier mBarrier;
	int mSupported : 1,
	  mMonotonicRawSupport : 1,
	  mUnused0 : 30;
	int mTracingOn;

	// Intentionally unimplemented
	FtraceDriver(const FtraceDriver &);
	FtraceDriver &operator=(const FtraceDriver &);
};

#endif // FTRACEDRIVER_H
