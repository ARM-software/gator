/**
 * Copyright (C) Arm Limited 2014-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "AtraceDriver.h"

#include <unistd.h>

#include "Logging.h"
#include "OlyUtility.h"
#include "FtraceDriver.h"

class AtraceCounter : public DriverCounter
{
public:
    AtraceCounter(DriverCounter *next, const char *name, int flag);
    ~AtraceCounter();

    int getFlag() const
    {
        return mFlag;
    }

private:
    const int mFlag;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(AtraceCounter);
};

AtraceCounter::AtraceCounter(DriverCounter *next, const char *name, int flag)
        : DriverCounter(next, name),
          mFlag(flag)
{
}

AtraceCounter::~AtraceCounter()
{
}

AtraceDriver::AtraceDriver(const FtraceDriver & ftraceDriver)
        : SimpleDriver("Atrace"),
          mSupported(false),
          mNotifyPath(),
          mFtraceDriver(ftraceDriver)
{
}

AtraceDriver::~AtraceDriver()
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

    mxml_node_t *node = xml;
    while (true) {
        node = mxmlFindElement(node, xml, "event", NULL, NULL, MXML_DESCEND);
        if (node == NULL) {
            break;
        }
        const char *counter = mxmlElementGetAttr(node, "counter");
        if (counter == NULL) {
            continue;
        }

        if (strncmp(counter, "atrace_", 7) != 0) {
            continue;
        }

        const char *flagStr = mxmlElementGetAttr(node, "flag");
        if (flagStr == NULL) {
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
        snprintf(buf, sizeof(buf), "setprop debug.atrace.tags.enableflags %i; "
                 "CLASSPATH=%s app_process /system/bin Notify",
                 flags, mNotifyPath);
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
    for (AtraceCounter *counter = static_cast<AtraceCounter *>(getCounters()); counter != NULL;
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
