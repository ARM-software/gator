/**
 * Copyright (C) Arm Limited 2018. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "WaitForProcessPoller.h"

#include <string>

namespace
{
    /**
     * Utility class that processes the results of a single pass
     */
    class WaitForProcessPollerPass : public lnx::ProcessPollerBase::IProcessPollerReceiver
    {
    public:

        WaitForProcessPollerPass(const std::string & commandName, const lib::Optional<lib::FsEntry> & realPath)
            :   mCommandName (commandName),
                mRealPath (realPath),
                mPids ()
        {
        }

        inline const std::set<int> & pids() const
        {
            return mPids;
        }

        virtual void onProcessDirectory(int pid, const lib::FsEntry & path) override
        {
            if (shouldTrack(path)) {
                trackPid(pid);
            }
        }

    private:

        const std::string & mCommandName;
        const lib::Optional<lib::FsEntry> & mRealPath;
        std::set<int> mPids;

        bool shouldTrack(const lib::FsEntry & path) const
        {
            const auto cmdlineFile = lib::FsEntry::create(path, "cmdline");
            const auto cmdline = lib::readFileContents(cmdlineFile);

            // cmdline is separated by nulls so use c_str() to extract the command name
            const std::string command { cmdline.c_str() };

            // track it if they are the same string
            if (mCommandName == command) {
                return true;
            }

            // track it if they are the same file, or if they are the same basename
            const auto commandPath = lib::FsEntry::create(command);
            const auto realPath = commandPath.realpath();

            // they are the same executable command
            if (mRealPath && realPath && (*mRealPath == *realPath)) {
                return true;
            }

            // the basename of the command matches the command name
            // (e.g. /usr/bin/ls == ls)
            if (commandPath.name() == mCommandName) {
                return true;
            }

            return false;
        }

        void trackPid(int pid)
        {
            mPids.insert(pid);
        }
    };
}


WaitForProcessPoller::WaitForProcessPoller(const char * commandName)
    :   mCommandName (commandName),
        mRealPath (lib::FsEntry::create(commandName).realpath())
{
}

bool WaitForProcessPoller::poll(std::set<int> & pids)
{
    // poll
    WaitForProcessPollerPass pass { mCommandName, mRealPath };
    ProcessPollerBase::poll(false, false, pass);

    const auto & detectedPids = pass.pids();
    pids.insert(detectedPids.begin(), detectedPids.end());
    return !detectedPids.empty();
}
