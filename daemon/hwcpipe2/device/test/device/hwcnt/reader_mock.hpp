/*
 * Copyright (c) 2021-2022 ARM Limited.
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

#pragma once

#include <catch2/catch.hpp>

#include <device/hwcnt/sample.hpp>

namespace mock {

namespace detail {

struct block_handle_info {
    uint64_t data;
};

} // namespace detail

namespace hwc = hwcpipe::device::hwcnt;

/** Mock reader interface implementation */
class reader : public hwc::reader {

    struct sample_handle {
        const reader *self;
        uint64_t sample_nr;
    };

  public:
    /** Reader function call stats. */
    struct stats {
        uint64_t get_sample;
        uint64_t put_sample;
        uint64_t next;
    };

    reader(size_t max_iterations = 10)
        : hwc::reader(-1, {}, {})
        , max_iterations_(max_iterations) {}

    std::error_code get_sample(hwc::sample_metadata &sm, hwc::sample_handle &sample_hndl) override {
        ++stats_.get_sample;

        auto &hndl = sample_hndl.get<sample_handle>();
        hndl.self = this;
        hndl.sample_nr = stats_.get_sample;

        sm = sample_metadata_;

        return generate_error_code();
    }

    bool next(hwc::sample_handle sample_hndl, hwc::block_metadata &bm, hwc::block_handle &block_hndl) const override {
        ++stats_.next;

        const auto &hndl = sample_hndl.get<sample_handle>();
        CHECK(hndl.self == this);

        if (stats_.next > max_iterations_)
            return false;

        bm = {};

        bm.index = stats_.next - 1;
        bm.type = hwc::block_type::core;

        block_hndl = generate_block_hndl();

        return true;
    }

    std::error_code put_sample(hwc::sample_handle sample_hndl) override {
        ++stats_.put_sample;

        const auto &hndl = sample_hndl.get<sample_handle>();
        CHECK(hndl.self == this);
        CHECK(hndl.sample_nr == stats_.put_sample);

        return generate_error_code();
    }

    std::error_code discard() override { return generate_error_code(); }

    /** Return error once when next reader method is called. */
    void inject_error() { inject_error_ = true; }

    /** @return reader function call stats. */
    const stats &num() const { return stats_; }

    /**
     * Set sample meta-data to use in put_sample.
     *
     * @param[in] sample_metadata    Sample meta-data to set.
     */
    void set_sample_metadata(const hwc::sample_metadata &sample_metadata) { sample_metadata_ = sample_metadata; }

  private:
    std::error_code generate_error_code() const {
        std::error_code result;

        if (inject_error_) {
            result = std::make_error_code(std::errc::bad_address);
            inject_error_ = false;
        }

        return result;
    }

    /** Generate fake block handle. */
    hwc::block_handle generate_block_hndl() const {
        hwc::block_handle result;

        auto &bhi = result.get<detail::block_handle_info>();
        bhi.data = stats_.next;

        return result;
    }

    size_t max_iterations_;
    hwc::sample_metadata sample_metadata_{};
    mutable stats stats_{};
    mutable bool inject_error_{false};
};
} // namespace mock
