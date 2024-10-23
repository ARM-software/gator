/* Copyright (C) 2022-2024 by Arm Limited. All rights reserved. */

#pragma once

#include "k/perf_event.h"
#include "lib/Assert.h"
#include "lib/Span.h"
#include "lib/Syscall.h"

#include <cstddef>
#include <cstdint>
#include <utility>

#include <sys/mman.h>
#include <unistd.h>

namespace agents::perf {

    class mmap_ptr_t {
    public:
        using value_type = char;
        using size_type = std::size_t;

        mmap_ptr_t() = default;

        mmap_ptr_t(void * mmap, std::size_t length)
            // NOLINTNEXTLINE(performance-no-int-to-ptr)
            : mmap(mmap != MAP_FAILED ? mmap : nullptr), length(length)
        {
        }

        mmap_ptr_t(mmap_ptr_t const &) = delete;
        mmap_ptr_t & operator=(mmap_ptr_t const &) = delete;
        mmap_ptr_t(mmap_ptr_t && that) noexcept
            : mmap(std::exchange(that.mmap, nullptr)), length(std::exchange(that.length, 0))
        {
        }
        mmap_ptr_t & operator=(mmap_ptr_t && that) noexcept
        {
            if (this != &that) {
                mmap_ptr_t tmp {std::move(that)};
                std::swap(mmap, tmp.mmap);
                std::swap(length, tmp.length);
            }
            return *this;
        }
        ~mmap_ptr_t() noexcept
        {
            auto * mmap = std::exchange(this->mmap, nullptr);
            auto length = std::exchange(this->length, 0);

            if (mmap != nullptr) {
                lib::munmap(mmap, length);
            }
        }

        template<typename T>
        [[nodiscard]] T * get_as()
        {
            return reinterpret_cast<T *>(mmap);
        }

        template<typename T>
        [[nodiscard]] T const * get_as() const
        {
            return reinterpret_cast<T const *>(mmap);
        }

        [[nodiscard]] lib::Span<uint8_t const> as_span() const { return {data(), size()}; }

        [[nodiscard]] uint8_t * data() { return reinterpret_cast<uint8_t *>(mmap); }
        [[nodiscard]] uint8_t const * data() const { return reinterpret_cast<uint8_t const *>(mmap); }
        [[nodiscard]] size_type size() const { return length; }

        [[nodiscard]] bool operator==(std::nullptr_t) const { return (mmap == nullptr) || (length == 0); }
        [[nodiscard]] bool operator!=(std::nullptr_t) const { return (mmap != nullptr) && (length != 0); }
        [[nodiscard]] explicit operator bool() const { return (mmap != nullptr) && (length != 0); }

    private:
        void * mmap = nullptr;
        std::size_t length = 0;
    };

    class perf_ringbuffer_mmap_t {
    public:
        perf_ringbuffer_mmap_t() = default;

        perf_ringbuffer_mmap_t(std::size_t page_size, mmap_ptr_t data_mapping, mmap_ptr_t aux_mapping = {})
            : page_size(page_size), data_mapping(std::move(data_mapping)), aux_mapping(std::move(aux_mapping))
        {
        }

        [[nodiscard]] bool has_data() const { return !!data_mapping; }

        [[nodiscard]] bool has_aux() const { return !!aux_mapping && has_data(); }

        [[nodiscard]] perf_event_mmap_page * header() { return data_mapping.get_as<perf_event_mmap_page>(); }
        [[nodiscard]] perf_event_mmap_page const * header() const
        {
            return data_mapping.get_as<perf_event_mmap_page>();
        }

        [[nodiscard]] lib::Span<uint8_t const> aux_span() const { return aux_mapping.as_span(); }

        [[nodiscard]] lib::Span<uint8_t const> data_span() const
        {
            if (!data_mapping) {
                return {};
            }

            return {reinterpret_cast<uint8_t const *>(data_mapping.data() + page_size),
                    data_mapping.size() - page_size};
        }

        void set_aux_mapping(mmap_ptr_t mapping)
        {
            runtime_assert(has_data(), "Data region must be mapped before aux");
            aux_mapping = std::move(mapping);
        }

    private:
        std::size_t page_size = 0;
        mmap_ptr_t data_mapping;
        mmap_ptr_t aux_mapping;
    };
}
