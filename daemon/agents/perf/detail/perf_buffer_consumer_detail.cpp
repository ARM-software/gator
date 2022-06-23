/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#include "agents/perf/detail/perf_buffer_consumer_detail.h"

#include "lib/Utils.h"
#include "linux/perf/PerfUtils.h"

#include <cinttypes>

namespace agents::perf::detail {
    void * try_mmap_with_logging(int cpu, const buffer_config_t & config, std::size_t length, off_t offset, int fd)
    {
        auto * buf = lib::mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);

        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        if (buf == MAP_FAILED) {
            std::array<char, error_buf_sz> strbuf {0};
            strerror_r(errno, strbuf.data(), strbuf.size());
            LOG_DEBUG("mmap failed for fd %i (errno=%d, %s, mmapLength=%zu, offset=%zu)",
                      fd,
                      errno,
                      strbuf.data(),
                      length,
                      static_cast<std::size_t>(offset));
            if ((errno == ENOMEM) || ((errno == EPERM) && (getuid() != 0))) {
                LOG_ERROR("Could not mmap perf buffer on cpu %d, '%s' (errno: %d) returned.\n"
                          "This may be caused by a limit in /proc/sys/kernel/perf_event_mlock_kb.\n"
                          "Try again with a smaller value of --mmap-pages.\n"
                          "Usually, a value of ((perf_event_mlock_kb * 1024 / page_size) - 1) or lower will work.\n"
                          "The current effective value for --mmap-pages is %zu",
                          cpu,
                          strbuf.data(),
                          errno,
                          config.data_buffer_size / config.page_size);
                snprintf(strbuf.data(), strbuf.size(), "/sys/devices/system/cpu/cpu%u/online", cpu);
                std::int64_t online_status = 0;
                lib::readInt64FromFile(strbuf.data(), online_status);
                LOG_DEBUG("Online status for cpu%d is %" PRId64, cpu, online_status);

                std::optional<std::int64_t> file_value = perf_utils::readPerfEventMlockKb();
                if (file_value.has_value()) {
                    LOG_DEBUG(" Perf MlockKb Value is %" PRId64, file_value.value());
                }
                else {
                    LOG_DEBUG("reading Perf MlockKb returned null");
                }
            }
            else {
                LOG_DEBUG("mmap failed for a different reason");
            }
        }
        else {
            LOG_DEBUG("mmap passed for fd %i (mmapLength=%zu, offset=%zu)",
                      fd,
                      length,
                      static_cast<std::size_t>(offset));
        }
        return buf;
    }
}
