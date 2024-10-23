/* Copyright (C) 2024 by Arm Limited. All rights reserved. */

#pragma once

#include <cstdint>
#include <functional>
#include <utility>

namespace cpu_utils {
    /**
     * The CPUID value
     */
    class cpuid_t {
    public:
        static constexpr std::uint32_t cpuid_raw_invalid = 0;
        static constexpr std::uint32_t cpuid_raw_other = 0xFFFFFU;

        /** An invalid value */
        static const cpuid_t invalid;
        /** Indicates the "Other" pseudo-cpuid */
        static const cpuid_t other;

        /** Construct from a raw MIDR value */
        [[nodiscard]] static constexpr cpuid_t from_raw_midr(std::uint32_t raw_value)
        {
            constexpr std::uint32_t implementer_shift = 12U;
            constexpr std::uint32_t implementer_mask = 0xFF000U;
            constexpr std::uint32_t part_shift = 4U;
            constexpr std::uint32_t part_mask = 0x00FFFU;

            return cpuid_t(((raw_value >> implementer_shift) & implementer_mask)
                           | ((raw_value >> part_shift) & part_mask));
        }

        /** Construct from a raw CPUID value */
        [[nodiscard]] static constexpr cpuid_t from_raw(std::uint32_t raw_value) { return cpuid_t(raw_value); }

        /* Comparison operators */
        constexpr friend bool operator==(cpuid_t const & a, cpuid_t const & b) { return a.raw_value == b.raw_value; }
        constexpr friend bool operator!=(cpuid_t const & a, cpuid_t const & b) { return a.raw_value != b.raw_value; }
        constexpr friend bool operator<(cpuid_t const & a, cpuid_t const & b) { return a.raw_value < b.raw_value; }

        /* Swap */
        friend void swap(cpuid_t & a, cpuid_t & b) noexcept
        {
            using std::swap;
            swap(a.raw_value, b.raw_value);
        }

        /** Constructs an empty (invalid) CPUID value */
        constexpr cpuid_t() = default;

        /** Is valid? */
        [[nodiscard]] constexpr bool valid() const { return (raw_value != 0); }

        /** Is invalid or "Other" */
        [[nodiscard]] constexpr bool invalid_or_other() const
        {
            return (raw_value == 0) || (raw_value == cpuid_raw_other);
        }

        /** THe raw CPUID value */
        [[nodiscard]] constexpr std::uint32_t to_raw_value() const { return raw_value; }

    private:
        static constexpr std::uint32_t mask = 0xfffffU;

        std::uint32_t raw_value = 0;

        explicit constexpr cpuid_t(std::uint32_t raw_value) : raw_value(raw_value & mask) {}
    };

    /**
     * The MIDR register value
     */
    class midr_t {
    public:
        /** An invalid value */
        static const midr_t invalid;
        /** Indicates the "Other" pseudo-cpuid */
        static const midr_t other;

        /** Construct from a raw MIDR value */
        [[nodiscard]] static constexpr midr_t from_raw(std::uint32_t raw_value) { return midr_t(raw_value); }

        /* Comparison operators */
        constexpr friend bool operator==(midr_t const & a, midr_t const & b) { return a.raw_value == b.raw_value; }
        constexpr friend bool operator!=(midr_t const & a, midr_t const & b) { return a.raw_value != b.raw_value; }
        constexpr friend bool operator<(midr_t const & a, midr_t const & b) { return a.raw_value < b.raw_value; }

        /* Swap */
        friend void swap(midr_t & a, midr_t & b) noexcept
        {
            using std::swap;
            swap(a.raw_value, b.raw_value);
        }

        /** Constructs an empty (invalid) MIDR value */
        constexpr midr_t() = default;

        /* Modify the raw MIDR fields */
        constexpr void set_architecture(std::uint32_t architecture)
        {
            raw_value |= (architecture & architecture_mask) << architecture_shift;
        }
        constexpr void set_implementer(std::uint32_t implementer)
        {
            raw_value |= (implementer & implementer_mask) << implementer_shift;
        }
        constexpr void set_partnum(std::uint32_t partnum) { raw_value |= (partnum & part_mask) << part_shift; }
        constexpr void set_revision(std::uint32_t revision)
        {
            raw_value |= (revision & revision_mask) << revision_shift;
        }
        constexpr void set_variant(std::uint32_t variant) { raw_value |= (variant & variant_mask) << variant_shift; }

        /** Convert to the CPUID type */
        [[nodiscard]] constexpr cpuid_t to_cpuid() const { return cpuid_t::from_raw_midr(raw_value); }

        /** Is valid? */
        [[nodiscard]] constexpr bool valid() const { return (raw_value != 0); }

        /** Is invalid or "Other" */
        [[nodiscard]] constexpr bool invalid_or_other() const
        {
            return (raw_value == 0) || ((raw_value & other_mask) == other_mask);
        }

        /** The raw MIDR value */
        [[nodiscard]] constexpr std::uint32_t to_raw_value() const { return raw_value; }

        /** Get the revision value */
        [[nodiscard]] constexpr std::uint8_t get_revision() const
        {
            return (raw_value >> revision_shift) & revision_mask;
        }

        /** Get the variant value */
        [[nodiscard]] constexpr std::uint8_t get_variant() const { return (raw_value >> variant_shift) & variant_mask; }

    private:
        static constexpr std::uint32_t architecture_shift = 16U;
        static constexpr std::uint32_t architecture_mask = 0x0000000FU;
        static constexpr std::uint32_t implementer_shift = 24U;
        static constexpr std::uint32_t implementer_mask = 0x000000FFU;
        static constexpr std::uint32_t part_shift = 4U;
        static constexpr std::uint32_t part_mask = 0x00000FFFU;
        static constexpr std::uint32_t revision_shift = 0U;
        static constexpr std::uint32_t revision_mask = 0x0000000FU;
        static constexpr std::uint32_t variant_shift = 20U;
        static constexpr std::uint32_t variant_mask = 0x0000000FU;

        static constexpr std::uint32_t other_mask = (implementer_mask << implementer_shift) | (part_mask << part_shift);

        std::uint32_t raw_value = 0;

        explicit constexpr midr_t(std::uint32_t raw_value) : raw_value(raw_value) {}
    };

    // we expect to be able to share the backing memory for these over some memory mapped buffer
    // see PrimarySourceProvider.cpp `Ids`
    static_assert(alignof(midr_t) == alignof(int));
    static_assert(sizeof(midr_t) == sizeof(int));

    constexpr cpuid_t cpuid_t::invalid {};
    constexpr cpuid_t cpuid_t::other {~0U};
    constexpr midr_t midr_t::invalid {};
    constexpr midr_t midr_t::other {~0U};

    [[nodiscard]] constexpr bool operator==(midr_t const & m, cpuid_t const & c)
    {
        return m.to_cpuid() == c;
    }

    [[nodiscard]] constexpr bool operator!=(midr_t const & m, cpuid_t const & c)
    {
        return m.to_cpuid() != c;
    }

    [[nodiscard]] constexpr bool operator==(cpuid_t const & c, midr_t const & m)
    {
        return m.to_cpuid() == c;
    }

    [[nodiscard]] constexpr bool operator!=(cpuid_t const & c, midr_t const & m)
    {
        return m.to_cpuid() != c;
    }
}

namespace std {
    template<>
    struct hash<cpu_utils::cpuid_t> {
        [[nodiscard]] constexpr size_t operator()(cpu_utils::cpuid_t const & v) const noexcept
        {
            return v.to_raw_value();
        }
    };

    template<>
    struct hash<cpu_utils::midr_t> {
        [[nodiscard]] constexpr size_t operator()(cpu_utils::midr_t const & v) const noexcept
        {
            return v.to_raw_value();
        }
    };
}
