/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#pragma once

#include "lib/Span.h"

#include <functional>
#include <memory>
#include <vector>

namespace lib::alloc {
    /** Fixed size memory pool, used to minimise heap allocations.
    *
    * Ring buffers can't be contiguous (because they wrap) which prevents more
    * efficient copying when using the STL - their iterators don't conform to the
    * std::contiguous_iterator concept.
    *
    * So for the intermediary buffer I've gone for a memory pool pattern instead.
    * Fragmentation may be an issue though, hard to know without testing it.
    *
    * This class can be moved but not copied.  This class is not thread-safe.
    */
    class memory_pool_t {
    public:
        /** Unique pointer that releases the allocation on destruction. */
        class pointer_type {
            friend class memory_pool_t;

        public:
            using element_type = lib::Span<char>;

            pointer_type() = default;

            pointer_type(std::nullptr_t) : pointer_type {} {}

            pointer_type(pointer_type &&) = default;
            pointer_type & operator=(pointer_type &&) = default;

            pointer_type(const pointer_type &) = delete;
            pointer_type & operator=(const pointer_type &) = delete;

            ~pointer_type()
            {
                if (dealloc_) {
                    dealloc_();
                }
            }

            element_type * operator->() noexcept { return &span_; }

            element_type operator*() const noexcept { return span_; }

            operator bool() const noexcept { return span_.size(); }

            bool operator==(const pointer_type & other) const noexcept { return span_.data() == other.span_.data(); }

            bool operator!=(const pointer_type & other) const noexcept { return !(*this == other); }

            void reset()
            {
                span_ = element_type {};
                if (dealloc_) {
                    dealloc_();
                }
            }

        private:
            pointer_type(element_type span, std::function<void()> dealloc) : span_ {span}, dealloc_ {std::move(dealloc)}
            {
            }

            element_type span_;
            std::function<void()> dealloc_;
        };

        /** Constructor.
        *
        * The heap memory is allocated at once upon construction.
        * @param size Capacity of memory pool
        */
        explicit memory_pool_t(std::size_t size);

        memory_pool_t(memory_pool_t &&) = default;
        memory_pool_t & operator=(memory_pool_t &&) = default;
        memory_pool_t(const memory_pool_t &) = delete;
        memory_pool_t & operator=(const memory_pool_t &) = delete;

        /** Allocate @a size contiguous bytes from the pool.
        *
        * @param size Number of contiguous bytes to allocate
        * @return Managed span defining the memory, will be a nullptr if not
        * enough contiguous free space is available
        */
        [[nodiscard]] pointer_type alloc(std::size_t size);

        [[nodiscard]] std::size_t size() const { return mem.size(); }

    private:
        using use_list_type = std::vector<lib::alloc::memory_pool_t::pointer_type::element_type>;

        std::vector<char> mem;
        use_list_type use_list;
    };

}
