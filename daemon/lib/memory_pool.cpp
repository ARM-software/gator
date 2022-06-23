/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#include "lib/memory_pool.h"

#include <algorithm>
#include <cstddef>
#include <vector>

#include <alloca.h>

using lib::Span;

namespace {
    using use_list_type = std::vector<lib::alloc::memory_pool_t::pointer_type::element_type>;

    constexpr std::size_t use_list_reserve_count = 100;

    void deallocate(use_list_type & use_list, Span<char> chunk)
    {
        auto it = std::find_if(use_list.begin(), use_list.end(), [&](auto c) { return c.data() == chunk.data(); });
        if (it != use_list.end()) {
            use_list.erase(it);
        }
    }
}

namespace lib::alloc {

    memory_pool_t::memory_pool_t(std::size_t size)
    {
        mem.resize(size);
        // Minimise allocations
        use_list.reserve(use_list_reserve_count);
    }

    memory_pool_t::pointer_type memory_pool_t::alloc(std::size_t size)
    {
        const auto add_chunk = [&](pointer_type::element_type chunk) {
            use_list.push_back(chunk);
            std::sort(use_list.begin(), use_list.end(), [](auto a, auto b) { return a.data() < b.data(); });

            return pointer_type {chunk, [this, chunk]() { deallocate(use_list, chunk); }};
        };

        const auto gap_checker = [&](pointer_type::element_type prev_span, pointer_type::element_type this_span) {
            const auto gap = static_cast<std::uintptr_t>(this_span.data() - prev_span.end());
            return gap >= size;
        };

        // If the list is empty, just allocate from the start
        if (use_list.empty()) {
            if (size > mem.size()) {
                return {};
            }
            return add_chunk({mem.data(), size});
        }

        // Check the gap preceding the first chunk (if any), this is done outside of
        // the loop so there's fewer conditionals in it
        if (gap_checker({mem.data(), 0}, use_list.front())) {
            return add_chunk({mem.data(), size});
        }

        // Check the gaps between any used elements are big enough
        for (auto i = 1U; i < use_list.size(); ++i) {
            auto prev_span = use_list[i - 1];
            auto this_span = use_list[i];

            if (gap_checker(prev_span, this_span)) {
                return add_chunk({prev_span.end(), size});
            }
        }

        // ... And check the gap after the last chunk (if any)
        if (gap_checker(use_list.back(), {mem.end().base(), 0})) {
            return add_chunk({use_list.back().end(), size});
        }

        // There's no gaps big enough!
        return {};
    };

}
