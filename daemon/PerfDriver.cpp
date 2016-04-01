/**
 * Copyright (C) ARM Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "PerfDriver.h"

#include <dirent.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "Buffer.h"
#include "Config.h"
#include "ConfigurationXML.h"
#include "Counter.h"
#include "DriverSource.h"
#include "DynBuf.h"
#include "Logging.h"
#include "PerfGroup.h"
#include "Proc.h"
#include "SessionData.h"

#define PERF_DEVICES "/sys/bus/event_source/devices"

#define TYPE_DERIVED ~0U

static GatorCpu gatorCpuOther("Other", "Other", NULL, 0xfffff, 6);

class PerfCounter : public DriverCounter {
public:
	PerfCounter(DriverCounter *next, const char *name, uint32_t type, uint64_t config, uint64_t sampleType, uint64_t flags, const GatorCpu *const cluster, const int count) : DriverCounter(next, name), mType(type), mConfig(config), mSampleType(sampleType), mFlags(flags), mCluster(cluster), mCount(count) {}

	~PerfCounter() {
	}

	uint32_t getType() const { return mType; }
	int getCount() const { return mCount; }
	void setCount(const int count) { mCount = count; }
	uint64_t getConfig() const { return mConfig; }
	void setConfig(const uint64_t config) { mConfig = config; }
	uint64_t getSampleType() const { return mSampleType; }
	void setSampleType(uint64_t sampleType) { mSampleType = sampleType; }
	uint64_t getFlags() const { return mFlags; }
	const GatorCpu *getCluster() const { return mCluster; }
	virtual void read(Buffer *const, const int) {}

private:
	const uint32_t mType;
	uint64_t mConfig;
	uint64_t mSampleType;
	const uint64_t mFlags;
	const GatorCpu *const mCluster;
	int mCount;

	// Intentionally undefined
	PerfCounter(const PerfCounter &);
	PerfCounter &operator=(const PerfCounter &);
};

class CPUFreqDriver : public PerfCounter {
public:
	CPUFreqDriver(DriverCounter *next, const char *name, uint64_t id, const GatorCpu *const cluster) : PerfCounter(next, name, PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_RAW, PERF_GROUP_LEADER | PERF_GROUP_PER_CPU, cluster, 1) {}

	void read(Buffer *const buffer, const int cpu) {
		if (gSessionData.mSharedData->mClusters[gSessionData.mSharedData->mClusterIds[cpu]] != getCluster()) {
			return;
		}

		char buf[64];

		snprintf(buf, sizeof(buf), "/sys/devices/system/cpu/cpu%i/cpufreq/cpuinfo_cur_freq", cpu);
		int64_t freq;
		if (DriverSource::readInt64Driver(buf, &freq) != 0) {
			freq = 0;
		}
		buffer->perfCounter(cpu, getKey(), 1000*freq);
	}

private:
	// Intentionally undefined
	CPUFreqDriver(const CPUFreqDriver &);
	CPUFreqDriver &operator=(const CPUFreqDriver &);
};

PerfDriver::PerfDriver() : mIsSetup(false), mLegacySupport(false) {
}

PerfDriver::~PerfDriver() {
}

class PerfTracepoint {
public:
	PerfTracepoint(PerfTracepoint *const next, const DriverCounter *const counter, const char *const tracepoint) : mNext(next), mCounter(counter), mTracepoint(tracepoint) {}

	PerfTracepoint *getNext() const { return mNext; }
	const DriverCounter *getCounter() const { return mCounter; }
	const char *getTracepoint() const { return mTracepoint; }

private:
	PerfTracepoint *const mNext;
	const DriverCounter *const mCounter;
	const char *const mTracepoint;

	// Intentionally undefined
	PerfTracepoint(const PerfTracepoint &);
	PerfTracepoint &operator=(const PerfTracepoint &);
};

void PerfDriver::addCpuCounters(const GatorCpu *const cpu) {
	int cluster = gSessionData.mSharedData->mClusterCount++;
	if (cluster >= ARRAY_LENGTH(gSessionData.mSharedData->mClusters)) {
		logg.logError("Too many clusters on the target, please increase CLUSTER_COUNT in Config.h");
		handleException();
	}
	gSessionData.mSharedData->mClusters[cluster] = cpu;

	int len = snprintf(NULL, 0, "%s_ccnt", cpu->getPmncName()) + 1;
	char *name = new char[len];
	snprintf(name, len, "%s_ccnt", cpu->getPmncName());
	setCounters(new PerfCounter(getCounters(), name, cpu->getType(), -1, PERF_SAMPLE_READ, PERF_GROUP_PER_CPU | PERF_GROUP_CPU, cpu, 0));

	for (int j = 0; j < cpu->getPmncCounters(); ++j) {
		len = snprintf(NULL, 0, "%s_cnt%d", cpu->getPmncName(), j) + 1;
		name = new char[len];
		snprintf(name, len, "%s_cnt%d", cpu->getPmncName(), j);
		setCounters(new PerfCounter(getCounters(), name, cpu->getType(), -1, PERF_SAMPLE_READ, PERF_GROUP_PER_CPU | PERF_GROUP_CPU, cpu, 0));
	}
}

void PerfDriver::addUncoreCounters(const char *const counterName, const int type, const int numCounters, const bool hasCyclesCounter) {
	int len;
	char *name;

	if (hasCyclesCounter) {
		len = snprintf(NULL, 0, "%s_ccnt", counterName) + 1;
		name = new char[len];
		snprintf(name, len, "%s_ccnt", counterName);
		setCounters(new PerfCounter(getCounters(), name, type, -1, PERF_SAMPLE_READ, 0, NULL, 0));
	}

	for (int j = 0; j < numCounters; ++j) {
		len = snprintf(NULL, 0, "%s_cnt%d", counterName, j) + 1;
		name = new char[len];
		snprintf(name, len, "%s_cnt%d", counterName, j);
		setCounters(new PerfCounter(getCounters(), name, type, -1, PERF_SAMPLE_READ, 0, NULL, 0));
	}
}

long long PerfDriver::getTracepointId(const char *const counter, const char *const name, DynBuf *const printb) {
	long long result = PerfDriver::getTracepointId(name, printb);
	if (result <= 0) {
		logg.logSetup("%s is disabled\n%s was not found", counter, printb->getBuf());
	}
	return result;
}

void PerfDriver::readEvents(mxml_node_t *const xml) {
	mxml_node_t *node = xml;
	DynBuf printb;

	// Only for use with perf
	if (!isSetup()) {
		return;
	}

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

		const char *tracepoint = mxmlElementGetAttr(node, "tracepoint");
		if (tracepoint == NULL) {
			const char *regex = mxmlElementGetAttr(node, "regex");
			if (regex == NULL) {
				logg.logError("The tracepoint counter %s is missing the required tracepoint attribute", counter);
				handleException();
			} else {
				logg.logMessage("Not using perf for counter %s", counter);
				continue;
			}
		}

		const char *arg = mxmlElementGetAttr(node, "arg");

		long long id = getTracepointId(counter, tracepoint, &printb);
		if (id >= 0) {
			logg.logMessage("Using perf for %s", counter);
			setCounters(new PerfCounter(getCounters(), strdup(counter), PERF_TYPE_TRACEPOINT, id, arg == NULL ? 0 : PERF_SAMPLE_RAW, PERF_GROUP_LEADER | PERF_GROUP_PER_CPU | PERF_GROUP_ALL_CLUSTERS, NULL, 1));
			mTracepoints = new PerfTracepoint(mTracepoints, getCounters(), strdup(tracepoint));
		}
	}
}

bool PerfDriver::setup() {
	// Check the kernel version
	int release[3];
	if (!getLinuxVersion(release)) {
		logg.logMessage("getLinuxVersion failed");
		return false;
	}

	const int kernelVersion = KERNEL_VERSION(release[0], release[1], release[2]);
	if (kernelVersion < KERNEL_VERSION(3, 4, 0)) {
		logg.logSetup("Unsupported kernel version\nPlease upgrade to 3.4 or later");
		return false;
	}
	mLegacySupport = kernelVersion < KERNEL_VERSION(3, 12, 0);
	mClockidSupport = kernelVersion >= KERNEL_VERSION(4, 2, 0);

	if (access(EVENTS_PATH, R_OK) != 0) {
		logg.logSetup(EVENTS_PATH " does not exist\nIs CONFIG_TRACING and CONFIG_CONTEXT_SWITCH_TRACER enabled?");
		return false;
	}

	// Add supported PMUs
	bool foundCpu = false;
	DIR *dir = opendir(PERF_DEVICES);
	if (dir == NULL) {
		logg.logMessage("opendir failed");
		return false;
	}

	struct dirent *dirent;
	while ((dirent = readdir(dir)) != NULL) {
		logg.logMessage("perf pmu: %s", dirent->d_name);
		GatorCpu *gatorCpu = GatorCpu::find(dirent->d_name);
		if (gatorCpu != NULL) {
			int type;
			char buf[256];
			snprintf(buf, sizeof(buf), PERF_DEVICES "/%s/type", dirent->d_name);
			if (DriverSource::readIntDriver(buf, &type) == 0) {
				foundCpu = true;
				logg.logMessage("Adding cpu counters for %s with type %i", gatorCpu->getCoreName(), type);
				gatorCpu->setType(type);
				addCpuCounters(gatorCpu);
				continue;
			}
		}

		UncorePmu *uncorePmu = UncorePmu::find(dirent->d_name);
		if (uncorePmu != NULL) {
			int type;
			char buf[256];
			snprintf(buf, sizeof(buf), PERF_DEVICES "/%s/type", dirent->d_name);
			if (DriverSource::readIntDriver(buf, &type) == 0) {
				logg.logMessage("Adding uncore counters for %s with type %i", uncorePmu->getCoreName(), type);
				addUncoreCounters(uncorePmu->getCoreName(), type, uncorePmu->getPmncCounters(), uncorePmu->getHasCyclesCounter());
				continue;
			}
		}
	}
	closedir(dir);

	if (!foundCpu) {
		GatorCpu *gatorCpu = GatorCpu::find(gSessionData.mMaxCpuId);
		if (gatorCpu != NULL) {
			foundCpu = true;
			logg.logMessage("Adding cpu counters (based on cpuid) for %s", gatorCpu->getCoreName());
			gatorCpu->setType(PERF_TYPE_RAW);
			addCpuCounters(gatorCpu);
		}
	}

	if (!foundCpu) {
		logCpuNotFound();
#if defined(__arm__) || defined(__aarch64__)
		gatorCpuOther.setType(PERF_TYPE_RAW);
		addCpuCounters(&gatorCpuOther);
#endif
	}

	if (gSessionData.mSharedData->mClusterCount  == 0) {
		gSessionData.mSharedData->mClusters[gSessionData.mSharedData->mClusterCount++] = &gatorCpuOther;
	}
	// Reread cpuinfo so that cluster data is recalculated
	gSessionData.readCpuInfo();

	// Add supported software counters
	long long id;
	DynBuf printb;
	char buf[40];

	id = getTracepointId("Interrupts: SoftIRQ", "irq/softirq_exit", &printb);
	if (id >= 0) {
		for (int cluster = 0; cluster < gSessionData.mSharedData->mClusterCount; ++cluster) {
			snprintf(buf, sizeof(buf), "%s_softirq", gSessionData.mSharedData->mClusters[cluster]->getPmncName());
			setCounters(new PerfCounter(getCounters(), strdup(buf), PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_READ, PERF_GROUP_PER_CPU | PERF_GROUP_CPU, gSessionData.mSharedData->mClusters[cluster], 0));
		}
	}

	id = getTracepointId("Interrupts: IRQ", "irq/irq_handler_exit", &printb);
	if (id >= 0) {
		for (int cluster = 0; cluster < gSessionData.mSharedData->mClusterCount; ++cluster) {
			snprintf(buf, sizeof(buf), "%s_irq", gSessionData.mSharedData->mClusters[cluster]->getPmncName());
			setCounters(new PerfCounter(getCounters(), strdup(buf), PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_READ, PERF_GROUP_PER_CPU | PERF_GROUP_CPU, gSessionData.mSharedData->mClusters[cluster], 0));
		}
	}

	id = getTracepointId("Scheduler: Switch", SCHED_SWITCH, &printb);
	if (id >= 0) {
		for (int cluster = 0; cluster < gSessionData.mSharedData->mClusterCount; ++cluster) {
			snprintf(buf, sizeof(buf), "%s_switch", gSessionData.mSharedData->mClusters[cluster]->getPmncName());
			setCounters(new PerfCounter(getCounters(), strdup(buf), PERF_TYPE_TRACEPOINT, id, PERF_SAMPLE_READ, PERF_GROUP_PER_CPU | PERF_GROUP_CPU, gSessionData.mSharedData->mClusters[cluster], 0));
		}
	}

	id = getTracepointId("Clock: Frequency", CPU_FREQUENCY, &printb);
	if (id >= 0 && access("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq", R_OK) == 0) {
		for (int cluster = 0; cluster < gSessionData.mSharedData->mClusterCount; ++cluster) {
			snprintf(buf, sizeof(buf), "%s_freq", gSessionData.mSharedData->mClusters[cluster]->getPmncName());
			setCounters(new CPUFreqDriver(getCounters(), strdup(buf), id, gSessionData.mSharedData->mClusters[cluster]));
		}
	}

	setCounters(new PerfCounter(getCounters(), strdup("Linux_cpu_wait_contention"), TYPE_DERIVED, -1, 0, 0, NULL, 0));
	for (int cluster = 0; cluster < gSessionData.mSharedData->mClusterCount; ++cluster) {
		snprintf(buf, sizeof(buf), "%s_system", gSessionData.mSharedData->mClusters[cluster]->getPmncName());
		setCounters(new PerfCounter(getCounters(), strdup(buf), TYPE_DERIVED, -1, 0, 0, NULL, 0));
		snprintf(buf, sizeof(buf), "%s_user", gSessionData.mSharedData->mClusters[cluster]->getPmncName());
		setCounters(new PerfCounter(getCounters(), strdup(buf), TYPE_DERIVED, -1, 0, 0, NULL, 0));
	}

	mIsSetup = true;
	return true;
}

void logCpuNotFound() {
#if defined(__arm__) || defined(__aarch64__)
		logg.logSetup("CPU is not recognized\nUsing the ARM architected counters");
#else
		logg.logSetup("CPU is not recognized\nOmitting CPU counters");
#endif
}

bool PerfDriver::summary(Buffer *const buffer) {
	struct utsname utsname;
	if (uname(&utsname) != 0) {
		logg.logMessage("uname failed");
		return false;
	}

	char buf[512];
	snprintf(buf, sizeof(buf), "%s %s %s %s %s GNU/Linux", utsname.sysname, utsname.nodename, utsname.release, utsname.version, utsname.machine);

	long pageSize = sysconf(_SC_PAGESIZE);
	if (pageSize < 0) {
		logg.logMessage("sysconf _SC_PAGESIZE failed");
		return false;
	}

	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
		logg.logMessage("clock_gettime failed");
		return false;
	}
	const int64_t timestamp = (int64_t)ts.tv_sec * NS_PER_S + ts.tv_nsec;

	const uint64_t monotonicStarted = getTime();
	gSessionData.mMonotonicStarted = monotonicStarted;
	const uint64_t currTime = 0;//getTime() - gSessionData.mMonotonicStarted;

	buffer->summary(currTime, timestamp, monotonicStarted, monotonicStarted, buf, pageSize, getClockidSupport());

	for (int i = 0; i < gSessionData.mCores; ++i) {
		coreName(currTime, buffer, i);
	}
	buffer->commit(currTime);

	return true;
}

void PerfDriver::coreName(const uint64_t currTime, Buffer *const buffer, const int cpu) {
	const SharedData *const sharedData = gSessionData.mSharedData;
	// Don't send information on a cpu we know nothing about
	if (sharedData->mCpuIds[cpu] == -1) {
		return;
	}

	GatorCpu *gatorCpu = GatorCpu::find(sharedData->mCpuIds[cpu]);
	if (gatorCpu != NULL && gatorCpu->getCpuid() == sharedData->mCpuIds[cpu]) {
		buffer->coreName(currTime, cpu, sharedData->mCpuIds[cpu], gatorCpu->getCoreName());
	} else {
		char buf[32];
		if (sharedData->mCpuIds[cpu] == -1) {
			snprintf(buf, sizeof(buf), "Unknown");
		} else {
			snprintf(buf, sizeof(buf), "Unknown (0x%.3x)", sharedData->mCpuIds[cpu]);
		}
		buffer->coreName(currTime, cpu, sharedData->mCpuIds[cpu], buf);
	}
}

void PerfDriver::setupCounter(Counter &counter) {
	PerfCounter *const perfCounter = static_cast<PerfCounter *>(findCounter(counter));
	if (perfCounter == NULL) {
		counter.setEnabled(false);
		return;
	}

	// Don't use the config from counters XML if it's not set, ex: software counters
	if (counter.getEvent() != -1) {
		perfCounter->setConfig(counter.getEvent());
	}
	if (counter.getCount() > 0) {
		// EBS
		perfCounter->setCount(counter.getCount());
		// Collect samples
		perfCounter->setSampleType(perfCounter->getSampleType() | PERF_SAMPLE_TID | PERF_SAMPLE_IP);
	}
	perfCounter->setEnabled(true);
	counter.setKey(perfCounter->getKey());
}

bool PerfDriver::enable(const uint64_t currTime, PerfGroup *const group, Buffer *const buffer) const {
	for (PerfCounter *counter = static_cast<PerfCounter *>(getCounters()); counter != NULL; counter = static_cast<PerfCounter *>(counter->getNext())) {
		if (counter->isEnabled() && (counter->getType() != TYPE_DERIVED) &&
				!group->add(currTime, buffer, counter->getKey(), counter->getType(), counter->getConfig(), counter->getCount(), counter->getSampleType(), counter->getFlags(), counter->getCluster())) {
			logg.logMessage("PerfGroup::add failed");
			return false;
		}
	}

	return true;
}

void PerfDriver::read(Buffer *const buffer, const int cpu) {
	for (PerfCounter *counter = static_cast<PerfCounter *>(getCounters()); counter != NULL; counter = static_cast<PerfCounter *>(counter->getNext())) {
		if (!counter->isEnabled()) {
			continue;
		}
		counter->read(buffer, cpu);
	}
}

bool PerfDriver::sendTracepointFormats(const uint64_t currTime, Buffer *const buffer, DynBuf *const printb, DynBuf *const b) {
	if (
		!readTracepointFormat(currTime, buffer, SCHED_SWITCH, printb, b) ||
		!readTracepointFormat(currTime, buffer, CPU_IDLE, printb, b) ||
		!readTracepointFormat(currTime, buffer, CPU_FREQUENCY, printb, b) ||
		false) {
		return false;
	}

	for (PerfTracepoint *tracepoint = mTracepoints; tracepoint != NULL; tracepoint = tracepoint->getNext()) {
		if (tracepoint->getCounter()->isEnabled() && !readTracepointFormat(currTime, buffer, tracepoint->getTracepoint(), printb, b)) {
			return false;
		}
	}

	return true;
}

long long PerfDriver::getTracepointId(const char *const name, DynBuf *const printb) {
	if (!printb->printf(EVENTS_PATH "/%s/id", name)) {
		logg.logMessage("DynBuf::printf failed");
		return -1;
	}

	int64_t result;
	if (DriverSource::readInt64Driver(printb->getBuf(), &result) != 0) {
		logg.logMessage("Unable to read tracepoint id for %s", printb->getBuf());
		return -1;
	}

	return result;
}
