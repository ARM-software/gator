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

#include <device/handle.hpp>
#include <device/hwcnt/sample.hpp>
#include <device/hwcnt/sampler/configuration.hpp>
#include <device/hwcnt/sampler/manual.hpp>
#include <device/hwcnt/sampler/periodic.hpp>
#include <device/instance.hpp>

#include <chrono>
#include <ctime>
#include <queue>
#include <set>
#include <thread>
#include <vector>

#include <poll.h>

namespace dev = hwcpipe::device;

namespace hwcnt = hwcpipe::device::hwcnt;

/** The number of start stop pairs done by the test. */
static constexpr uint64_t num_sessions = 32;

/**
 * The number of manual samples taken / read per session.
 *
 * The number should be lower than num_sessions. It's better
 * to be a `P - 1`, where P is a co-prime number with `num_sessions`.
 * So each session starts from a different hwcnt ring buffer slot.
 */
static constexpr uint64_t num_samples_per_session = 30;

/** Nanoseconds per millisecond */
static constexpr uint64_t ns_per_ms = 1000000;

/** Sampling period (one ms). */
static constexpr uint64_t period_ns = 1 * ns_per_ms;

/** How long to wait for periodic sample to appear. */
static constexpr uint64_t timeout_ns = 16 * period_ns;

/**
 * Wait for sample to become available.
 *
 * @param reader[in]    Hardware counters reader.
 * @param timeout_ns[in] How long to wait.
 * @return True if sample is ready to be consumed, false otherwise.
 */
static bool wait_for_sample(const hwcnt::reader &reader, uint64_t timeout_ns) {
    pollfd fds{};

    fds.fd = reader.get_fd();
    fds.events = POLLIN;

    static constexpr nfds_t num_fds = 1;
    const auto timeout_ms = static_cast<int>(timeout_ns / ns_per_ms);

    const auto result = poll(&fds, num_fds, timeout_ms);

    return result == num_fds;
}

/**
 * Convert `configuration::enable_map_type` to prfcnt_en mask.
 *
 * @param enable_mask[in]    Enable mask.
 * @return Value converted.
 */
static uint32_t shrink_enable_mask(hwcnt::sampler::configuration::enable_map_type enable_mask) {
    uint32_t result{};

    static constexpr size_t enable_per_bit = 4;
    static constexpr size_t bits_per_byte = 8;
    static constexpr size_t enable_mask_size = sizeof(uint32_t) * enable_per_bit * bits_per_byte;

    static_assert(enable_mask.size() == enable_mask_size, "Unexpected enable_mask size");

    for (uint32_t i = 0, j = 0; i < enable_mask.size(); i += enable_per_bit, ++j) {
        if (enable_mask[i + 0] || enable_mask[i + 1] || enable_mask[i + 2] || enable_mask[i + 3])
            result |= (1u << j);
    }

    return result;
}

/** Sampler configuration type. */
using configuration_type = std::vector<hwcnt::sampler::configuration>;

/** Counters values reader. */
class values_reader {
  public:
    values_reader(const hwcnt::block_extents &extents)
        : extents_(extents) {}

    /** Read the GPU timestamp. */
    uint64_t timestamp(const void *values) const {
        static constexpr uint64_t shift = 32;
        const uint64_t timestamp_lo = read_value_uint32(values, timestamp_lo_idx);
        const uint64_t timestamp_hi = read_value_uint32(values, timestamp_hi_idx);

        return timestamp_lo | (timestamp_hi << shift);
    }

    /**
     * Read prfcnt_en mask value.
     *
     * @param values[in] Hardware counters values pointer.
     * @return prfcnt_en mask value read.
     */
    uint32_t prfcnt_en(const void *values) const { return read_value_uint32(values, prfcnt_en_idx); }

    /** Read all values from the hardware counters buffer. */
    void touch_values(const void *values) const {
        volatile uint64_t counter{};

        for (size_t i = 0; i < extents_.counters_per_block(); ++i)
            counter = read_value(values, i);

        static_cast<void>(counter);
    }

    /**
     * Read one value from the hardware counters buffer.
     *
     * @param values[in]   Hardware counters values pointer.
     * @param index[in]    Counter index to read.
     * @return Value read.
     */
    uint64_t read_value(const void *values, size_t index) const {
        switch (extents_.values_type()) {
        case hwcnt::sample_values_type::uint32:
            return static_cast<const uint32_t *>(values)[index];
        case hwcnt::sample_values_type::uint64:
            return static_cast<const uint64_t *>(values)[index];
        }

        FAIL();

        return 0;
    }

  private:
    /**
     * Read one `uint32_t` value from the hardware counters buffer.
     *
     * @param values[in]   Hardware counters values pointer.
     * @param index[in]    Counter index to read.
     * @return Value read.
     */
    uint32_t read_value_uint32(const void *values, size_t index) const {
        static constexpr uint64_t mask = 0xFFFFFFFF;

        const auto value = read_value(values, index);
        CHECK((value & mask) == value);

        return value & mask;
    }

    /** Index of performance counters enable mask. */
    static constexpr size_t timestamp_lo_idx = 0;
    /** Index of performance counters enable mask. */
    static constexpr size_t timestamp_hi_idx = 1;
    /** Index of performance counters enable mask. */
    static constexpr size_t prfcnt_en_idx = 2;
    /** Block extents reference. */
    const hwcnt::block_extents &extents_;
};

/** Sample's user_data and timestamp expectations. */
class sample_expectation {
  public:
    /**
     * Constructor.
     *
     * @param user_data[in]    Expected user_data value.
     */
    sample_expectation(uint64_t user_data)
        : user_data_(user_data)
        , timestamp_ns_lower_(clock_gettime())
        , timestamp_ns_upper_(0) {}

    /** Default copy. */
    sample_expectation(const sample_expectation &) = default;
    /** Default assign. */
    sample_expectation &operator=(const sample_expectation &) = default;

    /**
     * Check sample's user_data and timestamp fields.
     *
     * If timestamp's upper bound was not set, it is set to NOW.
     *
     * @param user_data[in] User data to check.
     * @param timestamp_ns[in] Timestamp to check.
     */
    void check(uint64_t user_data, uint64_t timestamp_ns) {
        if (timestamp_ns_upper_ == 0)
            end();

        CHECK(user_data == user_data_);
        CHECK(timestamp_ns > timestamp_ns_lower_);
        CHECK(timestamp_ns < timestamp_ns_upper_);
    }

    /** Set timestamp's upper bound to NOW. */
    void end() { timestamp_ns_upper_ = clock_gettime(); }

    /** @return user_data value expected. */
    uint64_t user_data() const { return user_data_; }

  private:
    /** @return clock monotonic raw, if supported, or clock monotonic timestamp. */
    static uint64_t clock_gettime() {
        timespec now{};
#if defined(CLOCK_MONOTONIC_RAW)
        ::clock_gettime(CLOCK_MONOTONIC_RAW, &now);
#elif defined(CLOCK_MONOTONIC)
        ::clock_gettime(CLOCK_MONOTONIC, &now);
#else
#error "No clock id defined"
#endif
        constexpr uint64_t nsec_per_sec = 1000000000;

        return now.tv_sec * nsec_per_sec + now.tv_nsec;
    }

    /** User data. */
    uint64_t user_data_;
    /** Lower bound for the sample timestamp. */
    uint64_t timestamp_ns_lower_;
    /** Upper bound for the sample timestamp. */
    uint64_t timestamp_ns_upper_;
};

/** Sample validator class. */
class sample_validator {
  public:
    /**
     * Sample validator constructor.
     *
     * @param instance[in]      Instance.
     * @param configs[in]       Sampler configuration.
     * @param reader[in,out]    Hardware counters reader.
     */
    sample_validator(const dev::instance &instance, const configuration_type &configs, hwcnt::reader &reader)
        : reader_(reader)
        , values_reader_(reader.get_block_extents())
        , expected_prfcnt_en_(init_expected_prfcnt_en(configs)) {
        validate_block_extents(instance, configs);
    }

    /**
     * Read one sample, and check if the expectations hold.
     *
     * @param expectation[in] Sample's expectations.
     * @param timeout_ns[in] Sample waiting timeout (nanoseconds).
     */
    void validate_one(sample_expectation expectation, uint64_t timeout_ns) {
        uint64_t user_data = 0;
        uint64_t timestamp_ns = 0;

        std::tie(user_data, timestamp_ns) = validate(timeout_ns);

        expectation.check(user_data, timestamp_ns);
    }

    /**
     * Read many samples until @p stop_expectation is met.
     *
     * @param expectation[in]      Sample's expectations.
     * @param stop_expectation[in] Stop sample's expectations.
     */
    void validate_many(sample_expectation expectation, sample_expectation stop_expectation) {
        uint64_t user_data = 0;
        uint64_t timestamp_ns = 0;

        for (;;) {
            /* The stop sample is sync, therefore all samples must be ready. */
            static constexpr uint64_t timeout_ns = 0;
            std::tie(user_data, timestamp_ns) = validate(timeout_ns);

            if (user_data == stop_expectation.user_data()) {
                stop_expectation.check(user_data, timestamp_ns);
                break;
            }

            expectation.check(user_data, timestamp_ns);
        }
    }

  private:
    using expected_prfcnt_en_type = std::array<uint32_t, hwcnt::block_extents::num_block_types>;
    using values_set_type = std::set<const void *>;

    /**
     * Check if block extents agree with the counters subscribed.
     *
     * @param instance[in] Instance.
     * @param configs[in] Sampler configuration.
     */
    void validate_block_extents(const dev::instance &instance, const configuration_type &configs) {
        const auto block_extents_instance = instance.get_hwcnt_block_extents();
        const auto block_extents_reader = reader_.get_block_extents();

        auto is_block_enabled = [&](hwcnt::block_type block_type) {
            for (const auto &config : configs) {
                if (config.type == block_type)
                    return true;
            }
            return false;
        };

        REQUIRE(block_extents_instance.counters_per_block() == block_extents_reader.counters_per_block());
        REQUIRE(block_extents_instance.values_type() == block_extents_reader.values_type());

        static constexpr std::array<hwcnt::block_type, 4> block_types = {{
            hwcnt::block_type::fe,
            hwcnt::block_type::tiler,
            hwcnt::block_type::memory,
            hwcnt::block_type::core,
        }};

        for (const auto block_type : block_types) {
            if (is_block_enabled(block_type))
                REQUIRE(block_extents_instance.num_blocks_of_type(block_type) ==
                        block_extents_reader.num_blocks_of_type(block_type));
            else
                REQUIRE(block_extents_reader.num_blocks_of_type(block_type) == 0);
        }
    }

    /**
     * Validate HWCNT blocks of a sample.
     *
     * @param sample[in] Sample to validate.
     */
    void validate_blocks(const hwcnt::sample &sample) {
        hwcnt::block_extents::num_blocks_of_type_type num_blocks_of_type{};

        const auto block_extents = reader_.get_block_extents();
        size_t num_blocks{0};

        values_set_type values;

        uint64_t timestamp_gpu_max{};

        for (const auto &block : sample.blocks()) {
            const auto block_type_raw = static_cast<size_t>(block.type);
            REQUIRE(block_type_raw < num_blocks_of_type.size());

            CHECK(block.index == num_blocks_of_type[block_type_raw]);
            CHECK(block.set == hwcnt::prfcnt_set::primary);

            CAPTURE(block.type);
            CAPTURE(block.index);

            values.insert(block.values);

            /* Check if we can read the entire counters buffer. */
            values_reader_.touch_values(block.values);

            const auto prfcnt_en = values_reader_.prfcnt_en(block.values);
            CHECK(prfcnt_en == expected_prfcnt_en(block.type));

            const auto timestamp_gpu = values_reader_.timestamp(block.values);

            /* If GPU timestamp is supported, make sure it is growing. */
            if (timestamp_gpu != 0 && sample_nr_ != 0)
                CHECK(timestamp_gpu > last_timestamp_gpu_);

            timestamp_gpu_max = std::max(timestamp_gpu_max, timestamp_gpu);

            num_blocks_of_type[block_type_raw]++;
            num_blocks++;

            REQUIRE(num_blocks_of_type[block_type_raw] <= block_extents.num_blocks_of_type(block.type));
            REQUIRE(num_blocks <= block_extents.num_blocks());
        }

        CHECK(num_blocks == block_extents.num_blocks());
        CHECK(values.size() == num_blocks);

        {
            INFO("Value pointers must differ from the previous sample.");

            for (const auto value : last_values_)
                CHECK(values.count(value) == 0);
        }

        last_timestamp_gpu_ = timestamp_gpu_max;
        last_values_ = values;
    }

    /**
     * Read one sample and validate it.
     *
     * @param timeout_ns[in] Sample waiting timeout (nanoseconds).
     * @return A pair of user_data and sample end timestamp.
     */
    std::pair<uint64_t, uint64_t> validate(uint64_t timeout_ns) {
        std::error_code ec;
        uint64_t user_data = 0;
        uint64_t timestamp_ns = 0;

        {
            CAPTURE(sample_nr_);

            REQUIRE(wait_for_sample(reader_, timeout_ns));

            hwcnt::sample sample(reader_, ec);
            REQUIRE(sample);

            const auto metadata = sample.get_metadata();

            user_data = metadata.user_data;
            timestamp_ns = metadata.timestamp_ns_end;

            CHECK(metadata.sample_nr == sample_nr_);
            CHECK(metadata.timestamp_ns_begin <= metadata.timestamp_ns_end);

            if (sample_nr_ != 0)
                CHECK(metadata.timestamp_ns_begin >= last_timestamp_ns_);

            validate_blocks(sample);

            last_timestamp_ns_ = metadata.timestamp_ns_end;
            ++sample_nr_;
        }

        CHECK(!ec);
        return std::make_pair(user_data, timestamp_ns);
    }

    /**
     * Get expected prfcnt_en mask value for a block type.
     *
     * @param block_type[in] Block type to get the mask for.
     * @return prfcnt_en mask value.
     */
    uint32_t expected_prfcnt_en(hwcnt::block_type block_type) const {
        const auto block_type_raw = static_cast<size_t>(block_type);
        REQUIRE(block_type_raw < expected_prfcnt_en_.size());

        return expected_prfcnt_en_[block_type_raw];
    }

    /**
     * Initialize expected prfcnt_en mask values.
     *
     * @param configs[in] Sampler configuration.
     * @return expected prfcnt_en mask values initialized.
     */
    static expected_prfcnt_en_type init_expected_prfcnt_en(const configuration_type &configs) {
        expected_prfcnt_en_type result{};

        for (const auto &config : configs)
            result[static_cast<size_t>(config.type)] = shrink_enable_mask(config.enable_map);

        return result;
    }

    /** Hardware counters reader. */
    hwcnt::reader &reader_;
    /** Hardware counters values reader. */
    values_reader values_reader_;
    /** Expected sampler number. */
    uint64_t sample_nr_{0};
    /** Last timestamp read from the sample metadata. */
    uint64_t last_timestamp_ns_{0};
    /** Last GPU timestamp read from the counters buffer (if available).*/
    uint64_t last_timestamp_gpu_{0};
    /** The set of last counter buffers pointers. */
    values_set_type last_values_;
    /** Expected performance counters enable masks. */
    expected_prfcnt_en_type expected_prfcnt_en_{};
};

static uint64_t generate_user_data(uint64_t session_nr, uint64_t sample_nr) {
    static constexpr uint64_t session_nr_shift = 32;
    static constexpr uint64_t mask = 0xFFFFFFFF;

    return ((session_nr & mask) << session_nr_shift) | (sample_nr & mask);
}

/**
 * Test samplers constructors with bad configuration.
 *
 * @param[in] instance Device instance.
 */
static void test_sampler_bad_configuration(dev::instance &instance) {
    static constexpr auto enable_mask = 0b1111;

    SECTION("duplicate entry") {
        const configuration_type configuration{{
            {hwcnt::block_type::fe, hwcnt::prfcnt_set::primary, enable_mask},
            {hwcnt::block_type::fe, hwcnt::prfcnt_set::primary, enable_mask},
        }};

        SECTION("manual") {
            hwcnt::sampler::manual sampler(instance, configuration.data(), configuration.size());
            REQUIRE(!sampler);
        }
        SECTION("periodic") {
            hwcnt::sampler::periodic sampler(instance, period_ns, configuration.data(), configuration.size());
            REQUIRE(!sampler);
        }
    }
    SECTION("inconsistent prfcnt_set") {
        const configuration_type configuration{{
            {hwcnt::block_type::fe, hwcnt::prfcnt_set::primary, enable_mask},
            {hwcnt::block_type::tiler, hwcnt::prfcnt_set::secondary, enable_mask},
        }};

        SECTION("manual") {
            hwcnt::sampler::manual sampler(instance, configuration.data(), configuration.size());
            REQUIRE(!sampler);
        }
        SECTION("periodic") {
            hwcnt::sampler::periodic sampler(instance, period_ns, configuration.data(), configuration.size());
            REQUIRE(!sampler);
        }
    }
    SECTION("zero period") {
        const configuration_type configuration{{
            {hwcnt::block_type::fe, hwcnt::prfcnt_set::primary, enable_mask},
        }};
        SECTION("periodic") {
            hwcnt::sampler::periodic sampler(instance, 0, configuration.data(), configuration.size());
            REQUIRE(!sampler);
        }
    }
}

/**
 * Test samplers in normal conditions: "good" configuration, no overflows.
 *
 * @param[in] instance Device instance.
 */
static void test_sampler_good(dev::instance &instance) {
    /* The test sets enable masks to different values to subscribe some
     * counters, and then expects that this mask is shrunk (one bit to
     * enable four counters) and then dumped to the counters buffer at
     * index #3. However, a GPU _may_ change the mask before dumping,
     * to mask out unsupported counters.
     *
     * There are three possible options:
     *
     *   1. The mask is passed through with no filtering, even if enabled counters
     *      are unsupported.
     *   2. If there are up to 64 counters, 0xFFFF mask is applied. But the GPU may not
     *      support all 64 for a block.
     *   3. If a block supports < 64 counters, all unsupported counters are masked
     *      out strictly. E.g. if only 8 counters are supported 0b11 is applied.
     *
     * The enable masks below were carefully selected such that they are _never_
     * masked out for _any_ GPU we know of:
     */
    static constexpr uint64_t enable_mask_fe = 0b1111;
    static constexpr uint64_t enable_mask_tiler = 0b11111111;
    static constexpr uint64_t enable_mask_memory = 0b111111111111;
    static constexpr uint64_t enable_mask_core = 0b1111111111111111;

    const configuration_type configs = GENERATE_COPY( //
        configuration_type{{
            {hwcnt::block_type::fe, hwcnt::prfcnt_set::primary, enable_mask_fe},
        }},
        configuration_type{{
            {hwcnt::block_type::tiler, hwcnt::prfcnt_set::primary, enable_mask_tiler},
        }},
        configuration_type{{
            {hwcnt::block_type::memory, hwcnt::prfcnt_set::primary, enable_mask_memory},
        }},
        configuration_type{{
            {hwcnt::block_type::core, hwcnt::prfcnt_set::primary, enable_mask_core},
        }},
        configuration_type{{
            {hwcnt::block_type::fe, hwcnt::prfcnt_set::primary, enable_mask_fe},
            {hwcnt::block_type::tiler, hwcnt::prfcnt_set::primary, enable_mask_tiler},
            {hwcnt::block_type::memory, hwcnt::prfcnt_set::primary, enable_mask_memory},
            {hwcnt::block_type::core, hwcnt::prfcnt_set::primary, enable_mask_core},
        }});

    SECTION("manual") {
        hwcnt::sampler::manual sampler(instance, configs.data(), configs.size());
        REQUIRE(sampler);

        sample_validator validator{instance, configs, sampler.get_reader()};
        std::queue<sample_expectation> expectations_queue;

        for (uint64_t session_nr = 0; session_nr < num_sessions; ++session_nr) {
            REQUIRE(!sampler.accumulation_start());

            for (uint64_t sample_nr = 0; sample_nr < num_samples_per_session; ++sample_nr) {
                sample_expectation expectation{generate_user_data(session_nr, num_samples_per_session)};

                REQUIRE(!sampler.request_sample(expectation.user_data()));

                expectation.end();
                expectations_queue.push(expectation);
            }

            {
                sample_expectation expectation{generate_user_data(session_nr, num_samples_per_session)};

                REQUIRE(!sampler.accumulation_stop(expectation.user_data()));

                expectation.end();
                expectations_queue.push(expectation);
            }

            REQUIRE(expectations_queue.size() == (num_samples_per_session + 1));
            while (!expectations_queue.empty()) {
                validator.validate_one(expectations_queue.front(), 0);
                expectations_queue.pop();
            }
            CHECK(!wait_for_sample(sampler.get_reader(), 0));
        }
    }
    SECTION("periodic") {
        hwcnt::sampler::periodic sampler(instance, period_ns, configs.data(), configs.size());
        REQUIRE(sampler);

        sample_validator validator{instance, configs, sampler.get_reader()};

        for (uint64_t session_nr = 0; session_nr < num_sessions; ++session_nr) {
            const sample_expectation expectation_start{generate_user_data(session_nr, 0)};

            REQUIRE(!sampler.sampling_start(expectation_start.user_data()));

            for (uint64_t sample_nr = 0; sample_nr < num_samples_per_session; ++sample_nr) {
                validator.validate_one(expectation_start, timeout_ns);
            }

            sample_expectation expectation_stop{generate_user_data(session_nr, 1)};
            REQUIRE(!sampler.sampling_stop(expectation_stop.user_data()));
            expectation_stop.end();

            validator.validate_many(expectation_start, expectation_stop);
            CHECK(!wait_for_sample(sampler.get_reader(), 0));
        }
    }
}

/**
 * Test samplers when overflow happens.
 *
 * The test starts a profiling session, overflows the counters ring
 * buffer, and then stops the profiling session. When session is stopped,
 * there must be a sample corresponding to `accumulation_stop` / `sampling stop`.
 *
 * @param[in] instance Device instance.
 */
static void test_sampler_overflow(dev::instance &instance) {
    static constexpr auto enable_mask = 0b1111;
    const configuration_type configuration{{
        {hwcnt::block_type::fe, hwcnt::prfcnt_set::primary, enable_mask},
    }};

    static constexpr uint64_t user_data_sample = 42;
    static constexpr uint64_t user_data_stop = 43;
    /* Arbitrary high. */
    static constexpr uint64_t max_samples = 256;

    SECTION("manual") {
        hwcnt::sampler::manual sampler(instance, configuration.data(), configuration.size());
        REQUIRE(sampler);

        sample_validator validator{instance, configuration, sampler.get_reader()};

        uint64_t num_samples = 0;

        std::queue<sample_expectation> expectations_queue;

        REQUIRE(!sampler.accumulation_start());

        /* Exhaust the ring buffer space. */
        for (; num_samples < max_samples; ++num_samples) {
            sample_expectation expectation{user_data_sample};

            if (sampler.request_sample(user_data_sample))
                break;

            expectation.end();
            expectations_queue.push(expectation);
        }

        {
            sample_expectation expectation{user_data_stop};

            /* There are two possible implementations:
             * * There is at least one slot reserved for the stop command.
             * * Stop fails, we must read at least one sample to free the slot.
             */
            if (sampler.accumulation_stop(user_data_stop)) {
                validator.validate_one(expectations_queue.front(), 0);
                expectations_queue.pop();

                REQUIRE(!sampler.accumulation_stop(user_data_stop));
            }

            expectation.end();
            expectations_queue.push(expectation);
        }

        /* Drain the samples and validate them. */
        while (!expectations_queue.empty()) {
            validator.validate_one(expectations_queue.front(), 0);
            expectations_queue.pop();
        }
        CHECK(!wait_for_sample(sampler.get_reader(), 0));
    }
    SECTION("periodic") {
        hwcnt::sampler::periodic sampler(instance, period_ns, configuration.data(), configuration.size());
        REQUIRE(sampler);

        sample_validator validator{instance, configuration, sampler.get_reader()};

        const sample_expectation expectation_start{user_data_sample};
        REQUIRE(!sampler.sampling_start(user_data_sample));

        /* Sleep until the buffer is overflown. */
        std::this_thread::sleep_for(std::chrono::nanoseconds(max_samples * period_ns));

        /* There are two possible implementations:
         * * There is at least one slot reserved for the stop command.
         * * Stop fails, but periodic sampling is stopped. We must read at
         *   least one sample to free the slot.
         */
        const sample_expectation expectation_stop{user_data_stop};
        if (sampler.sampling_stop(user_data_stop)) {
            validator.validate_one(expectation_start, 0);

            REQUIRE(!sampler.sampling_stop(user_data_stop));
        }

        validator.validate_many(expectation_start, expectation_stop);
        CHECK(!wait_for_sample(sampler.get_reader(), 0));
    }
}

TEST_CASE("ETE device::hwcnt::sampler", "[end-to-end]") {
    auto handle = dev::handle::create();
    REQUIRE(handle);

    auto instance = dev::instance::create(*handle);
    REQUIRE(instance);

    SECTION("Bad configuration") { test_sampler_bad_configuration(*instance); }
    SECTION("Good configuration, no overflows") { test_sampler_good(*instance); }
    SECTION("Good configuration, overflows") { test_sampler_overflow(*instance); }
}
