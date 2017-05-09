/**
 * Copyright (C) ARM Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Proc.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "Buffer.h"
#include "DynBuf.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"

#include "linux/proc/ProcessPollerBase.h"
#include "linux/proc/ProcPidStatFileRecord.h"

namespace
{
    class ReadProcSysDependenciesPollerVisiter : private lnx::ProcessPollerBase::IProcessPollerReceiver,
                                                 private lnx::ProcessPollerBase
    {
    public:

        ReadProcSysDependenciesPollerVisiter(uint64_t currTime_, Buffer & buffer_)
                : currTime(currTime_),
                  buffer(buffer_)
        {
        }

        void poll()
        {
            ProcessPollerBase::poll(true, true, *this);
        }

    private:

        uint64_t currTime;
        Buffer & buffer;

        virtual void onThreadDetails(int pid, int tid, const lnx::ProcPidStatFileRecord & statRecord,
                                     const lib::Optional<lnx::ProcPidStatmFileRecord> &,
                                     const lib::Optional<lib::FsEntry> & exe) override
        {
            buffer.marshalComm(currTime, pid, tid, (exe ? exe->path().c_str() : ""), statRecord.getComm().c_str());
        }
    };

    class ReadProcMapsPollerVisiter : private lnx::ProcessPollerBase::IProcessPollerReceiver,
                                      private lnx::ProcessPollerBase
    {
    public:

        ReadProcMapsPollerVisiter(uint64_t currTime_, Buffer & buffer_)
                : currTime(currTime_),
                  buffer(buffer_)
        {
        }

        void poll()
        {
            ProcessPollerBase::poll(false, false, *this);
        }

    private:

        uint64_t currTime;
        Buffer & buffer;

        virtual void onProcessDirectory(int pid, const lib::FsEntry & path) override
        {
            const lib::FsEntry mapsFile = lib::FsEntry::create(path, "maps");
            const std::string mapsContents = lib::readFileContents(mapsFile);

            buffer.marshalMaps(currTime, pid, pid, mapsContents.c_str());
        }
    };
}

bool readProcSysDependencies(const uint64_t currTime, Buffer & buffer, DynBuf * const printb, DynBuf * const b1)
{
    ReadProcSysDependenciesPollerVisiter poller(currTime, buffer);
    poller.poll();

    if (gSessionData.mFtraceRaw) {
        if (!gSessionData.mFtraceDriver.readTracepointFormats(currTime, &buffer, printb, b1)) {
            logg.logMessage("FtraceDriver::readTracepointFormats failed");
            return false;
        }
    }

    return true;
}

bool readProcMaps(const uint64_t currTime, Buffer & buffer)
{
    ReadProcMapsPollerVisiter poller(currTime, buffer);
    poller.poll();

    return true;
}

bool readKallsyms(const uint64_t currTime, Buffer * const buffer, const bool * const isDone)
{
    int fd = ::open("/proc/kallsyms", O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        logg.logMessage("open failed");
        return true;
    };

    char buf[1 << 12];
    ssize_t pos = 0;
    while (gSessionData.mSessionIsActive && !ACCESS_ONCE(*isDone)) {
        // Assert there is still space in the buffer
        if (sizeof(buf) - pos - 1 == 0) {
            logg.logError("no space left in buffer");
            handleException();
        }

        {
            // -1 to reserve space for \0
            const ssize_t bytes = ::read(fd, buf + pos, sizeof(buf) - pos - 1);
            if (bytes < 0) {
                logg.logError("read failed");
                handleException();
            }
            if (bytes == 0) {
                // Assert the buffer is empty
                if (pos != 0) {
                    logg.logError("buffer not empty on eof");
                    handleException();
                }
                break;
            }
            pos += bytes;
        }

        ssize_t newline;
        // Find the last '\n'
        for (newline = pos - 1; newline >= 0; --newline) {
            if (buf[newline] == '\n') {
                const char was = buf[newline + 1];
                buf[newline + 1] = '\0';
                buffer->marshalKallsyms(currTime, buf);
                buf[0] = was;
                // Assert the memory regions do not overlap
                if (pos - newline >= newline + 1) {
                    logg.logError("memcpy src and dst overlap");
                    handleException();
                }
                if (pos - newline - 2 > 0) {
                    memcpy(buf + 1, buf + newline + 2, pos - newline - 2);
                }
                pos -= newline + 1;
                break;
            }
        }
    }

    close(fd);

    return true;
}

bool readTracepointFormat(const uint64_t currTime, Buffer * const buffer, const char * const name,
                          DynBuf * const printb, DynBuf * const b)
{
    if (!printb->printf(EVENTS_PATH "/%s/format", name)) {
        logg.logMessage("DynBuf::printf failed");
        return false;
    }
    if (!b->read(printb->getBuf())) {
        logg.logMessage("DynBuf::read failed");
        return false;
    }
    buffer->marshalFormat(currTime, b->getLength(), b->getBuf());

    return true;
}
