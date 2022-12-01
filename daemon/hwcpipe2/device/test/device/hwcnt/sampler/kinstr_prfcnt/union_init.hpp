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

/** @file union_init.hpp */

#pragma once

#include <device/ioctl/kinstr_prfcnt/types.hpp>

#include <array>

namespace test {

namespace {
/* Use anonymous namespace here to prevent kinstr_prfcnt export */
namespace kinstr_prfcnt = hwcpipe::device::ioctl::kinstr_prfcnt;
} // namespace

namespace enum_item {
/**
 * Initialize block enum item.
 *
 * @param type[in]             Block type.
 * @param set[in]              Performance counters set.
 * @paran num_instances[in]    Number of block instances.
 * @param num_values[in]       Number of counters values.
 * @param counter_mask[in]     Supported counters mask.
 * @return Block enum item initialized.
 */
inline auto block(kinstr_prfcnt::block_type type, kinstr_prfcnt::prfcnt_set set, uint16_t num_instances,
                  uint16_t num_values, std::array<uint64_t, 2> counter_mask = {}) {
    kinstr_prfcnt::enum_item result{};

    result.hdr.type = kinstr_prfcnt::enum_item::item_type::block;
    result.hdr.item_version = 0;
    result.u.block_counter.type = type;
    result.u.block_counter.set = set;
    result.u.block_counter.num_instances = num_instances;
    result.u.block_counter.num_values = num_values;
    result.u.block_counter.counter_mask[0] = counter_mask[0];
    result.u.block_counter.counter_mask[1] = counter_mask[1];

    return result;
}

/**
 * Initialize request enum item.
 *
 * @param request_type[in]    Request type.
 * @param version[in]         Bitmask of versions that support this request.
 * @return Request enum item initialized.
 */
inline auto request(kinstr_prfcnt::enum_item::enum_request::request_type request_type, uint32_t versions_mask) {
    kinstr_prfcnt::enum_item result{};

    result.hdr.type = kinstr_prfcnt::enum_item::item_type::request;
    result.hdr.item_version = 0;
    result.u.request.request_item_type = request_type;
    result.u.request.versions_mask = versions_mask;

    return result;
}

/**
 * Initialize sample enum item.
 *
 * @param num_clock_domains[in]    Number of clock domains of the GPU.
 * @return Sample enum item initialized.
 */
inline auto sample_info(uint32_t num_clock_domains) {
    kinstr_prfcnt::enum_item result{};

    result.hdr.type = kinstr_prfcnt::enum_item::item_type::sample_info;
    result.hdr.item_version = 0;
    result.u.sample_info.num_clock_domains = num_clock_domains;

    return result;
}
} // namespace enum_item

namespace request_item {
/**
 * Initialize mode request item.
 *
 * @param period_ns[in]    Sampling period (nanoseconds).
 * @return Mode request item initialized.
 */
inline auto mode(uint64_t period_ns) {
    kinstr_prfcnt::request_item result{};

    result.hdr.type = kinstr_prfcnt::request_item::item_type::mode;
    result.hdr.item_version = kinstr_prfcnt::api_version;

    if (period_ns != 0) {
        result.u.req_mode.mode = kinstr_prfcnt::request_item::request_mode::sampling_mode::periodic;
        result.u.req_mode.mode_config.periodic.period_ns = period_ns;
    } else {
        result.u.req_mode.mode = kinstr_prfcnt::request_item::request_mode::sampling_mode::manual;
    }

    return result;
}

/**
 * Initialize enable request item.
 *
 * @param type[in]    Block type.
 * @param set[in]     Performance counters set.
 * @param malk_lo[in] Low 64 bits of the counters enable mask.
 * @param malk_hi[in] High 64 bits of the counters enable mask.
 * @return Enable request item initialized.
 */
inline auto enable(kinstr_prfcnt::block_type type, kinstr_prfcnt::prfcnt_set set, uint64_t mask_lo, uint64_t mask_hi) {
    using kinstr_prfcnt::request_item;

    kinstr_prfcnt::request_item result{};
    result.hdr.type = kinstr_prfcnt::request_item::item_type::enable;
    result.hdr.item_version = kinstr_prfcnt::api_version;

    result.u.req_enable.type = type;
    result.u.req_enable.set = set;
    result.u.req_enable.enable_mask[0] = mask_lo;
    result.u.req_enable.enable_mask[1] = mask_hi;

    return result;
}
} // namespace request_item

namespace metadata_item {
/**
 * Initialize block metadata item.
 *
 * @param type[in]          Block type.
 * @param block_idx[in]     Block index.
 * @param set[in]           Performance counters set.
 * @param values_offset[in] Counters values offset in the counters buffer.
 * @return Block metadata item initialized.
 */
inline auto block(kinstr_prfcnt::block_type type, uint8_t block_idx, kinstr_prfcnt::prfcnt_set set,
                  uint32_t values_offset) {
    kinstr_prfcnt::metadata_item result{};

    result.hdr.type = kinstr_prfcnt::metadata_item::item_type::block;
    result.hdr.item_version = kinstr_prfcnt::api_version;

    result.u.block_md.type = type;
    result.u.block_md.block_idx = block_idx;
    result.u.block_md.set = set;
    result.u.block_md.values_offset = values_offset;

    return result;
}

/**
 * Initialize clock metadata item.
 *
 * @param num_domains[in]    Number of clock domains.
 * @param top_cycle[in]      Top clock domain cycle count.
 * @param sc_cycle[in]       SC clock domain cycle count.
 * @return Clock metadata item initialized.
 */
inline auto clock(uint32_t num_domains, uint64_t top_cycle, uint64_t sc_cycle) {
    kinstr_prfcnt::metadata_item result{};

    result.hdr.type = kinstr_prfcnt::metadata_item::item_type::clock;
    result.hdr.item_version = kinstr_prfcnt::api_version;

    result.u.clock_md.num_domains = num_domains;
    result.u.clock_md.cycles[0] = top_cycle;
    result.u.clock_md.cycles[1] = sc_cycle;

    return result;
}

/**
 * Initialize sample metadata item.
 *
 * @param start        Sample start.
 * @param stop         Sample stop.
 * @param seq          Sample number.
 * @param user_data    User data.
 * @param flags        Sample flags.
 * @return Sample metadata initialized.
 */
inline auto sample(uint64_t start, uint64_t stop, uint64_t seq, uint64_t user_data,
                   kinstr_prfcnt::metadata_item::sample_metadata::sample_flag flags) {
    kinstr_prfcnt::metadata_item result{};

    result.hdr.type = kinstr_prfcnt::metadata_item::item_type::sample;
    result.hdr.item_version = kinstr_prfcnt::api_version;

    result.u.sample_md.timestamp_start = start;
    result.u.sample_md.timestamp_stop = stop;
    result.u.sample_md.seq = seq;
    result.u.sample_md.user_data = user_data;
    result.u.sample_md.flags = flags;

    return result;
}

} // namespace metadata_item
} // namespace test
