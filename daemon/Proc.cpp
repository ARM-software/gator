/* Copyright (C) 2013-2023 by Arm Limited. All rights reserved. */

#include "Proc.h"

#include "DynBuf.h"
#include "FtraceDriver.h"
#include "Logging.h"
#include "lib/FsEntry.h"
#include "linux/perf/IPerfAttrsConsumer.h"
#include "linux/proc/ProcPidStatFileRecord.h"
#include "linux/proc/ProcPidStatmFileRecord.h"
#include "linux/proc/ProcessPollerBase.h"

#include <atomic>
#include <cstring>
#include <optional>
#include <string>

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

namespace {
    class ReadProcSysDependenciesPollerVisiter : private lnx::ProcessPollerBase::IProcessPollerReceiver,
                                                 private lnx::ProcessPollerBase {
    public:
        ReadProcSysDependenciesPollerVisiter(IPerfAttrsConsumer & buffer_) : buffer(buffer_) {}

        void poll() { ProcessPollerBase::poll(true, true, *this); }

    private:
        IPerfAttrsConsumer & buffer;

        void onThreadDetails(int pid,
                             int tid,
                             const lnx::ProcPidStatFileRecord & statRecord,
                             const std::optional<lnx::ProcPidStatmFileRecord> & /*statmRecord*/,
                             const std::optional<std::string> & exe) override
        {
            buffer.marshalComm(pid, tid, (exe ? exe->c_str() : ""), statRecord.getComm().c_str());
        }
    };

    class ReadProcMapsPollerVisiter : private lnx::ProcessPollerBase::IProcessPollerReceiver,
                                      private lnx::ProcessPollerBase {
    public:
        ReadProcMapsPollerVisiter(IPerfAttrsConsumer & buffer_) : buffer(buffer_) {}

        void poll() { ProcessPollerBase::poll(false, false, *this); }

    private:
        IPerfAttrsConsumer & buffer;

        void onProcessDirectory(int pid, const lib::FsEntry & path) override
        {
            const lib::FsEntry mapsFile = lib::FsEntry::create(path, "maps");
            const std::string mapsContents = lib::readFileContents(mapsFile);

            buffer.marshalMaps(pid, pid, mapsContents.c_str());
        }
    };
}

bool readProcSysDependencies(IPerfAttrsConsumer & buffer,
                             DynBuf * const printb,
                             DynBuf * const b1,
                             FtraceDriver & ftraceDriver)
{
    ReadProcSysDependenciesPollerVisiter poller(buffer);
    poller.poll();

    if (!ftraceDriver.readTracepointFormats(buffer, printb, b1)) {
        LOG_DEBUG("FtraceDriver::readTracepointFormats failed");
        return false;
    }

    return true;
}

bool readProcMaps(IPerfAttrsConsumer & buffer)
{
    ReadProcMapsPollerVisiter poller(buffer);
    poller.poll();

    return true;
}

bool readKallsyms(IPerfAttrsConsumer & attrsConsumer, const std::atomic_bool & isDone)
{
    int fd = ::open("/proc/kallsyms", O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        LOG_DEBUG("open failed");
        return true;
    };

    char buf[1 << 12];
    ssize_t pos = 0;
    while (!isDone) {
        // Assert there is still space in the buffer
        if (sizeof(buf) - pos - 1 == 0) {
            LOG_ERROR("no space left in buffer");
            handleException();
        }

        {
            // -1 to reserve space for \0
            const ssize_t bytes = ::read(fd, buf + pos, sizeof(buf) - pos - 1);
            if (bytes < 0) {
                LOG_ERROR("read failed");
                handleException();
            }
            if (bytes == 0) {
                // Assert the buffer is empty
                if (pos != 0) {
                    LOG_ERROR("buffer not empty on eof");
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
                attrsConsumer.marshalKallsyms(buf);
                buf[0] = was;
                // Assert the memory regions do not overlap
                if (pos - newline >= newline + 1) {
                    LOG_ERROR("memcpy src and dst overlap");
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
