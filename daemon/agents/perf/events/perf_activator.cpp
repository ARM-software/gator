/* Copyright (C) 2021-2023 by Arm Limited. All rights reserved. */

#include "agents/perf/events/perf_activator.hpp"

#include "Logging.h"
#include "agents/perf/events/event_configuration.hpp"
#include "agents/perf/events/types.hpp"
#include "k/perf_event.h"
#include "lib/Assert.h"
#include "lib/AutoClosingFd.h"
#include "lib/EnumUtils.h"
#include "lib/Span.h"
#include "lib/String.h"
#include "lib/Syscall.h"
#include "lib/Utils.h"
#include "lib/error_code_or.hpp"
#include "linux/perf/PerfUtils.h"

#include <cerrno>
#include <cstring>
#include <sstream>
#include <stdexcept>

#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>

#include <unistd.h>

namespace agents::perf {
    namespace {

        inline lib::AutoClosingFd perf_event_open(perf_event_attr & attr,
                                                  pid_t pid,
                                                  int core,
                                                  int group_fd,
                                                  bool supports_cloexec)
        {
            auto const flags = PERF_FLAG_FD_OUTPUT | (supports_cloexec ? PERF_FLAG_FD_CLOEXEC : 0UL);

            int result = lib::perf_event_open(&attr, pid, core, group_fd, flags);
            if (result < 0) {
                return {};
            }

            if (!supports_cloexec) {
                int fdf = lib::fcntl(result, F_GETFD);
                //NOLINTNEXTLINE(hicpp-signed-bitwise) - FD_CLOEXEC
                if (lib::fcntl(result, F_SETFD, fdf | FD_CLOEXEC) != 0) {
                    LOG_WARNING("failed to set CLOEXEC on perf event due to %d", errno);
                }
            }

            return lib::AutoClosingFd {result};
        }

        lib::error_code_or_t<lib::AutoClosingFd> try_perf_event_open(perf_event_attr & attr,
                                                                     pid_t pid,
                                                                     int core,
                                                                     int group_fd,
                                                                     bool supports_cloexec,
                                                                     lib::Span<std::array<bool, 3> const> patterns)
        {
            for (auto const pattern : patterns) {
                // set
                attr.exclude_kernel = pattern[0];
                attr.exclude_hv = pattern[1];
                attr.exclude_idle = pattern[2];

                // try to open the event as is
                auto fd = perf_event_open(attr, pid, core, group_fd, supports_cloexec);

                // take a copy of errno so that logging calls etc don't overwrite it
                auto peo_errno = boost::system::errc::make_error_code(boost::system::errc::errc_t(errno));

                // successfully created?
                if (fd) {
                    LOG_DEBUG("Succeeded when exclude_kernel=%u, exclude_hv=%u, exclude_idle=%u",
                              bool(attr.exclude_kernel),
                              bool(attr.exclude_hv),
                              bool(attr.exclude_idle));

                    return {std::move(fd)};
                }

                LOG_WARNING("Failed when exclude_kernel=%u, exclude_hv=%u, exclude_idle=%u with %s",
                            bool(attr.exclude_kernel),
                            bool(attr.exclude_hv),
                            bool(attr.exclude_idle),
                            peo_errno.message().c_str());

                // not an error we can retry?
                if ((peo_errno != boost::system::errc::errc_t::permission_denied)
                    && (peo_errno != boost::system::errc::errc_t::operation_not_permitted)
                    && (peo_errno != boost::system::errc::errc_t::operation_not_supported)) {
                    return {peo_errno};
                }
            }

            // just return permission denied
            return {boost::system::errc::make_error_code(boost::system::errc::permission_denied)};
        }

        perf_event_id_t read_perf_id(int fd)
        {
            // get the id
            std::uint64_t id = 0;
            //NOLINTNEXTLINE(hicpp-signed-bitwise) - PERF_EVENT_IOC_ID
            if ((lib::ioctl(fd, PERF_EVENT_IOC_ID, reinterpret_cast<unsigned long>(&id)) != 0)
#if (__SIZEOF_LONG__ < 8)
                // Workaround for running 32-bit gatord on 64-bit systems, kernel patch in the works
                && (lib::ioctl(fd,
                               ((PERF_EVENT_IOC_ID & ~IOCSIZE_MASK) | (8 << _IOC_SIZESHIFT)),
                               reinterpret_cast<unsigned long>(&id))
                    != 0)
#endif
            ) {
                return perf_event_id_t::invalid;
            }
            return perf_event_id_t(id);
        }

        mmap_ptr_t try_mmap_with_logging(core_no_t core_no,
                                         const buffer_config_t & config,
                                         std::size_t length,
                                         off_t offset,
                                         int fd)
        {
            mmap_ptr_t result {lib::mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset), length};

            if (!result) {
                auto const mm_errno = boost::system::errc::make_error_code(boost::system::errc::errc_t(errno));

                LOG_WARNING("mmap failed for fd %i (errno=%d, %s, mmapLength=%zu, offset=%zu)",
                            fd,
                            mm_errno.value(),
                            mm_errno.message().c_str(),
                            length,
                            static_cast<std::size_t>(offset));

                if ((mm_errno == boost::system::errc::errc_t::not_enough_memory)
                    || ((errno == boost::system::errc::errc_t::operation_not_permitted) && (getuid() != 0))) {
                    LOG_ERROR("Could not mmap perf buffer on cpu %d, '%s' (errno: %d) returned.\n"
                              "This may be caused by a limit in /proc/sys/kernel/perf_event_mlock_kb.\n"
                              "Try again with a smaller value of --mmap-pages.\n"
                              "Usually, a value of ((perf_event_mlock_kb * 1024 / page_size) - 1) or lower will work.\n"
                              "The current effective value for --mmap-pages is %zu",
                              lib::toEnumValue(core_no),
                              mm_errno.message().c_str(),
                              mm_errno.value(),
                              config.data_buffer_size / config.page_size);

                    // log online state for core
                    lib::dyn_printf_str_t online_path {"/sys/devices/system/cpu/cpu%d/online",
                                                       lib::toEnumValue(core_no)};
                    std::int64_t online_status = 0;
                    lib::readInt64FromFile(online_path.c_str(), online_status);
                    LOG_DEBUG("Online status for cpu%d is %" PRId64, lib::toEnumValue(core_no), online_status);

                    // and mlock value
                    std::optional<std::int64_t> file_value = perf_utils::readPerfEventMlockKb();
                    if (file_value.has_value()) {
                        LOG_DEBUG(" Perf MlockKb Value is %" PRId64, file_value.value());
                    }
                    else {
                        LOG_DEBUG("reading Perf MlockKb returned null");
                    }
                }
            }
            else {
                LOG_DEBUG("mmap passed for fd %i (mmapLength=%zu, offset=%zu)",
                          fd,
                          length,
                          static_cast<std::size_t>(offset));
            }

            return result;
        }

        /**
         * Calculate the mmap region from @a config.
         *
         * @param config Buffer config.
         * @return Size in bytes.
         */
        [[nodiscard]] constexpr std::size_t get_data_mmap_length(buffer_config_t const & config)
        {
            return config.page_size + config.data_buffer_size;
        }
    }

    bool perf_activator_t::is_legacy_kernel_requires_id_from_read() const
    {
        return !capture_configuration->perf_config.has_ioctl_read_id;
    }

    std::pair<perf_activator_t::read_ids_status_t, std::vector<perf_event_id_t>>
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    perf_activator_t::read_legacy_ids(std::uint64_t read_format, int group_fd, std::size_t nr_ids)
    {
        constexpr int retry_count = 10;

        auto const is_id = ((read_format & PERF_FORMAT_ID) == PERF_FORMAT_ID);
        runtime_assert(is_id, "PERF_FORMAT_ID is required");
        auto const is_group = ((read_format & PERF_FORMAT_GROUP) == PERF_FORMAT_GROUP);
        auto const is_time_enabled = ((read_format & PERF_FORMAT_TOTAL_TIME_ENABLED) == PERF_FORMAT_TOTAL_TIME_ENABLED);
        auto const is_time_running = ((read_format & PERF_FORMAT_TOTAL_TIME_RUNNING) == PERF_FORMAT_TOTAL_TIME_RUNNING);
        auto const required_u64 = (is_time_enabled ? 1 : 0) //
                                + (is_time_running ? 1 : 0) //
                                + (is_group ? (nr_ids * 2) + 1 : 2);

        std::vector<std::uint64_t> buffer(required_u64);

        for (int retry = 0; retry < retry_count; ++retry) {
            auto bytes =
                lib::read(group_fd, reinterpret_cast<char *>(buffer.data()), buffer.size() * sizeof(std::uint64_t));

            if (bytes < 0) {
                auto rerrno = boost::system::errc::make_error_code(boost::system::errc::errc_t(errno));
                LOG_WARNING("read failed for read_legacy_ids with %d (%s)", rerrno.value(), rerrno.message().c_str());
                return {read_ids_status_t::failed_fatal, {}};
            }

            if (bytes == 0) {
                /* pinning failed, retry */
                usleep(1);
                continue;
            }

            // decode the buffer and emit key->id mappings
            auto const nr = (is_group ? buffer[0] : 1);
            auto const id_offset = (1 + (is_time_enabled ? 1 : 0) + (is_time_running ? 1 : 0) + (is_group ? 1 : 0));

            if (nr != nr_ids) {
                LOG_ERROR("Unexpected read_format data read (invalid size, expected %zu, got %" PRIu64 ", group=%u)",
                          nr_ids,
                          nr,
                          is_group);
                return {read_ids_status_t::failed_fatal, {}};
            }

            std::vector<perf_event_id_t> result {};
            for (std::size_t n = 0; n < nr; ++n) {
                auto const id = perf_event_id_t(buffer[id_offset + (2 * n)]);
                result.emplace_back(id);
            }
            return {read_ids_status_t::success, std::move(result)};
        }

        return {read_ids_status_t::failed_offline, {}};
    }

    //NOLINTNEXTLINE(readability-function-cognitive-complexity)
    perf_activator_t::event_creation_result_t perf_activator_t::create_event(event_definition_t const & event,
                                                                             enable_state_t enable_state,
                                                                             core_no_t core_no,
                                                                             pid_t pid,
                                                                             int group_fd)
    {
        constexpr std::array<std::array<bool, 3>, 4> exclude_pattern_exclude_kernel {{
            // exclude_kernel, exclude_hv, exclude_idle
            {{true, true, true}},
            {{true, true, false}},
            {{true, false, true}},
            {{true, false, false}},
        }};

        constexpr std::array<std::array<bool, 3>, 6> exclude_pattern_include_kernel {{
            // exclude_kernel, exclude_hv, exclude_idle
            {{false, false, false}},
            {{false, true, false}},
            // these are the same as per exclude_pattern_exclude_kernel
            {{true, true, true}},
            {{true, true, false}},
            {{true, false, true}},
            {{true, false, false}},
        }};

        // prepare the attribute
        // Note we are modifying the attr after we have marshalled it
        // but we are assuming the modifications are not important to Streamline
        auto attr = event.attr;

        // set enable on exec bit
        attr.disabled = ((group_fd < 0) && (enable_state != enable_state_t::enabled));
        attr.enable_on_exec = (attr.disabled && (enable_state == enable_state_t::enable_on_exec));

        LOG_FINE("Opening attribute:\n"
                 "    cpu: %i\n"
                 "    key: %i\n"
                 "    -------------\n"
                 "%s",
                 lib::toEnumValue(core_no),
                 lib::toEnumValue(event.key),
                 perf_event_printer.perf_attr_to_string(attr, core_no, "    ", "\n").c_str());
        LOG_FINE("perf_event_open: cpu: %d, pid: %d, leader = %d", lib::toEnumValue(core_no), pid, group_fd);

        lib::AutoClosingFd fd {};
        boost::system::error_code peo_errno {};

        // if the attr excludes kernel events, then try by excluding various combinations of exclude_bits starting from most restrictive
        if (attr.exclude_kernel) {
            auto result = try_perf_event_open(attr,
                                              pid,
                                              int(core_no),
                                              group_fd,
                                              capture_configuration->perf_config.has_fd_cloexec,
                                              exclude_pattern_exclude_kernel);

            lib::get_error_or_value(std::move(result), fd, peo_errno);
        }
        else {
            auto result = try_perf_event_open(attr,
                                              pid,
                                              int(core_no),
                                              group_fd,
                                              capture_configuration->perf_config.has_fd_cloexec,
                                              exclude_pattern_include_kernel);

            lib::get_error_or_value(std::move(result), fd, peo_errno);
        }

        // process the failure?
        if (!fd) {
            LOG_WARNING("... failed %d %s", peo_errno.value(), peo_errno.message().c_str());

            if (peo_errno == boost::system::errc::errc_t::no_such_device) {
                // CPU offline
                return event_creation_result_t {event_creation_status_t::failed_offline};
            }
            if (peo_errno == boost::system::errc::errc_t::no_such_process) {
                // thread terminated before we could open the event
                return event_creation_result_t {event_creation_status_t::failed_invalid_pid};
            }
            if (peo_errno == boost::system::errc::errc_t::no_such_file_or_directory) {
                // event doesn't apply to this cpu
                return event_creation_result_t {event_creation_status_t::failed_invalid_device};
            }

            // all other errors are fatal
            std::ostringstream error_message {};

            error_message << "perf_event_open failed to online counter for "
                          << perf_event_printer.map_attr_type(attr.type, core_no);
            error_message << " with config=0x" << std::hex << attr.config << std::dec;
            error_message << " on CPU " << int(core_no);
            error_message << ". Failure given was errno=" << peo_errno.value() << " (" << peo_errno.message() << ").";

            if (capture_configuration->perf_config.is_system_wide) {
                if (peo_errno == boost::system::errc::errc_t::invalid_argument) {
                    switch (event.attr.type) {
                        case PERF_TYPE_BREAKPOINT:
                        case PERF_TYPE_SOFTWARE:
                        case PERF_TYPE_TRACEPOINT:
                            break;
                        case PERF_TYPE_HARDWARE:
                        case PERF_TYPE_HW_CACHE:
                        case PERF_TYPE_RAW:
                        default:
                            error_message << "\n\nAnother process may be using the PMU counter, or the "
                                             "combination requested may not be supported by the hardware. Try "
                                             "removing some events.";
                            break;
                    }
                }
            }

            // some other error
            return event_creation_result_t {peo_errno, error_message.str()};
        }

        // read the id
        perf_event_id_t perf_id = perf_event_id_t::invalid;

        if (capture_configuration->perf_config.has_ioctl_read_id) {
            perf_id = read_perf_id(*fd);
            if (perf_id == perf_event_id_t::invalid) {
                // take a new copy of the errno if it failed, before calling log
                peo_errno = boost::system::errc::make_error_code(boost::system::errc::errc_t(errno));
                LOG_WARNING("Reading a perf event id failed for file-descriptor %d with error %d (%s)",
                            *fd,
                            peo_errno.value(),
                            peo_errno.message().c_str());
                return event_creation_result_t {peo_errno};
            }
        }

        LOG_FINE("... event activated successfully %" PRIu64 " %d", lib::toEnumValue(perf_id), *fd);

        // complete
        return event_creation_result_t {perf_id,
                                        std::make_shared<boost::asio::posix::stream_descriptor>(context, fd.release())};
    }

    //NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    bool perf_activator_t::set_output(int fd, int output_fd)
    {
        runtime_assert((output_fd > 0), "invalid output_fd");

        //NOLINTNEXTLINE(hicpp-signed-bitwise) - PERF_EVENT_IOC_SET_OUTPUT
        if (lib::ioctl(fd, PERF_EVENT_IOC_SET_OUTPUT, output_fd) != 0) {
            // take a new copy of the errno if it failed, before calling log
            auto peo_errno = boost::system::errc::make_error_code(boost::system::errc::errc_t(errno));
            LOG_DEBUG("Setting the output fd for perf event %d with error %d (%s)",
                      fd,
                      peo_errno.value(),
                      peo_errno.message().c_str());
            return false;
        }

        return true;
    }

    //NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    perf_ringbuffer_mmap_t perf_activator_t::mmap_data(core_no_t core_no, int fd)
    {
        auto const & ringbuffer_config = capture_configuration->ringbuffer_config;
        auto const data_length = get_data_mmap_length(ringbuffer_config);

        auto data_mapping = try_mmap_with_logging(core_no, ringbuffer_config, data_length, 0, fd);
        if (!data_mapping) {
            return {};
        }

        return {ringbuffer_config.page_size, std::move(data_mapping)};
    }

    void perf_activator_t::mmap_aux(perf_ringbuffer_mmap_t & mmap, core_no_t core_no, int fd)
    {
        auto const & ringbuffer_config = capture_configuration->ringbuffer_config;
        auto const data_length = get_data_mmap_length(ringbuffer_config);
        auto const aux_length = ringbuffer_config.aux_buffer_size;

        if (data_length > std::numeric_limits<off_t>::max()) {
            LOG_WARNING("Offset for perf aux buffer is out of range: %zu", data_length);
            return;
        }

        // Update the header
        auto * pemp = mmap.header();
        pemp->aux_offset = data_length;
        pemp->aux_size = aux_length;

        auto aux_mapping =
            try_mmap_with_logging(core_no, ringbuffer_config, aux_length, static_cast<off_t>(data_length), fd);
        if (!aux_mapping) {
            return;
        }

        mmap.set_aux_mapping(std::move(aux_mapping));
    }

    //NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    bool perf_activator_t::start(int fd)
    {
        LOG_DEBUG("enabling fd %d", fd);
        //NOLINTNEXTLINE(hicpp-signed-bitwise) - PERF_EVENT_IOC_ENABLE
        return (lib::ioctl(fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP) == 0);
    }

    //NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    bool perf_activator_t::stop(int fd)
    {
        LOG_DEBUG("disabling fd %d", fd);
        //NOLINTNEXTLINE(hicpp-signed-bitwise) - PERF_EVENT_IOC_DISABLE
        return (lib::ioctl(fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) == 0);
    }

    //NOLINTNEXTLINE(readability-convert-member-functions-to-static)
    bool perf_activator_t::re_enable(int fd)
    {
        LOG_DEBUG("enabling fd %d", fd);
        //NOLINTNEXTLINE(hicpp-signed-bitwise) - PERF_EVENT_IOC_ENABLE
        return (lib::ioctl(fd, PERF_EVENT_IOC_ENABLE, 0) == 0);
    }
}
