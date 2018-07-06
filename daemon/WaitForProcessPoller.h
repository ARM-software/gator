/**
 * Copyright (C) Arm Limited 2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef WAIT_FOR_PROCESS_POLLER_H
#define WAIT_FOR_PROCESS_POLLER_H

#include <set>

#include "ClassBoilerPlate.h"
#include "lib/FsEntry.h"
#include "linux/proc/ProcessPollerBase.h"

/**
 * Polls /proc/ for some process matching the given command name
 */
class WaitForProcessPoller : private lnx::ProcessPollerBase
{
public:

    /**
     * Constructor
     *
     * @param commandName The string representing the name to match
     */
    WaitForProcessPoller(const char * commandName);

    /**
     * Perform one pass over /proc, polling for any pids matching commandName
     *
     * @param pids As set of ints containing pids for processes that match
     * @return True if pids is modified, false otherwise
     */
    bool poll(std::set<int> & pids);

private:

    const std::string mCommandName;
    const lib::Optional<lib::FsEntry> mRealPath;

    CLASS_DELETE_COPY_MOVE(WaitForProcessPoller);
};

#endif /* WAIT_FOR_PROCESS_POLLER_H */
