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
#include <device/instance.hpp>
#include <device/product_id.hpp>

#include <array>
#include <cstdlib>
#include <cstring>
#include <functional>

namespace test {
/**
 * Return true if a value is a power of two.
 *
 * @param value[in]    Value to check.
 * @return true if a value is a power of two, false otherwise.
 */
bool is_power_of_two(uint64_t value) { return value && !(value & (value - 1)); }

/**
 * Return the number of bits set.
 *
 * @param value[in]    The value to count bits for.
 * @return the number of bits set.
 */
uint32_t popcount(uint64_t value) {
    uint32_t result = 0;
    for (; value; value >>= 1)
        if (value & 1)
            ++result;
    return result;
}

/**
 * Check if this GPU is a CSF GPU.
 *
 * @param product_id[in] Product id to check.
 * @return true
 */
bool is_csf_gpu(uint64_t gpu_id) {
    using hwcpipe::device::product_id;
    const product_id pid{gpu_id};

    return pid.get_gpu_frontend() == product_id::gpu_frontend::csf;
}

} // namespace test

/** RAII class to `setenv` in constructor and restore it in destructor. */
class setenv_guard {
  public:
    setenv_guard(const char *var_name, const char *var_value)
        : var_name_(var_name) {
        const auto env = getenv(var_name);

        if (env != nullptr)
            orig_value_ = env;

        env_empty_ = !env;

        REQUIRE(!setenv(var_name, var_value, 1));
    }

    ~setenv_guard() {
        if (env_empty_)
            REQUIRE(!unsetenv(var_name_));
        else
            REQUIRE(!setenv(var_name_, orig_value_.c_str(), 1));
    }

  private:
    /** Name of the variable changed. */
    const char *var_name_{};
    /** Original variable valuem if any. */
    std::string orig_value_{};
    /** True, if the variable was set prior to the change. */
    bool env_empty_{false};
};

SCENARIO("ETE device::instance", "[end-to-end]") {
    namespace device = hwcpipe::device;

    auto handle = device::handle::create();
    REQUIRE(handle);

    GIVEN("device::instance") {
        auto instance = device::instance::create(*handle);
        REQUIRE(instance);

        WHEN("device::instance::get_constants is called") {
            const auto constants = instance->get_constants();

            THEN("constants::gpu_id is non zero") { CHECK(constants.gpu_id != 0); }

            THEN("constants::fw_version is non zero for CSF GPUs") {
                if (test::is_csf_gpu(constants.gpu_id))
                    CHECK(constants.fw_version != 0);
                else
                    CHECK(constants.fw_version == 0);
            }

            /* From the architecture perspective, axi_bus_width is unbound.
             * However, practically, we expect it to belong [32, 512].
             */
            THEN("32 <= constants::axi_bus_width <= 512 and power of two") {
                CAPTURE(constants.axi_bus_width);
                CHECK(constants.axi_bus_width >= 32);
                CHECK(constants.axi_bus_width <= 512);
                CHECK(test::is_power_of_two(constants.axi_bus_width));
            }

            THEN("0 < constants::num_shader_cores <= 64") {
                CHECK(constants.num_shader_cores <= 64);
                CHECK(constants.num_shader_cores > 0);
            }

            THEN("constants::shader_core_mask > 0") { CHECK(constants.shader_core_mask > 0); }

            THEN("constants::num_shader_cores agrees with constants::shader_core_mask") {
                CAPTURE(constants.shader_core_mask);
                CHECK(constants.num_shader_cores == test::popcount(constants.shader_core_mask));
            }

            THEN("0 < constants::num_l2_slices <= 16") {
                CHECK(constants.num_l2_slices > 0);
                CHECK(constants.num_l2_slices <= 16);
            }

            /* From the architecture perspective, the l2 slice size is unbound,
             * However, practically, we expect it to belong [8 Kb, 4 Mb].
             */
            THEN("constants::l2_slice_size > 0 and power of two") {
                static constexpr uint64_t l2_slice_size_min = 8ULL * (1ULL << 10ULL);
                static constexpr uint64_t l2_slice_size_max = 4ULL * (1ULL << 20ULL);
                CHECK(constants.l2_slice_size >= l2_slice_size_min);
                CHECK(constants.l2_slice_size <= l2_slice_size_max);
                CHECK(test::is_power_of_two(constants.l2_slice_size));
            }

            /* TODO GPUCORE-33051: Update this test when num_exec_engines is fixed. */
            THEN("constants::num_exec_engines == 0") { CHECK(constants.num_exec_engines == 0); }

            THEN("constants::tile_size == 16") { CHECK(constants.tile_size == 16); }

            THEN("0 <= constants::warp_width <= 16") { CHECK(constants.warp_width <= 16); }

            AND_WHEN("device::instance::get_hwcnt_block_extents is called") {
                const auto extents = instance->get_hwcnt_block_extents();

                THEN("values are in reasonable boundaries") {
                    using device::hwcnt::block_type;
                    using device::hwcnt::sample_values_type;

                    CHECK(extents.num_blocks_of_type(block_type::fe) == 1);
                    CHECK(extents.num_blocks_of_type(block_type::tiler) == 1);
                    CHECK(extents.num_blocks_of_type(block_type::memory) <=
                          static_cast<uint8_t>(constants.num_l2_slices));
                    CHECK(extents.num_blocks_of_type(block_type::core) ==
                          static_cast<uint8_t>(constants.num_shader_cores));

                    const size_t num_blocks = 1 + 1 + constants.num_l2_slices + constants.num_shader_cores;
                    CHECK(num_blocks == extents.num_blocks());

                    CAPTURE(extents.counters_per_block());
                    CHECK((extents.counters_per_block() == 64 || extents.counters_per_block() == 128));

                    CAPTURE(extents.values_type());
                    CHECK((extents.values_type() == sample_values_type::uint32 ||
                           extents.values_type() == sample_values_type::uint64));
                }
            }
        }
    }
    GIVEN("device::handle") {
        WHEN("Multiple instances are created") {
            auto instance0 = device::instance::create(*handle);
            auto instance1 = device::instance::create(*handle);
            auto instance2 = device::instance::create(*handle);
            auto instance3 = device::instance::create(*handle);

            THEN("They are all valid") {
                CHECK(instance0 != nullptr);
                CHECK(instance1 != nullptr);
                CHECK(instance2 != nullptr);
                CHECK(instance3 != nullptr);
            }
        }
        WHEN("HWCPIPE_BACKEND_INTERFACE set to all known back-end types") {
            static const std::array<const char *, 5> known_backends_types = {
                "vinstr",            //
                "vinstr_pre_r21",    //
                "kinstr_prfcnt",     //
                "kinstr_prfcnt_wa",  //
                "kinstr_prfcnt_bad", //
            };

            uint32_t num_backends = 0;

            for (const auto backend_type : known_backends_types) {
                const setenv_guard iface_env{"HWCPIPE_BACKEND_INTERFACE", backend_type};

                auto instance = device::instance::create(*handle);
                if (instance != nullptr)
                    num_backends++;
            }

            THEN("At least one back-end is supported") { CHECK(num_backends > 0); }
        }
        WHEN("HWCPIPE_BACKEND_INTERFACE is set to an invalid back-end type") {
            const auto backend_type = GENERATE("", "abcd");

            const setenv_guard iface_env{"HWCPIPE_BACKEND_INTERFACE", backend_type};

            THEN("instance creation fails") { CHECK(device::instance::create(*handle) == nullptr); }
        }
    }
}
