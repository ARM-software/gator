/* Copyright (C) 2013-2023 by Arm Limited. All rights reserved. */

// Define to get format macros from inttypes.h
#define __STDC_FORMAT_MACROS

#include "DiskIODriver.h"

#include "Logging.h"
#include "SessionData.h"
#include "lib/String.h"

#include <cinttypes>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <unistd.h>

class DiskIOCounter : public DriverCounter {
public:
    DiskIOCounter(DriverCounter * next, const char * name);

    // Intentionally unimplemented
    DiskIOCounter(const DiskIOCounter &) = delete;
    DiskIOCounter & operator=(const DiskIOCounter &) = delete;
    DiskIOCounter(DiskIOCounter &&) = delete;
    DiskIOCounter & operator=(DiskIOCounter &&) = delete;

    int64_t read() override;
    void set(uint64_t sectors);

private:
    uint64_t sectors {0};
    uint64_t prev_sectors {0};
    static const uint64_t BYTES_IN_SECTOR = 512;
};

DiskIOCounter::DiskIOCounter(DriverCounter * next, const char * const name) : DriverCounter(next, name)
{
}

int64_t DiskIOCounter::read()
{
    // Diskstats reports number of sectors read/written. Linux always considers
    // sectors to be 512 bytes so multiply by 512 to get the bytes. See "number of sectors read/written"
    // https://www.kernel.org/doc/Documentation/iostats.txt and
    // https://github.com/torvalds/linux/blob/6f0d349d922ba44e4348a17a78ea51b7135965b1/include/linux/types.h#L125
    const uint64_t bytes = (sectors - prev_sectors) * BYTES_IN_SECTOR;
    prev_sectors = sectors;
    return static_cast<int64_t>(bytes);
}

void DiskIOCounter::set(uint64_t sectors)
{
    this->sectors = sectors;
}

namespace {
    constexpr int MIN_LINE_LEN_FOR_NAME_AND_USAGE = 10;
    constexpr int MIN_LINE_LEN_FOR_DISK_NAME = 3;
    constexpr int NAME_INDEX = 2;
    constexpr int READ_INDEX = 5;
    constexpr int WRITE_INDEX = 9;

    std::vector<std::string> split_diskstat_line_on_whitespace(const std::string & line, int words_needed)
    {
        std::istringstream line_stream {line};
        std::string string_in_diskstat_line {};
        std::vector<std::string> strings_in_diststat_line {};
        int words_found = 0;
        while (line_stream >> string_in_diskstat_line) {
            if (words_found == words_needed) {
                break; // don't visit the whole line, stop when we have reached the number of words we want
            }
            strings_in_diststat_line.emplace_back(string_in_diskstat_line);
            words_found++;
        }
        return strings_in_diststat_line;
    }

    using diskstats_line_tuple_t = std::tuple<std::string, uint64_t, uint64_t>;

    diskstats_line_tuple_t parse_diskstats_line(const std::vector<std::string> & diskstat_line)
    {
        return {diskstat_line[NAME_INDEX],
                std::stoull(diskstat_line[READ_INDEX]),
                std::stoull(diskstat_line[WRITE_INDEX])};
    }

    std::string parse_diskstats_name(const std::vector<std::string> & diskstat_line)
    {
        return diskstat_line[NAME_INDEX];
    }

    template<class T>
    std::vector<T> parse_diskstats(int min_line_len, std::function<T(std::vector<std::string>)> line_parser)
    {
        std::vector<T> result {};

        std::ifstream diskstats("/proc/diskstats");
        if (diskstats.fail()) {
            LOG_ERROR("Unable to read /proc/diskstats");
            handleException();
        }

        std::string diskstat_line {};
        const std::string current_partition_name {};
        while (std::getline(diskstats, diskstat_line)) {
            const std::vector<std::string> strings_in_diskstat_line =
                split_diskstat_line_on_whitespace(diskstat_line, min_line_len);

            if (strings_in_diskstat_line.size() < static_cast<typename std::vector<T>::size_type>(min_line_len)) {
                LOG_ERROR("Unable to parse /proc/diskstats");
                handleException();
            }

            result.emplace_back(line_parser(strings_in_diskstat_line));
        }

        return result;
    }

    std::vector<std::string> parse_diskstats_names()
    {
        return parse_diskstats<std::string>(MIN_LINE_LEN_FOR_DISK_NAME, parse_diskstats_name);
    }

    std::vector<diskstats_line_tuple_t> parse_diskstats_names_and_usage()
    {
        return parse_diskstats<diskstats_line_tuple_t>(MIN_LINE_LEN_FOR_NAME_AND_USAGE, parse_diskstats_line);
    }

}

void DiskIODriver::readEvents(mxml_node_t * const /*unused*/)
{
    if (access("/proc/diskstats", R_OK) == 0) {
        setCounters(new DiskIOCounter(getCounters(), "Linux_block_rq_rd"));
        setCounters(new DiskIOCounter(getCounters(), "Linux_block_rq_wr"));

        for (const auto & disk_name : parse_diskstats_names()) {
            std::string counter_name_reads = "diskstats_" + disk_name + "_reads";
            setCounters(new DiskIOCounter(getCounters(), counter_name_reads.data()));
            std::string counter_name_writes = "diskstats_" + disk_name + "_writes";
            setCounters(new DiskIOCounter(getCounters(), counter_name_writes.data()));
        }
    }
    else {
        LOG_SETUP("Linux counters\nCannot access /proc/diskstats. Disk I/O read and write counters not available.");
    }
}

void DiskIODriver::writeEvents(mxml_node_t * root) const
{
    if (access("/proc/diskstats", R_OK) == 0) {
        // Find the "Linux" category node
        mxml_node_t * node = root;
        while (true) {
            node = mxmlFindElement(node, root, "category", nullptr, nullptr, MXML_DESCEND);
            if (node == nullptr) {
                break;
            }
            const char * category_name = mxmlElementGetAttr(node, "name");
            if (strcmp(category_name, "Linux") == 0) {
                break; // Try next category
            }
        }

        // Add the per-disk reads and writes counters to this node
        for (const auto & disk_name : parse_diskstats_names()) {
            std::string counter_name_reads = "diskstats_" + disk_name + "_reads";
            auto * reads_xml = mxmlNewElement(node, "event");
            mxmlElementSetAttr(reads_xml, "counter", counter_name_reads.data());
            mxmlElementSetAttr(reads_xml, "title", "Disk I/O");
            const std::string counter_display_name_reads = "Reads: " + disk_name;
            mxmlElementSetAttr(reads_xml, "name", counter_display_name_reads.c_str());
            mxmlElementSetAttr(reads_xml, "units", "B");

            std::string counter_name_writes = "diskstats_" + disk_name + "_writes";
            auto * writes_xml = mxmlNewElement(node, "event");
            mxmlElementSetAttr(writes_xml, "counter", counter_name_writes.data());
            mxmlElementSetAttr(writes_xml, "title", "Disk I/O");
            const std::string counter_display_name_writes = "Writes: " + disk_name;
            mxmlElementSetAttr(writes_xml, "name", counter_display_name_writes.c_str());
            mxmlElementSetAttr(writes_xml, "units", "B");
        }
    }
}

void DiskIODriver::doRead()
{
    if (!countersEnabled()) {
        return;
    }

    uint64_t totalWriteBytes = 0;
    uint64_t totalReadBytes = 0;

    std::string current_partition_name {};
    for (const auto & [disk_name, read_bytes, write_bytes] : parse_diskstats_names_and_usage()) {
        // If a disk name starts with a previously seen disk name it's a
        // partition e.g. sda1 is a partition of sda
        const bool disk_is_not_a_partition = !lib::starts_with(disk_name, current_partition_name);
        const bool first_disk = current_partition_name.empty();
        if (first_disk || disk_is_not_a_partition) {
            totalReadBytes += read_bytes;
            totalWriteBytes += write_bytes;
            current_partition_name = disk_name;
        }

        const std::string counter_name_reads = "diskstats_" + disk_name + "_reads";
        const std::string counter_name_writes = "diskstats_" + disk_name + "_writes";
        for (DriverCounter * counter = getCounters(); counter != nullptr; counter = counter->getNext()) {
            auto * diskIoCounter = dynamic_cast<DiskIOCounter *>(counter);
            if (strcmp(diskIoCounter->getName(), counter_name_reads.c_str()) == 0) {
                diskIoCounter->set(static_cast<int64_t>(read_bytes));
            }
            if (strcmp(diskIoCounter->getName(), counter_name_writes.c_str()) == 0) {
                diskIoCounter->set(static_cast<int64_t>(write_bytes));
            }
        }
    }

    for (DriverCounter * counter = getCounters(); counter != nullptr; counter = counter->getNext()) {
        auto * diskIoCounter = dynamic_cast<DiskIOCounter *>(counter);
        if (strcmp(diskIoCounter->getName(), "Linux_block_rq_rd") == 0) {
            diskIoCounter->set(totalReadBytes);
        }
        if (strcmp(diskIoCounter->getName(), "Linux_block_rq_wr") == 0) {
            diskIoCounter->set(totalWriteBytes);
        }
    }
}

void DiskIODriver::start()
{
    doRead();
    // Initialize previous values
    for (DriverCounter * counter = getCounters(); counter != nullptr; counter = counter->getNext()) {
        if (!counter->isEnabled()) {
            continue;
        }
        counter->read();
    }
}

void DiskIODriver::read(IBlockCounterFrameBuilder & buffer)
{
    doRead();
    super::read(buffer);
}
