/**
 * Copyright (C) ARM Limited 2014-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "FtraceDriver.h"

#include <dirent.h>
#include <fcntl.h>
#include <regex.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "Buffer.h"
#include "Config.h"
#include "DriverSource.h"
#include "Logging.h"
#include "Proc.h"
#include "SessionData.h"

Barrier::Barrier() : mCount(0) {
	pthread_mutex_init(&mMutex, NULL);
	pthread_cond_init(&mCond, NULL);
}

Barrier::~Barrier() {
	pthread_cond_destroy(&mCond);
	pthread_mutex_destroy(&mMutex);
}

void Barrier::init(unsigned int count) {
	mCount = count;
}

void Barrier::wait() {
  pthread_mutex_lock(&mMutex);

	mCount--;
  if (mCount == 0) {
    pthread_cond_broadcast(&mCond);
  } else {
		// Loop in case of spurious wakeups
		for (;;) {
			pthread_cond_wait(&mCond, &mMutex);
			if (mCount <= 0) {
				break;
			}
		}
	}

  pthread_mutex_unlock(&mMutex);
}

class FtraceCounter : public DriverCounter {
public:
	FtraceCounter(DriverCounter *next, char *name, const char *enable);
	~FtraceCounter();

	bool readTracepointFormat(const uint64_t currTime, Buffer *const buffer, DynBuf *const printb, DynBuf *const b);

	void prepare();
	void stop();

private:
	char *const mEnable;
	int mWasEnabled;

	// Intentionally unimplemented
	FtraceCounter(const FtraceCounter &);
	FtraceCounter &operator=(const FtraceCounter &);
};

FtraceCounter::FtraceCounter(DriverCounter *next, char *name, const char *enable) : DriverCounter(next, name), mEnable(enable == NULL ? NULL : strdup(enable)) {
}

FtraceCounter::~FtraceCounter() {
	if (mEnable != NULL) {
		free(mEnable);
	}
}

void FtraceCounter::prepare() {
	if (mEnable == NULL) {
		if (gSessionData.mFtraceRaw) {
			logg.logError("The ftrace counter %s is not compatible with the more efficient ftrace collection as it is missing the enable attribute. Please either add the enable attribute to the counter in events XML or disable the counter in counter configuration.", getName());
			handleException();
		}
		return;
	}

	char buf[1<<10];
	snprintf(buf, sizeof(buf), EVENTS_PATH "/%s/enable", mEnable);
	if ((DriverSource::readIntDriver(buf, &mWasEnabled) != 0) ||
			(DriverSource::writeDriver(buf, 1) != 0)) {
		logg.logError("Unable to read or write to %s", buf);
		handleException();
	}
}

void FtraceCounter::stop() {
	if (mEnable == NULL) {
		return;
	}

	char buf[1<<10];
	snprintf(buf, sizeof(buf), EVENTS_PATH "/%s/enable", mEnable);
	DriverSource::writeDriver(buf, mWasEnabled);
}

bool FtraceCounter::readTracepointFormat(const uint64_t currTime, Buffer *const buffer, DynBuf *const printb, DynBuf *const b) {
	return ::readTracepointFormat(currTime, buffer, mEnable, printb, b);
}

static void handlerUsr1(int signum)
{
	(void)signum;

	// Although this signal handler does nothing, SIG_IGN doesn't interrupt splice in all cases
}

static int pageSize;

class FtraceReader {
public:
	FtraceReader(Barrier *const barrier, int cpu, int tfd, int pfd0, int pfd1) : mNext(mHead), mBarrier(barrier), mCpu(cpu), mTfd(tfd), mPfd0(pfd0), mPfd1(pfd1) {
		mHead = this;
	}

	void start();
	bool interrupt();
	bool join();

	static FtraceReader *getHead() { return mHead; }
	FtraceReader *getNext() const { return mNext; }
	int getPfd0() const { return mPfd0; }

private:
	static FtraceReader *mHead;
	FtraceReader *const mNext;
	Barrier *const mBarrier;
	pthread_t mThread;
	const int mCpu;
	const int mTfd;
	const int mPfd0;
	const int mPfd1;

	static void *runStatic(void *arg);
	void run();
};

FtraceReader *FtraceReader::mHead;

void FtraceReader::start() {
	if (pthread_create(&mThread, NULL, runStatic, this) != 0) {
		logg.logError("Unable to start the ftraceReader thread");
		handleException();
	}
}

bool FtraceReader::interrupt() {
	return pthread_kill(mThread, SIGUSR1) == 0;
}

bool FtraceReader::join() {
	return pthread_join(mThread, NULL) == 0;
}

void *FtraceReader::runStatic(void *arg) {
	FtraceReader *const ftraceReader = static_cast<FtraceReader *>(arg);
	ftraceReader->run();
	return NULL;
}

#ifndef SPLICE_F_MOVE

#include <sys/syscall.h>

// Pre Android-21 does not define splice
#define SPLICE_F_MOVE 1

static ssize_t sys_splice(int fd_in, loff_t *off_in, int fd_out, loff_t *off_out, size_t len, unsigned int flags) {
	return syscall(__NR_splice, fd_in, off_in, fd_out, off_out, len, flags);
}

#define splice(fd_in, off_in, fd_out, off_out, len, flags) sys_splice(fd_in, off_in, fd_out, off_out, len, flags)

#endif

void FtraceReader::run() {
	{
		char buf[16];
		snprintf(buf, sizeof(buf), "gatord-reader%02i", mCpu);
		prctl(PR_SET_NAME, (unsigned long)&buf, 0, 0, 0);
	}

	mBarrier->wait();

	while (gSessionData.mSessionIsActive) {
		const ssize_t bytes = splice(mTfd, NULL, mPfd1, NULL, pageSize, SPLICE_F_MOVE);
		if (bytes == 0) {
			logg.logError("ftrace splice unexpectedly returned 0");
			handleException();
		} else if (bytes < 0) {
			if (errno != EINTR) {
				logg.logError("splice failed");
				handleException();
			}
		} else {
			// Can there be a short splice read?
			if (bytes != pageSize) {
				logg.logError("splice short read");
				handleException();
			}
			// Will be read by gatord-external
		}
	}

	if (!setNonblock(mTfd)) {
		logg.logError("setNonblock failed");
		handleException();
	}

	for (;;) {
		ssize_t bytes;

		bytes = splice(mTfd, NULL, mPfd1, NULL, pageSize, SPLICE_F_MOVE);
		if (bytes <= 0) {
			break;
		} else {
			// Can there be a short splice read?
			if (bytes != pageSize) {
				logg.logError("splice short read");
				handleException();
			}
			// Will be read by gatord-external
		}
	}

	{
		// Read any slop
		ssize_t bytes;
		size_t size;
		char buf[1<<16];

		if (sizeof(buf) < (size_t)pageSize) {
			logg.logError("ftrace slop buffer is too small");
			handleException();
		}
		for (;;) {
			bytes = read(mTfd, buf, sizeof(buf));
			if (bytes == 0) {
				logg.logError("ftrace read unexpectedly returned 0");
				handleException();
			} else if (bytes < 0) {
				if (errno != EAGAIN) {
					logg.logError("reading slop from ftrace failed");
					handleException();
				}
				break;
			} else {
				size = bytes;
				bytes = write(mPfd1, buf, size);
				if (bytes != (ssize_t)size) {
					logg.logError("writing slop to ftrace pipe failed");
					handleException();
				}
			}
		}
	}

	close(mTfd);
	close(mPfd1);
	// Intentionally don't close mPfd0 as it is used after this thread is exited to read the slop
}

FtraceDriver::FtraceDriver() : mValues(NULL), mSupported(false), mMonotonicRawSupport(false), mTracingOn(0) {
}

FtraceDriver::~FtraceDriver() {
	delete [] mValues;
}

void FtraceDriver::readEvents(mxml_node_t *const xml) {
	// Check the kernel version
	int release[3];
	if (!getLinuxVersion(release)) {
		logg.logError("getLinuxVersion failed");
		handleException();
	}

	// The perf clock was added in 3.10
	const int kernelVersion = KERNEL_VERSION(release[0], release[1], release[2]);
	if (kernelVersion < KERNEL_VERSION(3, 10, 0)) {
		mSupported = false;
		logg.logSetup("Ftrace Disabled\nFor full ftrace functionality please upgrade to Linux 3.10 or later. With user space gator and Linux prior to 3.10, ftrace counters with the tracepoint and arg attributes will be available.");
		return;
	}
	mMonotonicRawSupport = kernelVersion >= KERNEL_VERSION(4, 2, 0);

	// Is debugfs or tracefs available?
	if (access(TRACING_PATH, R_OK) != 0) {
		mSupported = false;
		logg.logSetup("Ftrace Disabled\nUnable to locate the tracing directory");
		return;
	}

	mSupported = true;

	mxml_node_t *node = xml;
	int count = 0;
	while (true) {
		node = mxmlFindElement(node, xml, "event", NULL, NULL, MXML_DESCEND);
		if (node == NULL) {
			break;
		}
		const char *counter = mxmlElementGetAttr(node, "counter");
		if (counter == NULL) {
			continue;
		}

		if (strncmp(counter, "ftrace_", 7) != 0) {
			continue;
		}

		const char *regex = mxmlElementGetAttr(node, "regex");
		if (regex == NULL) {
			logg.logError("The regex counter %s is missing the required regex attribute", counter);
			handleException();
		}

		const char *tracepoint = mxmlElementGetAttr(node, "tracepoint");
		const char *enable = mxmlElementGetAttr(node, "enable");
		if (enable == NULL) {
			enable = tracepoint;
		}
		if (gSessionData.mPerf.isSetup() && tracepoint != NULL) {
			logg.logMessage("Not using ftrace for counter %s", counter);
			continue;
		}
		if (enable != NULL) {
			char buf[1<<10];
			snprintf(buf, sizeof(buf), EVENTS_PATH "/%s/enable", enable);
			if (access(buf, W_OK) != 0) {
				logg.logSetup("%s Disabled\n%s was not found", counter, buf);
				continue;
			}
		}

		logg.logMessage("Using ftrace for %s", counter);
		setCounters(new FtraceCounter(getCounters(), strdup(counter), enable));
		++count;
	}

	mValues = new int64_t[2*count];
}

bool FtraceDriver::prepare(int *const ftraceFds) {
	if (gSessionData.mFtraceRaw) {
		// Don't want the performace impact of sending all formats so gator only sends it for the enabled counters. This means other counters need to be disabled
		if (DriverSource::writeDriver(TRACING_PATH "/events/enable", "0") != 0) {
			logg.logError("Unable to turn off all events");
			handleException();
		}
	}

	for (FtraceCounter *counter = static_cast<FtraceCounter *>(getCounters()); counter != NULL; counter = static_cast<FtraceCounter *>(counter->getNext())) {
		if (!counter->isEnabled()) {
			continue;
		}
		counter->prepare();
	}

	if (DriverSource::readIntDriver(TRACING_PATH "/tracing_on", &mTracingOn)) {
		logg.logError("Unable to read if ftrace is enabled");
		handleException();
	}

	if (DriverSource::writeDriver(TRACING_PATH "/tracing_on", "0") != 0) {
		logg.logError("Unable to turn ftrace off before truncating the buffer");
		handleException();
	}

	{
		int fd;
		fd = open(TRACING_PATH "/trace", O_WRONLY | O_TRUNC | O_CLOEXEC, 0666);
		if (fd < 0) {
			logg.logError("Unable truncate ftrace buffer: %s", strerror(errno));
			handleException();
		}
		close(fd);
	}

	const char *const clock = mMonotonicRawSupport ? "mono_raw" : "perf";
	if (DriverSource::writeDriver(TRACING_PATH "/trace_clock", clock) != 0) {
		logg.logError("Unable to switch ftrace to the %s clock, please ensure you are running Linux %s or later", clock, mMonotonicRawSupport ? "4.2" : "3.10");
		handleException();
	}

	if (!gSessionData.mFtraceRaw) {
		ftraceFds[0] = open(TRACING_PATH "/trace_pipe", O_RDONLY | O_CLOEXEC);
		if (ftraceFds[0] < 0) {
			logg.logError("Unable to open trace_pipe");
			handleException();
		}
		ftraceFds[1] = -1;
		return true;
	}

	struct sigaction act;
	memset(&act, 0, sizeof(act));
	act.sa_handler = handlerUsr1;
	if (sigaction(SIGUSR1, &act, NULL) != 0) {
		logg.logError("sigaction failed");
		handleException();
	}

	pageSize = sysconf(_SC_PAGESIZE);
	if (pageSize <= 0) {
		logg.logError("sysconf PAGESIZE failed");
		handleException();
	}

	mBarrier.init(gSessionData.mCores + 1);

	int cpu;
	for (cpu = 0; cpu < gSessionData.mCores; ++cpu) {
		int pfd[2];
		if (pipe2(pfd, O_CLOEXEC) != 0) {
			logg.logError("pipe2 failed, %s (%i)", strerror(errno), errno);
			handleException();
		}

		char buf[64];
		snprintf(buf, sizeof(buf), TRACING_PATH "/per_cpu/cpu%i/trace_pipe_raw", cpu);
		const int tfd = open(buf, O_RDONLY | O_CLOEXEC);
		(new FtraceReader(&mBarrier, cpu, tfd, pfd[0], pfd[1]))->start();
		ftraceFds[cpu] = pfd[0];
	}
	ftraceFds[cpu] = -1;

	return false;
}

void FtraceDriver::start() {
	if (DriverSource::writeDriver(TRACING_PATH "/tracing_on", "1") != 0) {
		logg.logError("Unable to turn ftrace on");
		handleException();
	}

	if (gSessionData.mFtraceRaw) {
		mBarrier.wait();
	}
}

void FtraceDriver::stop(int *const ftraceFds) {
	DriverSource::writeDriver(TRACING_PATH "/tracing_on", mTracingOn);

	for (FtraceCounter *counter = static_cast<FtraceCounter *>(getCounters()); counter != NULL; counter = static_cast<FtraceCounter *>(counter->getNext())) {
		if (!counter->isEnabled()) {
			continue;
		}
		counter->stop();
	}

	if (!gSessionData.mFtraceRaw) {
		ftraceFds[0] = -1;
	} else {
		int i = 0;
		for (FtraceReader *reader = FtraceReader::getHead(); reader != NULL; reader = reader->getNext(), ++i) {
			reader->interrupt();
			ftraceFds[i] = reader->getPfd0();
		}
		ftraceFds[i] = -1;
		for (FtraceReader *reader = FtraceReader::getHead(); reader != NULL; reader = reader->getNext(), ++i) {
			reader->join();
		}
	}

	// Reset back to local after joining with the reader threads as otherwise any remaining ftrace data is purged
	DriverSource::writeDriver(TRACING_PATH "/trace_clock", "local");
}

bool FtraceDriver::readTracepointFormats(const uint64_t currTime, Buffer *const buffer, DynBuf *const printb, DynBuf *const b) {
	if (!gSessionData.mFtraceRaw) {
		return true;
	}

	if (!printb->printf(EVENTS_PATH "/header_page")) {
		logg.logMessage("DynBuf::printf failed");
		return false;
	}
	if (!b->read(printb->getBuf())) {
		logg.logMessage("DynBuf::read failed");
		return false;
	}
	buffer->marshalHeaderPage(currTime, b->getBuf());

	if (!printb->printf(EVENTS_PATH "/header_event")) {
		logg.logMessage("DynBuf::printf failed");
		return false;
	}
	if (!b->read(printb->getBuf())) {
		logg.logMessage("DynBuf::read failed");
		return false;
	}
	buffer->marshalHeaderEvent(currTime, b->getBuf());

	DIR *dir = opendir(EVENTS_PATH "/ftrace");
	if (dir == NULL) {
		logg.logError("Unable to open events ftrace folder");
		handleException();
	}
	struct dirent *dirent;
	while ((dirent = readdir(dir)) != NULL) {
		if (dirent->d_name[0] == '.' || dirent->d_type != DT_DIR) {
			continue;
		}
		if (!printb->printf(EVENTS_PATH "/ftrace/%s/format", dirent->d_name)) {
			logg.logMessage("DynBuf::printf failed");
			return false;
		}
		if (!b->read(printb->getBuf())) {
			logg.logMessage("DynBuf::read failed");
			return false;
		}
		buffer->marshalFormat(currTime, b->getLength(), b->getBuf());
	}
	closedir(dir);

	for (FtraceCounter *counter = static_cast<FtraceCounter *>(getCounters()); counter != NULL; counter = static_cast<FtraceCounter *>(counter->getNext())) {
		if (!counter->isEnabled()) {
			continue;
		}
		counter->readTracepointFormat(currTime, buffer, printb, b);
	}

	return true;
}
