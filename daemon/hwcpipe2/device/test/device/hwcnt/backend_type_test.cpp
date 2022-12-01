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

#include <device/hwcnt/backend_type.hpp>
#include <device/kbase_version.hpp>
#include <device/product_id.hpp>

#include <ostream>
#include <string>
#include <system_error>
#include <utility>

namespace hwcnt = hwcpipe::device::hwcnt;

inline hwcnt::backend_types_set backend_types() { return hwcnt::backend_types_set{}; }

template <typename head_t, typename... tail_t>
inline hwcnt::backend_types_set backend_types(head_t head, tail_t &&...tail) {
    auto result = backend_types(tail...);
    result.set(static_cast<size_t>(head));

    return result;
}

namespace mock {
class getenv_iface {
  public:
    getenv_iface(const char *env)
        : env_(env) {}

    const char *getenv(const char *name) {
        CHECK(std::string(name) == "HWCPIPE_BACKEND_INTERFACE");
        return env_;
    }

  private:
    const char *env_;
};
} // namespace mock

TEST_CASE("device::hwcnt::backend_type", "[unit]") {
    SECTION("from_str") {
        hwcnt::backend_type expected{};
        const char *str_to_parse{};
        std::error_code ec;
        std::error_code expected_ec;
        static const std::error_code einval = std::make_error_code(std::errc::invalid_argument);

        std::tie(str_to_parse, expected_ec, expected) = GENERATE_COPY(
            std::make_tuple("vinstr", std::error_code{}, hwcnt::backend_type::vinstr),
            std::make_tuple("vinstr_pre_r21", std::error_code{}, hwcnt::backend_type::vinstr_pre_r21),
            std::make_tuple("kinstr_prfcnt", std::error_code{}, hwcnt::backend_type::kinstr_prfcnt),
            std::make_tuple("kinstr_prfcnt_wa", std::error_code{}, hwcnt::backend_type::kinstr_prfcnt_wa),
            std::make_tuple("kinstr_prfcnt_bad", std::error_code{}, hwcnt::backend_type::kinstr_prfcnt_bad),
            std::make_tuple("", einval, hwcnt::backend_type{}),
            std::make_tuple("invalid", einval, hwcnt::backend_type{}));

        hwcnt::backend_type actual{};

        std::tie(ec, actual) = hwcnt::backend_type_from_str(str_to_parse);

        CAPTURE(str_to_parse);

        CHECK(ec == expected_ec);
        if (!ec)
            CHECK(actual == expected);
    }
    SECTION("discover") {
        using kbase_version = hwcpipe::device::kbase_version;
        using ioctl_iface_type = hwcpipe::device::ioctl_iface_type;
        using product_id = hwcpipe::device::product_id;
        using bt = hwcnt::backend_type;

        kbase_version version{};
        product_id pid{0};
        hwcnt::backend_types_set expected{};

        static constexpr product_id product_id_g78{9, 2};
        static constexpr product_id product_id_g710{10, 2};
        static constexpr product_id product_id_gtux{11, 2};

        const char *test_name = nullptr;

        std::tie(test_name, version, pid, expected) = GENERATE_COPY(                     //
            std::make_tuple(                                                             //
                "JM GPU before R21",                                                     //
                kbase_version{0, 0, ioctl_iface_type::jm_pre_r21},                       //
                product_id_g78,                                                          //
                backend_types(bt::vinstr_pre_r21)),                                      //
            std::make_tuple(                                                             //
                "JM GPU w/o kinstr_prfcnt",                                              //
                kbase_version{11, 34 - 1, ioctl_iface_type::jm_post_r21},                //
                product_id_g78,                                                          //
                backend_types(bt::vinstr)),                                              //
            std::make_tuple(                                                             //
                "JM GPU with kinstr_prfcnt",                                             //
                kbase_version{11, 34, ioctl_iface_type::jm_post_r21},                    //
                product_id_g78,                                                          //
                backend_types(bt::vinstr, bt::kinstr_prfcnt_bad, bt::kinstr_prfcnt_wa)), //
            std::make_tuple(                                                             //
                "CSF GPU w/o kinstr_prfcnt",                                             //
                kbase_version{1, 10 - 1, ioctl_iface_type::csf},                         //
                product_id_g710,                                                         //
                backend_types(bt::vinstr)),                                              //
            std::make_tuple(                                                             //
                "CSF GPU with kinstr_prfcnt",                                            //
                kbase_version{1, 10, ioctl_iface_type::csf},                             //
                product_id_g710,                                                         //
                backend_types(bt::vinstr, bt::kinstr_prfcnt_bad, bt::kinstr_prfcnt_wa)), //
            std::make_tuple(                                                             //
                "tTUx GPU with kinstr_prfcnt, but not vinstr",                           //
                kbase_version{1, 10, ioctl_iface_type::csf},                             //
                product_id_gtux,                                                         //
                backend_types(bt::kinstr_prfcnt_bad, bt::kinstr_prfcnt_wa)));

        auto actual = hwcnt::backend_type_discover(version, pid);

        INFO(test_name);
        CAPTURE(version);
        CAPTURE(pid);

        CHECK(actual == expected);
    }
    SECTION("select") {
        const char *test_name = nullptr;
        hwcnt::backend_types_set available_types{};
        const char *str_for_env{};
        std::pair<std::error_code, hwcnt::backend_type> expected{};

        std::tie(test_name, available_types, str_for_env, expected) = GENERATE_COPY(                           //
            std::make_tuple(                                                                                   //
                "vinstr only",                                                                                 //
                backend_types(hwcnt::backend_type::vinstr),                                                    //
                static_cast<const char *>(nullptr),                                                            //
                std::make_pair(std::error_code{}, hwcnt::backend_type::vinstr)                                 //
                ),                                                                                             //
            std::make_tuple(                                                                                   //
                "kinstr_prfcnt only",                                                                          //
                backend_types(hwcnt::backend_type::kinstr_prfcnt),                                             //
                static_cast<const char *>(nullptr),                                                            //
                std::make_pair(std::error_code{}, hwcnt::backend_type::kinstr_prfcnt)                          //
                ),                                                                                             //
            std::make_tuple(                                                                                   //
                "vinstr and kinstr_prfcnt",                                                                    //
                backend_types(hwcnt::backend_type::vinstr, hwcnt::backend_type::kinstr_prfcnt),                //
                static_cast<const char *>(nullptr),                                                            //
                std::make_pair(std::error_code{}, hwcnt::backend_type::vinstr)                                 //
                ),                                                                                             //
            std::make_tuple(                                                                                   //
                "vinstr and kinstr_prfcnt, kinstr_prfcnt override",                                            //
                backend_types(hwcnt::backend_type::vinstr, hwcnt::backend_type::kinstr_prfcnt),                //
                "kinstr_prfcnt",                                                                               //
                std::make_pair(std::error_code{}, hwcnt::backend_type::kinstr_prfcnt)                          //
                ),                                                                                             //
            std::make_tuple(                                                                                   //
                "vinstr only, kinstr_prfcnt override",                                                         //
                backend_types(hwcnt::backend_type::vinstr),                                                    //
                "kinstr_prfcnt",                                                                               //
                std::make_pair(std::make_error_code(std::errc::function_not_supported), hwcnt::backend_type{}) //
                )                                                                                              //
        );

        mock::getenv_iface iface{str_for_env};

        auto actual = hwcnt::backend_type_select(available_types, iface);

        INFO(test_name);
        CHECK(actual == expected);
    }
}
