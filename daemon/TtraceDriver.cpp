/* Copyright (C) 2014-2021 by Arm Limited. All rights reserved. */

#include "TtraceDriver.h"

#include "FtraceDriver.h"
#include "Logging.h"
#include "OlyUtility.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

class TtraceCounter : public DriverCounter {
public:
    TtraceCounter(DriverCounter * next, const char * name, int flag);

    // Intentionally unimplemented
    TtraceCounter(const TtraceCounter &) = delete;
    TtraceCounter & operator=(const TtraceCounter &) = delete;
    TtraceCounter(TtraceCounter &&) = delete;
    TtraceCounter & operator=(TtraceCounter &&) = delete;

    int getFlag() const { return mFlag; }

private:
    const int mFlag;
};

TtraceCounter::TtraceCounter(DriverCounter * next, const char * name, int flag) : DriverCounter(next, name), mFlag(flag)
{
}

TtraceDriver::TtraceDriver(const FtraceDriver & ftraceDriver)
    : SimpleDriver("Ttrace"), mSupported(false), mFtraceDriver(ftraceDriver)
{
}

void TtraceDriver::readEvents(mxml_node_t * const xml)
{
    if (access("/etc/tizen-release", R_OK) != 0) {
        // Reduce warning noise
        //logg.logSetup("Ttrace is disabled\n/etc/tizen-release is not found, this is not a Tizen target");
        return;
    }
    if (!mFtraceDriver.isSupported()) {
        logg.logSetup("Ttrace is disabled\nSupport for ftrace required");
        return;
    }

    mSupported = true;

    mxml_node_t * node = xml;
    while (true) {
        node = mxmlFindElement(node, xml, "event", nullptr, nullptr, MXML_DESCEND);
        if (node == nullptr) {
            break;
        }
        const char * counter = mxmlElementGetAttr(node, "counter");
        if (counter == nullptr) {
            continue;
        }

        if (strncmp(counter, "ttrace_", 7) != 0) {
            continue;
        }

        const char * flagStr = mxmlElementGetAttr(node, "flag");
        if (flagStr == nullptr) {
            logg.logError("The ttrace counter %s is missing the required flag attribute", counter);
            handleException();
        }
        int flag;
        if (!stringToInt(&flag, flagStr, 16)) {
            logg.logError("The flag attribute of the ttrace counter %s is not a hex integer", counter);
            handleException();
        }
        setCounters(new TtraceCounter(getCounters(), counter, flag));
    }
}

void TtraceDriver::setTtrace(const int flags)
{
    logg.logMessage("Setting ttrace flags to %i", flags);

    const int fd = open("/tmp/ttrace_tag", O_CREAT | O_RDWR | O_CLOEXEC, 0666);
    if (fd < 0) {
        logg.logError("Unable to open /tmp/ttrace_tag");
        handleException();
    }
    if (ftruncate(fd, sizeof(uint64_t)) != 0) {
        logg.logError("ftruncate failed");
        handleException();
    }

    auto * const buf =
        static_cast<uint64_t *>(mmap(nullptr, sizeof(uint64_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    if (buf == MAP_FAILED) {
        logg.logError("mmap failed");
        handleException();
    }
    close(fd);

    *buf = flags;

    munmap(buf, sizeof(uint64_t));
}

void TtraceDriver::start()
{
    if (!mSupported) {
        return;
    }

    int flags = 0;
    for (auto * counter = static_cast<TtraceCounter *>(getCounters()); counter != nullptr;
         counter = static_cast<TtraceCounter *>(counter->getNext())) {
        if (!counter->isEnabled()) {
            continue;
        }
        flags |= counter->getFlag();
    }

    setTtrace(flags);
}

void TtraceDriver::stop()
{
    if (!mSupported) {
        return;
    }

    setTtrace(0);
}
