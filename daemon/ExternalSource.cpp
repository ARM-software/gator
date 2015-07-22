/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "ExternalSource.h"

#include <fcntl.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "Child.h"
#include "DriverSource.h"
#include "Logging.h"
#include "OlySocket.h"
#include "SessionData.h"

extern Child *child;

static const char STREAMLINE_ANNOTATE[] = "\0streamline-annotate";
static const char MALI_VIDEO[] = "\0mali-video";
static const char MALI_VIDEO_STARTUP[] = "\0mali-video-startup";
static const char MALI_VIDEO_V1[] = "MALI_VIDEO 1\n";
static const char MALI_GRAPHICS[] = "\0mali_thirdparty_server";
static const char MALI_GRAPHICS_STARTUP[] = "\0mali_thirdparty_client";
static const char MALI_GRAPHICS_V1[] = "MALI_GRAPHICS 1\n";
static const char MALI_UTGARD_STARTUP[] = "\0mali-utgard-startup";
static const char FTRACE_V1[] = "FTRACE 1\n";

ExternalSource::ExternalSource(sem_t *senderSem) : mBuffer(0, FRAME_EXTERNAL, 128*1024, senderSem), mMonitor(), mMveStartupUds(MALI_VIDEO_STARTUP, sizeof(MALI_VIDEO_STARTUP)), mMaliStartupUds(MALI_GRAPHICS_STARTUP, sizeof(MALI_GRAPHICS_STARTUP)), mUtgardStartupUds(MALI_UTGARD_STARTUP, sizeof(MALI_UTGARD_STARTUP)), mAnnotate(8083), mAnnotateUds(STREAMLINE_ANNOTATE, sizeof(STREAMLINE_ANNOTATE), true), mInterruptFd(-1), mMaliUds(-1), mMveUds(-1), mFtraceFd(-1) {
	sem_init(&mBufferSem, 0, 0);
}

ExternalSource::~ExternalSource() {
}

void ExternalSource::waitFor(const int bytes) {
	while (mBuffer.bytesAvailable() <= bytes) {
		if (gSessionData->mOneShot && gSessionData->mSessionIsActive) {
			logg->logMessage("One shot (external)");
			child->endSession();
		}
		sem_wait(&mBufferSem);
	}
}

void ExternalSource::configureConnection(const int fd, const char *const handshake, size_t size) {
	if (!setNonblock(fd)) {
		logg->logError("Unable to set nonblock on fh");
		handleException();
	}

	if (!mMonitor.add(fd)) {
		logg->logError("Unable to add fh to monitor");
		handleException();
	}

	// Write the handshake to the circular buffer
	waitFor(Buffer::MAXSIZE_PACK32 + size - 1);
	mBuffer.packInt(fd);
	mBuffer.writeBytes(handshake, size - 1);
	mBuffer.commit(1, true);
}

bool ExternalSource::connectMali() {
	mMaliUds = OlySocket::connect(MALI_GRAPHICS, sizeof(MALI_GRAPHICS));
	if (mMaliUds < 0) {
		return false;
	}

	configureConnection(mMaliUds, MALI_GRAPHICS_V1, sizeof(MALI_GRAPHICS_V1));

	return true;
}

bool ExternalSource::connectMve() {
	if (!gSessionData->mMaliVideo.countersEnabled()) {
		return true;
	}

	mMveUds = OlySocket::connect(MALI_VIDEO, sizeof(MALI_VIDEO));
	if (mMveUds < 0) {
		return false;
	}

	if (!gSessionData->mMaliVideo.start(mMveUds)) {
		return false;
	}

	configureConnection(mMveUds, MALI_VIDEO_V1, sizeof(MALI_VIDEO_V1));

	return true;
}

void ExternalSource::connectFtrace() {
	if (!gSessionData->mFtraceDriver.isSupported()) {
		return;
	}

	gSessionData->mFtraceDriver.prepare();

	mFtraceFd = open(TRACING_PATH "/trace_pipe", O_RDONLY | O_CLOEXEC);
	if (mFtraceFd < 0) {
		logg->logError("Unable to open trace_pipe");
		handleException();
	}

	configureConnection(mFtraceFd, FTRACE_V1, sizeof(FTRACE_V1));
}

bool ExternalSource::prepare() {
	if (!mMonitor.init() ||
			!setNonblock(mMveStartupUds.getFd()) || !mMonitor.add(mMveStartupUds.getFd()) ||
			!setNonblock(mMaliStartupUds.getFd()) || !mMonitor.add(mMaliStartupUds.getFd()) ||
			!setNonblock(mUtgardStartupUds.getFd()) || !mMonitor.add(mUtgardStartupUds.getFd()) ||
			!setNonblock(mAnnotate.getFd()) || !mMonitor.add(mAnnotate.getFd()) ||
			!setNonblock(mAnnotateUds.getFd()) || !mMonitor.add(mAnnotateUds.getFd()) ||
			false) {
		return false;
	}

	connectMali();
	connectMve();
	connectFtrace();
	gSessionData->mExternalDriver.start();

	return true;
}

void ExternalSource::run() {
	int pipefd[2];

	prctl(PR_SET_NAME, (unsigned long)&"gatord-external", 0, 0, 0);

	if (pipe_cloexec(pipefd) != 0) {
		logg->logError("pipe failed");
		handleException();
	}
	mInterruptFd = pipefd[1];

	if (!mMonitor.add(pipefd[0])) {
		logg->logError("Monitor::add failed");
		handleException();
	}

	// Notify annotate clients to retry connecting to gatord
	uint64_t val = 1;
	if (::write(gSessionData->mAnnotateStart, &val, sizeof(val)) != sizeof(val)) {
		logg->logMessage("Writing to annotate pipe failed");
	}

	if (mFtraceFd >= 0) {
		gSessionData->mAtraceDriver.start();

		if (DriverSource::writeDriver(TRACING_PATH "/tracing_on", "1") != 0) {
			logg->logError("Unable to turn ftrace on");
			handleException();
		}
	}

	// Wait until monotonicStarted is set before sending data
	int64_t monotonicStarted = 0;
	while (monotonicStarted <= 0 && gSessionData->mSessionIsActive) {
		usleep(10);

		if (gSessionData->mPerf.isSetup()) {
			monotonicStarted = gSessionData->mMonotonicStarted;
		} else {
			if (DriverSource::readInt64Driver("/dev/gator/started", &monotonicStarted) == -1) {
				logg->logError("Error reading gator driver start time");
				handleException();
			}
		}
	}

	while (gSessionData->mSessionIsActive) {
		struct epoll_event events[16];
		// Clear any pending sem posts
		while (sem_trywait(&mBufferSem) == 0);
		int ready = mMonitor.wait(events, ARRAY_LENGTH(events), -1);
		if (ready < 0) {
			logg->logError("Monitor::wait failed");
			handleException();
		}

		const uint64_t currTime = getTime() - gSessionData->mMonotonicStarted;

		for (int i = 0; i < ready; ++i) {
			const int fd = events[i].data.fd;
			if (fd == mMveStartupUds.getFd()) {
				// Mali Video Engine says it's alive
				int client = mMveStartupUds.acceptConnection();
				// Don't read from this connection, establish a new connection to Mali-V500
				close(client);
				if (!connectMve()) {
					logg->logError("Unable to configure incoming Mali video connection");
					handleException();
				}
			} else if (fd == mMaliStartupUds.getFd()) {
				// Mali Graphics says it's alive
				int client = mMaliStartupUds.acceptConnection();
				// Don't read from this connection, establish a new connection to Mali Graphics
				close(client);
				if (!connectMali()) {
					logg->logError("Unable to configure incoming Mali graphics connection");
					handleException();
				}
			} else if (fd == mUtgardStartupUds.getFd()) {
				// Mali Utgard says it's alive
				int client = mUtgardStartupUds.acceptConnection();
				// Don't read from this connection, configure utgard and expect them to reconnect with annotations
				close(client);
				gSessionData->mExternalDriver.disconnect();
				gSessionData->mExternalDriver.start();
			} else if (fd == mAnnotate.getFd()) {
				int client = mAnnotate.acceptConnection();
				if (!setNonblock(client) || !mMonitor.add(client)) {
					logg->logError("Unable to set socket options on incoming annotation connection");
					handleException();
				}
			} else if (fd == mAnnotateUds.getFd()) {
				int client = mAnnotateUds.acceptConnection();
				if (!setNonblock(client) || !mMonitor.add(client)) {
					logg->logError("Unable to set socket options on incoming annotation connection");
					handleException();
				}
			} else if (fd == pipefd[0]) {
				// Means interrupt has been called and mSessionIsActive should be reread
			} else {
				/* This can result in some starvation if there are multiple
				 * threads which are annotating heavily, but it is not
				 * recommended that threads annotate that much as it can also
				 * starve out the gator data.
				 */
				while (gSessionData->mSessionIsActive) {
					// Wait until there is enough room for the fd, two headers and two ints
					waitFor(7*Buffer::MAXSIZE_PACK32 + 2*sizeof(uint32_t));
					mBuffer.packInt(fd);
					const int contiguous = mBuffer.contiguousSpaceAvailable();
					const int bytes = read(fd, mBuffer.getWritePos(), contiguous);
					if (bytes < 0) {
						if (errno == EAGAIN) {
							// Nothing left to read
							mBuffer.commit(currTime, true);
							break;
						}
						// Something else failed, close the socket
						mBuffer.commit(currTime, true);
						mBuffer.packInt(-1);
						mBuffer.packInt(fd);
						// Here and other commits, always force-flush the buffer as this frame don't work like others
						mBuffer.commit(currTime, true);
						close(fd);
						break;
					} else if (bytes == 0) {
						// The other side is closed
						mBuffer.commit(currTime, true);
						mBuffer.packInt(-1);
						mBuffer.packInt(fd);
						mBuffer.commit(currTime, true);
						close(fd);
						break;
					}

					mBuffer.advanceWrite(bytes);
					mBuffer.commit(currTime, true);

					// Short reads also mean nothing is left to read
					if (bytes < contiguous) {
						break;
					}
				}
			}
		}
	}

	mBuffer.setDone();

	if (mFtraceFd >= 0) {
		gSessionData->mFtraceDriver.stop();
		gSessionData->mAtraceDriver.stop();
		close(mFtraceFd);
	}

	if (mMveUds >= 0) {
		gSessionData->mMaliVideo.stop(mMveUds);
	}

	mInterruptFd = -1;
	close(pipefd[0]);
	close(pipefd[1]);
}

void ExternalSource::interrupt() {
	if (mInterruptFd >= 0) {
		int8_t c = 0;
		// Write to the pipe to wake the monitor which will cause mSessionIsActive to be reread
		if (::write(mInterruptFd, &c, sizeof(c)) != sizeof(c)) {
			logg->logError("write failed");
			handleException();
		}
	}
}

bool ExternalSource::isDone() {
	return mBuffer.isDone();
}

void ExternalSource::write(Sender *sender) {
	// Don't send external data until the summary packet is sent so that monotonic delta is available
	if (!gSessionData->mSentSummary) {
		return;
	}
	if (!mBuffer.isDone()) {
		mBuffer.write(sender);
		sem_post(&mBufferSem);
	}
}
