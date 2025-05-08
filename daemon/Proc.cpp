/* Copyright (C) 2013-2024 by Arm Limited. All rights reserved. */

#include "Proc.h"

#include "DynBuf.h"
#include "FtraceDriver.h"
#include "Logging.h"
#include "lib/FsEntry.h"
#include "lib/Span.h"
#include "linux/perf/IPerfAttrsConsumer.h"
#include "linux/proc/ProcPidStatFileRecord.h"
#include "linux/proc/ProcPidStatmFileRecord.h"
#include "linux/proc/ProcessPollerBase.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
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

    constexpr auto build_id_namesz_offset = std::size_t {0x00};
    constexpr auto build_id_descsz_offset = std::size_t {0x04};
    constexpr auto build_id_type_offset = std::size_t {0x08};
    constexpr auto build_id_data_offset = std::size_t {12};
    constexpr auto build_id_note_min_size = build_id_data_offset;
    constexpr auto nt_gnu_build_id = 3;

    /**
         * The build ID ELF section is structured as follows:
         *
         * +----------------+
         * |     namesz     |   32-bit, size of "name" field
         * +----------------+
         * |     descsz     |   32-bit, size of "desc" field
         * +----------------+
         * |      type      |   32-bit, vendor specific "type"
         * +----------------+
         * |      name      |   "namesz" bytes, null-terminated string
         * +----------------+
         * |      desc      |   "descsz" bytes, binary data
         * +----------------+
         *
         * We're interested in the 'desc' fields, the 'name' field is always "GNU\0".
         * @param notes_contents ELF section data
         * @return Build ID
         */

    [[nodiscard]] lib::Span<std::uint8_t const> parse_build_id(lib::Span<std::uint8_t const> notes_contents)
    {
        while (notes_contents.size() >= build_id_note_min_size) {
            // Read 32-bit name size field
            auto const name_size =
                *reinterpret_cast<std::uint32_t const *>(notes_contents.data() + build_id_namesz_offset);
            // Read 32-bit description size field
            auto const field_size =
                *reinterpret_cast<std::uint32_t const *>(notes_contents.data() + build_id_descsz_offset);
            // Read 32-bit type field
            auto const type = *reinterpret_cast<std::uint32_t const *>(notes_contents.data() + build_id_type_offset);

            // split out a span for this note and any remaining notes
            auto const name_size_rounded = ((name_size + 3U) & ~3U);
            auto const data_size_rounded = ((field_size + 3U) & ~3U);
            auto const note_size = std::min<std::size_t>(build_id_note_min_size + name_size_rounded + data_size_rounded,
                                                         notes_contents.size());

            auto const data = notes_contents.subspan(0, note_size);
            notes_contents = notes_contents.subspan(note_size);

            // validate the name and type
            if ((name_size != 4) || (type != nt_gnu_build_id)) {
                continue;
            }

            if ((build_id_data_offset + name_size) > data.size()) {
                continue;
            }

            if ((data[build_id_data_offset + 0] != 'G') || (data[build_id_data_offset + 1] != 'N')
                || (data[build_id_data_offset + 2] != 'U') || (data[build_id_data_offset + 3] != '\0')) {
                continue;
            }

            // extract the build-id value
            auto const build_id_size =
                std::min<std::size_t>(field_size, data.size() - (build_id_data_offset + name_size));

            return data.subspan(build_id_data_offset + name_size, build_id_size);
        }

        return {};
    }
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

void readKernelBuildId(IPerfAttrsConsumer & attrsConsumer)
{
    // Parse "/sys/kernel/notes"
    auto const kernel_notes = lib::FsEntry::create("/sys/kernel/notes");
    if (!kernel_notes.exists()) {
        LOG_DEBUG("Kernel does not provide notes file at %s", kernel_notes.path().c_str());
        return;
    }

    auto const buffer = kernel_notes.readFileContentsAsBytes();
    auto const build_id = parse_build_id(buffer);

    if (!build_id.empty()) {
        attrsConsumer.marshalKernelBuildId(build_id);
    }
    else {
        LOG_DEBUG("Failed to read build-id from %s", kernel_notes.path().c_str());
    }
}

void readModuleBuildIds(IPerfAttrsConsumer & attrsConsumer)
{
    // Parse each "sys/module/*/notes/.note.gnu.build-id"

    auto const modules_dir = lib::FsEntry::create("/sys/module");
    auto iter = modules_dir.children();

    for (auto next = iter.next(); next.has_value(); next = iter.next()) {
        auto const & child = *next;
        auto const note = lib::FsEntry::create(child, "notes/.note.gnu.build-id");
        if (!note.exists()) {
            continue;
        }

        auto const buffer = note.readFileContentsAsBytes();
        auto const build_id = parse_build_id(buffer);

        if (!build_id.empty()) {
            attrsConsumer.marshalKernelModuleBuildId(child.name(), build_id);
        }
        else {
            LOG_DEBUG("Failed to read build-id from %s", child.path().c_str());
        }
    }
}
