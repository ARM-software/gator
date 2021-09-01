/* Copyright (C) 2018-2021 by Arm Limited. All rights reserved. */

#ifndef WAIT_FOR_PROCESS_POLLER_H
#define WAIT_FOR_PROCESS_POLLER_H

#include "lib/FsEntry.h"
#include "linux/proc/ProcessPollerBase.h"

#include <set>

/**
 * Polls /proc/ for some process matching the given command name
 */
class WaitForProcessPoller : private lnx::ProcessPollerBase {
public:
    /**
     * Constructor
     *
     * @param commandName The string representing the name to match
     */
    WaitForProcessPoller(const char * commandName);

    WaitForProcessPoller(const WaitForProcessPoller &) = delete;
    WaitForProcessPoller & operator=(const WaitForProcessPoller &) = delete;
    WaitForProcessPoller(WaitForProcessPoller &&) = delete;
    WaitForProcessPoller & operator=(WaitForProcessPoller &&) = delete;

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
};

#endif /* WAIT_FOR_PROCESS_POLLER_H */
