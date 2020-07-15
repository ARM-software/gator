/* Copyright (C) 2014-2020 by Arm Limited. All rights reserved. */

#include "AtraceDriver.h"

#include "FtraceDriver.h"
#include "Logging.h"
#include "OlyUtility.h"

#include <unistd.h>

class AtraceCounter : public DriverCounter {
public:
    AtraceCounter(DriverCounter * next, const char * name, int flag);

    int getFlag() const { return mFlag; }

private:
    const int mFlag;

    // Intentionally unimplemented
    AtraceCounter(const AtraceCounter &) = delete;
    AtraceCounter & operator=(const AtraceCounter &) = delete;
    AtraceCounter(AtraceCounter &&) = delete;
    AtraceCounter & operator=(AtraceCounter &&) = delete;
};

AtraceCounter::AtraceCounter(DriverCounter * next, const char * name, int flag) : DriverCounter(next, name), mFlag(flag)
{
}

AtraceDriver::AtraceDriver(const FtraceDriver & ftraceDriver)
    : SimpleDriver("Atrace"), mSupported(false), mNotifyPath(), mFtraceDriver(ftraceDriver)
{
}

void AtraceDriver::readEvents(mxml_node_t * const xml)
{
    if (access("/system/bin/setprop", X_OK) != 0) {
        // Reduce warning noise
        //logg.logSetup("Atrace is disabled\nUnable to find setprop, this is not an Android target");
        return;
    }
    if (!mFtraceDriver.isSupported()) {
        logg.logSetup("Atrace is disabled\nSupport for ftrace is required");
        return;
    }
    if (getApplicationFullPath(mNotifyPath, sizeof(mNotifyPath)) != 0) {
        logg.logMessage("Unable to determine the full path of gatord, the cwd will be used");
    }
    strncat(mNotifyPath, "notify.dex", sizeof(mNotifyPath) - strlen(mNotifyPath) - 1);
    if (access(mNotifyPath, W_OK) != 0) {
        logg.logSetup("Atrace is disabled\nUnable to locate notify.dex");
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

        if (strncmp(counter, "atrace_", 7) != 0) {
            continue;
        }

        const char * flagStr = mxmlElementGetAttr(node, "flag");
        if (flagStr == nullptr) {
            logg.logError("The atrace counter %s is missing the required flag attribute", counter);
            handleException();
        }
        int flag;
        if (!stringToInt(&flag, flagStr, 16)) {
            logg.logError("The flag attribute of the atrace counter %s is not a hex integer", counter);
            handleException();
        }
        setCounters(new AtraceCounter(getCounters(), counter, flag));
    }
}

void AtraceDriver::setAtrace(const int flags)
{
    logg.logMessage("Setting atrace flags to %i", flags);
    pid_t pid = fork();
    if (pid < 0) {
        logg.logError("fork failed");
        handleException();
    }
    else if (pid == 0) {
        char buf[1 << 10];
        snprintf(buf,
                 sizeof(buf),
                 "setprop debug.atrace.tags.enableflags %i; "
                 "CLASSPATH=%s app_process /system/bin Notify",
                 flags,
                 mNotifyPath);
        execlp("sh", "sh", "-c", buf, nullptr);
        exit(0);
    }
}

void AtraceDriver::start()
{
    if (!mSupported) {
        return;
    }

    int flags = 0;
    for (auto * counter = static_cast<AtraceCounter *>(getCounters()); counter != nullptr;
         counter = static_cast<AtraceCounter *>(counter->getNext())) {
        if (!counter->isEnabled()) {
            continue;
        }
        flags |= counter->getFlag();
    }

    setAtrace(flags);
}

void AtraceDriver::stop()
{
    if (!mSupported) {
        return;
    }

    setAtrace(0);
}
