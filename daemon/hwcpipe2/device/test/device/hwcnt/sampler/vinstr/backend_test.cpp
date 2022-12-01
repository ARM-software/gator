/*
 * Copyright (c) 2022 ARM Limited.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <catch2/catch.hpp>

#include <debug/ostream_indent.hpp>
#include <device/hwcnt/block_extents.hpp>
#include <device/hwcnt/block_metadata.hpp>
#include <device/hwcnt/prfcnt_set.hpp>
#include <device/hwcnt/reader.hpp>
#include <device/hwcnt/sample.hpp>
#include <device/hwcnt/sample_operators.hpp>
#include <device/hwcnt/sampler/vinstr/backend.hpp>
#include <device/hwcnt/sampler/vinstr/sample_layout.hpp>
#include <device/ioctl/vinstr/commands.hpp>
#include <device/ioctl/vinstr/compare.hpp>
#include <device/ioctl/vinstr/print.hpp>
#include <device/ioctl/vinstr/types.hpp>
#include <device/mock/syscall/iface.hpp>

#include <algorithm>
#include <bitset>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <vector>

#include <poll.h>
#include <unistd.h>

namespace hwcnt = hwcpipe::device::hwcnt;
namespace vinstr = hwcpipe::device::hwcnt::sampler::vinstr;
namespace ioctl = hwcpipe::device::ioctl;

/**
 * Enable catch2 macros for a thread.
 *
 * Catch2 supports only one thread at a time.
 */
static thread_local bool enable_catch2 = true;

/** Indicates whether error occurred on a thread. */
static thread_local bool thread_result = true;

#define LOCAL_REQUIRE(expr)                                                                                            \
    do {                                                                                                               \
        if (enable_catch2) {                                                                                           \
            REQUIRE(expr);                                                                                             \
        } else if (!(expr)) {                                                                                          \
            std::cout << __FILE__ << ":" << __LINE__ << ": REQUIRE(" #expr ") failed!" << std::endl;                   \
            thread_result = false;                                                                                     \
            throw std::runtime_error("REQUIRE failed");                                                                \
        }                                                                                                              \
    } while (false)

#define LOCAL_CHECK(expr)                                                                                              \
    do {                                                                                                               \
        if (enable_catch2) {                                                                                           \
            CHECK(expr);                                                                                               \
        } else if (!(expr)) {                                                                                          \
            std::cout << __FILE__ << ":" << __LINE__ << ": CHECK(" #expr ") failed!" << std::endl;                     \
            thread_result = false;                                                                                     \
        }                                                                                                              \
    } while (false)

#define LOCAL_INFO(expr)                                                                                               \
    do {                                                                                                               \
        if (enable_catch2)                                                                                             \
            UNSCOPED_INFO(expr);                                                                                       \
    } while (false)

namespace test {
/** Top GPU cycles value. */
static constexpr uint64_t gpu_cycles{12345};
/** Shader cores cycles value. */
static constexpr uint64_t sc_cycles{54321};
/** Timstamp start value. */
static constexpr uint64_t timestamp_start{100000};
/** Timstamp step value. */
static constexpr uint64_t timestamp_dt{42};
/** Buffer size */
static constexpr uint64_t buffer_size{1024};
/** The number of L2 cache slices. */
static constexpr uint64_t num_l2_slices = 2;
/** Shader cores mask. */
static constexpr uint64_t shader_cores_mask = 0b1111;
/** Block Layout */
static constexpr hwcpipe::device::hwcnt::sampler::vinstr::sample_layout_type sample_layout_type =
    hwcpipe::device::hwcnt::sampler::vinstr::sample_layout_type::non_v4;
/** Block extents. */
static const hwcnt::block_extents extents{{{1, 1, 2, 4}}, 64, hwcnt::sample_values_type::uint32};
/** Sample layout instance */
static const vinstr::sample_layout sample_layout{extents, num_l2_slices, shader_cores_mask, sample_layout_type};

} // namespace test

namespace mock {

/** Mock timestamp interface. */
struct timestamp_iface {
    uint64_t clock_gettime() {
        LOCAL_REQUIRE(clock_gettime_fn);

        return clock_gettime_fn();
    }

    std::function<uint64_t()> clock_gettime_fn;
};

/** Back-end with mocked syscall and timestamp interfaces. */
using backend_type = vinstr::backend<mock::syscall::iface, mock::timestamp_iface>;
/** Back-end arguments type. */
using backend_args_type = backend_type::args_type;

/** Vinstr kernel state mock. */
class vinstr {
  public:
    vinstr() {
        int fds[2];
        LOCAL_REQUIRE(pipe(fds) == 0);

        read_fd_ = fds[0];
        write_fd_ = fds[1];
    }

    ~vinstr() {
        LOCAL_CHECK(close(read_fd_) == 0);
        LOCAL_CHECK(close(write_fd_) == 0);
    }

    /** @return Mock vinstr fd. */
    int fd() const { return read_fd_; }

    /** @return clear command return code. */
    std::error_code clear() {
        clear_count_++;
        clear_flag_ = true;
        timstamp_flag_ = false;

        return {};
    }

    /** @return Dump command return code. */
    std::error_code dump() {
        LOCAL_INFO("Dump command is only allowed for manual context "
                   "or when periodic session is being stopped.");

        LOCAL_CHECK(interval_ == 0);

        return dump(ioctl::vinstr::reader_event::manual);
    }

    /** @return Periodic dump command return code. */
    std::error_code periodic_dump() {
        LOCAL_CHECK(interval_ != 0);

        return dump(ioctl::vinstr::reader_event::periodic);
    }

    std::error_code get_buffer_with_cycles(void *argp) {
        if (has_active_buffer_)
            return std::make_error_code(std::errc::invalid_argument);

        ioctl::vinstr::reader_metadata_with_cycles metadata{};

        constexpr size_t read_size = sizeof(metadata);
        LOCAL_REQUIRE(read(read_fd_, &metadata, read_size) == read_size);

        has_active_buffer_ = true;
        std::memcpy(&active_buffer_, &metadata.metadata, sizeof(metadata.metadata));

        std::memcpy(argp, &metadata, read_size);
        return {};
    }

    std::error_code get_buffer(void *argp) {
        ioctl::vinstr::reader_metadata_with_cycles metadata{};
        auto ec = get_buffer_with_cycles(&metadata);

        if (ec)
            return ec;

        std::memcpy(argp, &metadata.metadata, sizeof(metadata.metadata));

        return {};
    }

    std::error_code put_buffer(void *argp) {
        if (!has_active_buffer_)
            return std::make_error_code(std::errc::invalid_argument);

        auto metadata = static_cast<const ioctl::vinstr::reader_metadata *>(argp);

        LOCAL_REQUIRE(*metadata == active_buffer_);

        {
            std::unique_lock<std::mutex> lock(buffer_count_lock_);
            buffer_count_++;
            LOCAL_REQUIRE(buffer_count_ <= backend_args_type::buffer_count);

            lock.unlock();
            has_buffer_.notify_one();
        }

        has_active_buffer_ = false;
        return {};
    }

    std::error_code set_interval(void *argp) {
        const auto new_interval = as_uint64(argp);
        LOCAL_CHECK(new_interval != interval_);

        interval_ = new_interval;

        if (!interval_)
            return {};

        {
            LOCAL_INFO("Counters must have been cleared before setting the interval.");
            LOCAL_CHECK(clear_flag_);
            clear_flag_ = false;
        }
        {
            LOCAL_INFO("Timestamp must have been requested before `set_interval` and after `clear`");
            LOCAL_CHECK(timstamp_flag_);
            timstamp_flag_ = false;
        }

        return {};
    }

    /** @return Interval set. */
    uint64_t interval() const { return interval_; }

    /** @return Number of times `clear` was called. */
    uint64_t clear_count() const { return clear_count_; }

    /** @return Mock timestamp value and advance time. */
    uint64_t timestamp_step() {
        timstamp_flag_ = true;

        const auto prev = timestamp_ns_;
        timestamp_ns_ += test::timestamp_dt;

        return prev;
    }

    /** Wait for space available to dump a hardware counters buffer. */
    void wait_buffer_available() {
        using namespace std::chrono_literals;

        std::unique_lock<std::mutex> lock(buffer_count_lock_);

        static auto constexpr ten_seconds = 10s;

        LOCAL_REQUIRE(has_buffer_.wait_for(lock, ten_seconds, [this]() { return buffer_count_ != 0; }));
    }

  private:
    static uint64_t as_uint64(void *arg) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(arg));
    }

    std::error_code dump(ioctl::vinstr::reader_event event) {
        {
            std::lock_guard<std::mutex> lock(buffer_count_lock_);

            if (buffer_count_ == 0)
                return std::make_error_code(std::errc::invalid_argument);

            --buffer_count_;
        }

        ioctl::vinstr::reader_metadata_with_cycles metadata{};

        metadata.metadata.timestamp = timestamp_step();
        metadata.metadata.event_id = event;
        metadata.metadata.buffer_idx = dump_count_ % backend_args_type::buffer_count;
        ++dump_count_;

        metadata.cycles.top = test::gpu_cycles;
        metadata.cycles.shader_cores = test::sc_cycles;

        constexpr size_t write_size = sizeof(metadata);
        LOCAL_REQUIRE(write(write_fd_, &metadata, write_size) == write_size);

        return {};
    }

    /** File descriptor for mock dumps reading. */
    int read_fd_{-1};
    /** File descriptor for mock dumps writing. */
    int write_fd_{-1};

    /** Number of times `dump` command was called. */
    uint64_t dump_count_{0};
    /** Mutex to protect buffer_count variable access. */
    std::mutex buffer_count_lock_;
    /** Conditional variable to signal there is a free buffer available for a dump. */
    std::condition_variable has_buffer_;

    /** Number of free buffers in the ring buffer. */
    uint64_t buffer_count_{backend_args_type::buffer_count};
    /** Interval set by the ioctl. */
    uint64_t interval_{};
    /** Number of times `clear` command was called. */
    uint64_t clear_count_{};
    /** Set when counters cleared, and unset when interval is changed. */
    bool clear_flag_{};
    /** Set when counters cleared, and unset when interval is changed. */
    bool timstamp_flag_{};

    /** Current timestamp value. */
    uint64_t timestamp_ns_{test::timestamp_start};
    /** True if there is an active buffer. */
    bool has_active_buffer_{};
    /** Reader metadata of the buffer being accessed. */
    ioctl::vinstr::reader_metadata active_buffer_{};
};

} // namespace mock

namespace test {

/** User data generation algorithm. */
class user_data_gen {
  public:
    /** @return user data for session start. */
    uint64_t start() {
        /* If session was stopped, reset the counters. */
        if (user_data_.is_stop) {
            user_data_.session_nr++;
            user_data_.sample_nr = 0;
            user_data_.is_stop = 0;
        }

        auto result = user_data_;
        result.sample_nr = 0;

        return result;
    }

    /** @return user data for manual sample. */
    uint64_t sample() {
        user_data_.sample_nr++;
        return user_data_;
    }

    /** @return user data for session stop. */
    uint64_t stop() {
        user_data_.is_stop = 1;

        auto result = user_data_;
        result.sample_nr = 0;

        return result;
    }

  private:
    struct user_data_type {
        uint64_t session_nr : 32;
        uint64_t sample_nr : 31;
        uint64_t is_stop : 1;

        operator uint64_t() const {
            uint64_t result{};

            static_assert(sizeof(*this) == sizeof(result), "Unexpected user_data size.");
            std::memcpy(&result, this, sizeof(*this));

            return result;
        }
    };

    user_data_type user_data_{};
};

struct configuration {
    uint64_t period_ns;
    ioctl::vinstr::reader_features features;
};

inline std::ostream &operator<<(std::ostream &os, const configuration &value) {
    namespace debug = hwcpipe::debug;

    return os << "configuration {\n"                                          //
              << debug::indent_level::push                                    //
              << debug::indent << ".period_ns = " << value.period_ns << ",\n" //
              << debug::indent << ".features = " << value.features << ",\n"   //
              << debug::indent_level::pop                                     //
              << debug::indent << "}";                                        //
}

/** Back-end test state. */
class state {
  public:
    state(configuration cfg)
        : configuration_(cfg)
        , syscall_iface_(init_syscall(vinstr_))
        , timestamp_iface_(init_timestamp(vinstr_))
        , backend_(
              mock::backend_args_type{
                  {
                      vinstr_.fd(),             //
                      configuration_.period_ns, //
                      {},                       //
                      test::extents,            //
                      {},                       //
                  },                            //
                  configuration_.features,      //
                  test::buffer_size,            //
                  test::sample_layout,
              },
              syscall_iface_, timestamp_iface_) {}

    /** @return vinstr mock. */
    mock::vinstr &vinstr() { return vinstr_; }

    /** @return back-end being tested. */
    mock::backend_type &backend() { return backend_; }

    std::error_code dump(uint64_t user_data) {
        if (configuration_.period_ns != 0)
            return vinstr_.periodic_dump();

        return backend_.request_sample(user_data);
    }

    void consume_sample(uint64_t user_data, uint64_t sample_nr = 0, uint64_t timestamp = test::timestamp_start) {
        using ioctl::vinstr::reader_features;

        hwcnt::sample_metadata expected{};

        expected.user_data = user_data;
        expected.sample_nr = sample_nr;
        expected.timestamp_ns_begin = timestamp;
        expected.timestamp_ns_end = timestamp + timestamp_dt;

        if (!!(configuration_.features & reader_features::cycles_top)) {
            expected.gpu_cycle = test::gpu_cycles;
            expected.sc_cycle = test::gpu_cycles;
        }

        if (!!(configuration_.features & reader_features::cycles_shader_core))
            expected.sc_cycle = test::sc_cycles;

        hwcnt::sample_metadata actual_sample{};
        hwcnt::sample_handle sample_handle{};
        LOCAL_REQUIRE(!backend_.get_sample(actual_sample, sample_handle));
        LOCAL_REQUIRE(!backend_.put_sample(sample_handle));

        LOCAL_CHECK(expected == actual_sample);
    }

  private:
    static mock::syscall::iface init_syscall(mock::vinstr &vinstr) {
        mock::syscall::iface result;

        result.ioctl_fn = [&vinstr](int fd, unsigned long command, void *argp) {
            LOCAL_CHECK(fd == vinstr.fd());

            switch (command) {
            case ioctl::vinstr::command::clear:
                return std::make_pair(vinstr.clear(), 0);
            case ioctl::vinstr::command::dump:
                return std::make_pair(vinstr.dump(), 0);
            case ioctl::vinstr::command::get_buffer_with_cycles:
                return std::make_pair(vinstr.get_buffer_with_cycles(argp), 0);
            case ioctl::vinstr::command::get_buffer:
                return std::make_pair(vinstr.get_buffer(argp), 0);
            case ioctl::vinstr::command::put_buffer:
                return std::make_pair(vinstr.put_buffer(argp), 0);
            case ioctl::vinstr::command::set_interval:
                return std::make_pair(vinstr.set_interval(argp), 0);
            }

            FAIL("Unknown command.");
            return std::make_pair(std::make_error_code(std::errc::function_not_supported), -1);
        };

        result.munmap_fn = [](void * /* addr */, size_t /* len */) { return std::error_code{}; };

        result.poll_fn = [](struct pollfd *fds, nfds_t nfds, int timeout) {
            constexpr int ten_seconds = 10000;
            const bool forever = timeout == -1;

            if (forever)
                timeout = ten_seconds;

            const int ret = poll(fds, nfds, timeout);

            LOCAL_REQUIRE(ret >= 0);

            if (forever) {
                LOCAL_INFO("poll() did not return in 10 seconds.");
                LOCAL_REQUIRE(ret > 0);
            }

            return std::make_pair(std::error_code{}, ret);
        };

        result.close_fn = [&vinstr](int fd) {
            LOCAL_CHECK(fd == vinstr.fd());
            return std::error_code{};
        };

        return result;
    }

    static mock::timestamp_iface init_timestamp(mock::vinstr &vinstr) {
        mock::timestamp_iface result;
        result.clock_gettime_fn = [&vinstr]() { return vinstr.timestamp_step(); };

        return result;
    }

    const configuration configuration_;
    mock::vinstr vinstr_;
    mock::syscall::iface syscall_iface_;
    mock::timestamp_iface timestamp_iface_;
    mock::backend_type backend_;
};

} // namespace test

SCENARIO("device::hwcnt::sampler::vinstr::backend", "[unit]") {
    using ioctl::vinstr::reader_features;

    test::configuration configuration{};
    configuration.period_ns = GENERATE(0, 1000);
    configuration.features = GENERATE(reader_features{},           //
                                      reader_features::cycles_top, //
                                      reader_features::cycles_top | reader_features::cycles_shader_core);

    test::state test_state(configuration);

    GIVEN("test configuration = " << configuration) {
        auto &backend = test_state.backend();
        auto &vinstr = test_state.vinstr();

        WHEN("stop() is called w/o start") {
            THEN("it is ignored w/o errors") { LOCAL_CHECK(!backend.stop(0)); }
        }

        WHEN("request_sample() is called w/o start") {
            THEN("it fails") { LOCAL_CHECK(backend.request_sample(0)); }
        }

        WHEN("start() is called") {
            test::user_data_gen user_data_gen;
            LOCAL_REQUIRE(!backend.start(user_data_gen.start()));

            THEN("sampling interval is set") { LOCAL_CHECK(vinstr.interval() == configuration.period_ns); }
            THEN("counters are cleared") { LOCAL_CHECK(vinstr.clear_count() == 1); }
            AND_THEN("start() is called again") {
                THEN("it is ignored w/o errors") { LOCAL_CHECK(!backend.start(0)); }
            }
            if (configuration.period_ns) {
                AND_THEN("request_sample() is called") {
                    THEN("it fails") {
                        LOCAL_INFO("Manual samples are not allowed on a periodic context.");
                        LOCAL_CHECK(backend.request_sample(user_data_gen.sample()));
                    }
                }
            }
            AND_WHEN("sample is dumped") {
                const uint64_t user_data_sample = user_data_gen.sample();
                LOCAL_REQUIRE(!test_state.dump(user_data_sample));

                THEN("the sample can be consumed") {
                    test_state.consume_sample(configuration.period_ns ? user_data_gen.start() : user_data_sample);
                }
            }

            AND_THEN("stop() is called") {
                LOCAL_REQUIRE(!backend.stop(user_data_gen.stop()));

                THEN("the last sample can be consumed") { test_state.consume_sample(user_data_gen.stop()); }
            }
        }

        WHEN("start, stop is maxed out") {
            uint64_t start_stop_counter = 0;

            for (test::user_data_gen user_data_gen;; ++start_stop_counter) {
                LOCAL_REQUIRE(!backend.start(user_data_gen.start()));

                if (backend.stop(user_data_gen.stop()))
                    break;

                LOCAL_REQUIRE(start_stop_counter <= mock::backend_args_type::buffer_count);
            }

            LOCAL_CHECK(start_stop_counter == mock::backend_args_type::buffer_count);

            AND_THEN("one sample is consumed") {
                hwcnt::sample_metadata sample_metadata{};
                hwcnt::sample_handle sample_handle{};
                LOCAL_REQUIRE(!backend.get_sample(sample_metadata, sample_handle));
                LOCAL_REQUIRE(!backend.put_sample(sample_handle));

                THEN("we can stop sampling with no errors") { LOCAL_REQUIRE(!backend.stop(0)); }
            }
            AND_THEN("discard() is called") {
                THEN("we can stop sampling") {
                    LOCAL_REQUIRE(!backend.discard());
                    LOCAL_REQUIRE(!backend.stop(0));
                }
            }
            AND_THEN("all samples are consumed one by one, and their user data is as expected") {
                test::user_data_gen user_data_gen;
                uint64_t timestamp_ns_begin = test::timestamp_start;

                for (size_t sample_nr = 0; sample_nr != start_stop_counter; ++sample_nr) {
                    user_data_gen.start();

                    test_state.consume_sample(user_data_gen.stop(), sample_nr, timestamp_ns_begin);
                    timestamp_ns_begin += test::timestamp_dt * 2;
                }
            }
        }
        WHEN("producer and consumer are called concurrently") {
            constexpr uint32_t num_sessions = 16;
            constexpr uint32_t num_samples = 128;

            bool producer_result = true;
            bool consumer_result = true;

            std::thread producer([&] {
                enable_catch2 = false;

                try {
                    test::user_data_gen user_data_gen;
                    for (uint32_t session = 0; session < num_sessions; ++session) {
                        LOCAL_REQUIRE(!backend.start(user_data_gen.start()));

                        for (uint32_t sample = 0; sample < num_samples; ++sample) {
                            vinstr.wait_buffer_available();
                            LOCAL_REQUIRE(!test_state.dump(user_data_gen.sample()));
                        }

                        vinstr.wait_buffer_available();
                        LOCAL_REQUIRE(!backend.stop(user_data_gen.stop()));
                    }
                } catch (std::exception &) {
                    /* No need to log. */
                }

                producer_result = thread_result;
            });

            std::thread consumer([&] {
                enable_catch2 = false;

                try {
                    test::user_data_gen user_data_gen;
                    uint64_t sample_nr = 0;
                    uint64_t timestamp_ns_begin = test::timestamp_start;

                    for (uint32_t session = 0; session < num_sessions; ++session) {
                        LOCAL_INFO(session);
                        user_data_gen.start();

                        for (uint32_t sample = 0; sample < num_samples; ++sample) {
                            LOCAL_INFO(sample);

                            test_state.consume_sample(                                                    //
                                configuration.period_ns ? user_data_gen.start() : user_data_gen.sample(), //
                                sample_nr,                                                                //
                                timestamp_ns_begin                                                        //
                            );

                            sample_nr++;
                            timestamp_ns_begin += test::timestamp_dt;
                        }

                        test_state.consume_sample(user_data_gen.stop(), sample_nr, timestamp_ns_begin);

                        sample_nr++;
                        timestamp_ns_begin += test::timestamp_dt * 2;
                    }
                } catch (std::exception &) {
                    /* No need to log. */
                }

                consumer_result = thread_result;
            });

            producer.join();
            consumer.join();

            LOCAL_CHECK(producer_result);
            LOCAL_CHECK(consumer_result);
        }
    }
}
