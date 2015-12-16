/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef EXTERNALSOURCE_H
#define EXTERNALSOURCE_H

#include <semaphore.h>

#include "Buffer.h"
#include "Monitor.h"
#include "OlySocket.h"
#include "Source.h"

// Counters from external sources like graphics drivers and annotations
class ExternalSource : public Source {
public:
	ExternalSource(sem_t *senderSem);
	~ExternalSource();

	bool prepare();
	void run();
	void interrupt();

	bool isDone();
	void write(Sender *sender);

private:
	void waitFor(const int bytes);
	void configureConnection(const int fd, const char *const handshake, size_t size);
	bool connectMidgard();
	bool connectMve();
	void connectFtrace();
	bool transfer(const uint64_t currTime, const int fd);

	sem_t mBufferSem;
	Buffer mBuffer;
	Monitor mMonitor;
	OlyServerSocket mMveStartupUds;
	OlyServerSocket mMidgardStartupUds;
	OlyServerSocket mUtgardStartupUds;
	OlyServerSocket mAnnotate;
	OlyServerSocket mAnnotateUds;
	int mInterruptFd;
	int mMidgardUds;
	int mMveUds;

	// Intentionally unimplemented
	ExternalSource(const ExternalSource &);
	ExternalSource &operator=(const ExternalSource &);
};

#endif // EXTERNALSOURCE_H
