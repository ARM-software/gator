// Copyright (C) 2023 by Arm Limited (or its affiliates). All rights reserved.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <span>
#include <vector>

static constexpr std::size_t total_iterations = 800000;

struct constants_single_t {
    static constexpr long branch_mispredicts_iterations = 8;
    static constexpr long divider_stalls_iterations = 2048;
    static constexpr long double_to_int_iterations = 3000;
    static constexpr long isb_iterations = 256;
    static constexpr long dcache_miss_iterations = 96;
    static constexpr long nop_counter = 1500ULL;
};

struct constants_outer_t {
    static constexpr long branch_mispredicts_iterations = 4;
    static constexpr long divider_stalls_iterations = 256;
    static constexpr long double_to_int_iterations = 256;
    static constexpr long isb_iterations = 256;
    static constexpr long dcache_miss_iterations = 64;
    static constexpr long nop_counter = 10000ULL;
};

struct constants_inner_t {
    static constexpr long branch_mispredicts_iterations = 2;
    static constexpr long divider_stalls_iterations = 64;
    static constexpr long double_to_int_iterations = 64;
    static constexpr long isb_iterations = 64;
    static constexpr long dcache_miss_iterations = 16;
    static constexpr long nop_counter = 10000ULL;
};

struct constants_final_t {
    static constexpr long branch_mispredicts_iterations = 1;
    static constexpr long divider_stalls_iterations = 32;
    static constexpr long double_to_int_iterations = 32;
    static constexpr long isb_iterations = 16;
    static constexpr long dcache_miss_iterations = 4;
    static constexpr long nop_counter = 10000ULL;
};

static constexpr long nextop_rate = 8;

template<typename Constants, typename NextOp>
class benchmarks_t {
public:
    explicit benchmarks_t(NextOp && next_op)
        : next_op(std::forward<NextOp>(next_op))
    {
    }

    ~benchmarks_t()
    {
        // the results are junk, we just want to make sure they get calculated and output somewhere
        std::cout << "Results are: lfsr=" << lfsr << ", int_divider=" << int_divider << ", d=" << d
                  << ", double_divider=" << double_divider << ", sum=" << sum << std::endl;
    }

    constexpr void operator()(std::size_t n)
    {
        switch (n % 7) {
            case 0: {
                lfsr += std::uint16_t(branch_mispredicts(lfsr, Constants::branch_mispredicts_iterations) + 1);
                break;
            }
            case 1: {
                int_divider =
                    (int_divider + int_divider_stalls(Constants::divider_stalls_iterations, int_divider + 1)) % 11;
                break;
            }
            case 2: {
                d += double_to_int(Constants::double_to_int_iterations, d, 0.1) + 1;
                break;
            }
            case 3: {
                double_divider +=
                    (double_divider + fp_divider_stalls(Constants::divider_stalls_iterations, double_divider + 1));
                break;
            }
            case 4: {
                isb(Constants::isb_iterations);
                break;
            }
            case 5: {
                sum += dcache_miss(dcache_miss_mem, Constants::dcache_miss_iterations);
                break;
            }
            case 6: {
                sum += nops(Constants::nop_counter);
                break;
            }
        }
    }

private:
    NextOp next_op;
    std::vector<void *> const dcache_miss_mem = dcache_miss_init();
    std::uint16_t lfsr = 0xACE1u;
    std::int64_t int_divider = 0;
    double double_divider = 0;
    double d = 2.345;
    long sum = 0;

    /* Create and initialise a block of memory with a non-linear pointer chain. */
    [[nodiscard, gnu::noinline, gnu::used]] static std::vector<void *> dcache_miss_init()
    {
        // L2D cache FOR Neoverse-N1 and Intel(R) Xeon(R) W-2145 CPU as per:
        // - /sys/bus/cpu/devices/cpu0/cache/index2/size
        // - /sys/bus/cpu/devices/cpu0/cache/index2/coherency_line_size
        // - /sys/bus/cpu/devices/cpu0/cache/index2/ways_of_associativity
        // - /sys/bus/cpu/devices/cpu0/cache/index2/number_of_sets
#if defined(__aarch64__)
        constexpr std::size_t DCACHE_SIZE = 1024*1024;
        constexpr std::size_t DCACHE_LINE_SIZE = 64;
        constexpr std::size_t DCACHE_ASSOCIATIVITY = 8;
        constexpr std::size_t DCACHE_SETS = 2048;
#elif defined(__x86_64__)
        constexpr std::size_t DCACHE_SIZE = 1024*1024;
        constexpr std::size_t DCACHE_LINE_SIZE = 64;
        constexpr std::size_t DCACHE_ASSOCIATIVITY = 16;
        constexpr std::size_t DCACHE_SETS = 1024;
#else
#  error Uknown architecture!
#endif

        constexpr std::size_t step_size = (DCACHE_LINE_SIZE * DCACHE_ASSOCIATIVITY) / sizeof(void *);
        constexpr std::size_t repetitions = 16;
        constexpr std::size_t mem_size = repetitions * DCACHE_SETS * (step_size * sizeof(void *));

        std::vector<void *> result {mem_size, nullptr};

        std::size_t idx = 0;

        for (std::size_t set = 1; set < DCACHE_SETS; set++) {
            std::size_t idxNext = (set & 1) ? (DCACHE_SETS - set) : set;
            result[step_size * idx] = &result[step_size * idxNext];
            idx = idxNext;
        }

        result[step_size * idx] = nullptr;

        return result;
    }

    [[nodiscard, gnu::noinline, gnu::used]] int branch_mispredicts(std::uint16_t lfsr, long iterations)
    {
        int result = 0;

        void const * const labels[] = {
            &&l0,  &&l1,  &&l2,  &&l3,  &&l4,  &&l5,  &&l6,  &&l7,  &&l8,  &&l9,  &&l10,
            &&l11, &&l12, &&l13, &&l14, &&l15, &&l16, &&l17, &&l18, &&l19, &&l20, &&l21,
            &&l22, &&l23, &&l24, &&l25, &&l26, &&l27, &&l28, &&l29, &&l30, &&l31,
        };

        for (unsigned int mask = 0x1F; mask > 0; mask >>= 1) {
            long n = iterations;
            while (n > 0) {
l31:
                result += 1;
l30:
                result += 1;
l29:
                result += 1;
l28:
                result += 1;
l27:
                result += 1;
l26:
                result += 1;
l25:
                result += 1;
l24:
                result += 1;
l23:
                result += 1;
l22:
                result += 1;
l21:
                result += 1;
l20:
                result += 1;
l19:
                result += 1;
l18:
                result += 1;
l17:
                result += 1;
l16:
                result += 1;
l15:
                result += 1;
l14:
                result += 1;
l13:
                result += 1;
l12:
                result += 1;
l11:
                result += 1;
l10:
                result += 1;
l9:
                result += 1;
l8:
                result += 1;
l7:
                result += 1;
l6:
                result += 1;
l5:
                result += 1;
l4:
                result += 1;
l3:
                result += 1;
l2:
                result += 1;
l1:
                result += 1;
                lfsr = (lfsr >> 1) | ((((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1) << 15);
                goto * labels[lfsr & mask];
l0:

                if ((n % nextop_rate) == 0) {
                    next_op();
                }

                n--;
            }
        }

        return result;
    }

    [[nodiscard, gnu::noinline, gnu::used]] std::int64_t int_divider_stalls(long iterations, std::int64_t divider)
    {
        std::int64_t result = INT64_MAX;

        for (long n = iterations; n > 0; n--) {
            result /= divider;
            result /= divider;
            result /= divider;
            result /= divider;
            if ((n % nextop_rate) == 0) {
                next_op();
            }
        }

        return result;
    }

    [[nodiscard, gnu::noinline, gnu::used]] double fp_divider_stalls(long iterations, double divider)
    {
        double result = INT64_MAX;

        for (long n = iterations; n > 0; n--) {
            result /= divider;
            result /= divider;
            result /= divider;
            result /= divider;
            if ((n % nextop_rate) == 0) {
                next_op();
            }
        }

        return result;
    }

    [[nodiscard, gnu::noinline, gnu::used]] int double_to_int(long iterations, double d, double inc)
    {
        int result = 0;

        for (long n = iterations; n > 0; n--) {
            result += (int) d;
            d += inc;
            if ((n % nextop_rate) == 0) {
                next_op();
            }
        }

        return result;
    }

    [[gnu::noinline, gnu::used]] void isb(long runs)
    {
        for (long n = runs; n > 0; n--) {
#if defined(__aarch64__)
            asm volatile("isb" ::: "memory");
            asm volatile("isb" ::: "memory");
            asm volatile("isb" ::: "memory");
            asm volatile("isb" ::: "memory");
#elif defined(__x86_64__)
            asm volatile("mfence" ::: "memory");
            asm volatile("mfence" ::: "memory");
            asm volatile("mfence" ::: "memory");
            asm volatile("mfence" ::: "memory");
#else
#  error Uknown architecture!
#endif
            if ((n % nextop_rate) == 0) {
                next_op();
            }
        }
    }

    [[nodiscard, gnu::noinline, gnu::used]] long dcache_miss(std::span<void * const> mem, long runs)
    {
        long sum = 0;

        /* Repeatedly follow the pointer chain to generate cache refills. */
        for (long n = runs; (n > 0) && (sum < runs); n--) {
            void * const volatile * next = &(mem.front());
            do {
                sum++;
                next = (void * const *) *next;
            } while (next != nullptr);

            if ((sum % nextop_rate) == 0) {
                next_op();
            }
        }

        return sum;
    }

    [[nodiscard, gnu::noinline, gnu::used]] long nops(long runs)
    {
        for (long n = runs; n > 0; n--) {
#if defined(__aarch64__)
            asm volatile("nop" ::: "memory");
            asm volatile("nop" ::: "memory");
            asm volatile("nop" ::: "memory");
            asm volatile("nop" ::: "memory");
            asm volatile("nop" ::: "memory");
            asm volatile("nop" ::: "memory");
            asm volatile("nop" ::: "memory");
            asm volatile("nop" ::: "memory");
#elif defined(__x86_64__)
            asm volatile("nop" ::: "memory");
            asm volatile("nop" ::: "memory");
            asm volatile("nop" ::: "memory");
            asm volatile("nop" ::: "memory");
            asm volatile("nop" ::: "memory");
            asm volatile("nop" ::: "memory");
            asm volatile("nop" ::: "memory");
            asm volatile("nop" ::: "memory");
#endif

            if ((n % nextop_rate) == 0) {
                next_op();
            }
        }

        return runs;
    }
};

template<std::size_t N>
[[nodiscard, gnu::noinline, gnu::used]] static std::array<std::size_t, N> init_sequence(std::mt19937 & randomizer)
{
    std::array<std::size_t, N> result {};

    // fill with sorted sequence 0...(N-1)
    for (std::size_t n = 0; n < N; ++n) {
        result[n] = n;
    }

    std::shuffle(result.begin(), result.end(), randomizer);
    std::shuffle(result.rbegin(), result.rend(), randomizer);

    return result;
}

struct no_op_t {
    constexpr void operator()() { }
};

class inner_loop_t {
public:
    std::size_t n = 0;

    constexpr void operator()() { inner(n); }

private:
    benchmarks_t<constants_final_t, no_op_t> inner {no_op_t {}};
};

class outer_loop_t {
public:
    std::size_t n = 0;

    outer_loop_t(inner_loop_t & inner)
        : inner(inner)
    {
    }

    constexpr void operator()() { inner(n); }

private:
    benchmarks_t<constants_inner_t, inner_loop_t &> inner;
};

int main(int argc, char ** argv)
{
    static constexpr std::size_t num_items = 7;

    auto const seed = argc > 1 ? std::strtoul(argv[1], nullptr, 0) //
                               : std::chrono::system_clock::now().time_since_epoch().count();

    auto const mode = (argc > 2 ? std::strtoul(argv[2], nullptr, 0) //
                                : 0);

    std::random_device random_dev {};
    std::mt19937 randomizer(random_dev());
    randomizer.seed(seed+1);

    auto sequence_outer = init_sequence<num_items>(randomizer);
    auto sequence_inner = init_sequence<num_items>(randomizer);
    auto sequence_final = init_sequence<num_items>(randomizer);

    std::cout << "Seed = " << seed << std::endl;
    std::cout << "Sequence (Outer) = {";
    for (auto n : sequence_outer) {
        std::cout << n << ", ";
    }
    std::cout << "}" << std::endl;
    std::cout << "Sequence (Inner) = {";
    for (auto n : sequence_inner) {
        std::cout << n << ", ";
    }
    std::cout << "}" << std::endl;
    std::cout << "Sequence (Final) = {";
    for (auto n : sequence_final) {
        std::cout << n << ", ";
    }
    std::cout << "}" << std::endl;

    switch (mode) {
        case 1: {
            benchmarks_t<constants_single_t, no_op_t> outer_benchmark {no_op_t {}};

            for (std::size_t n = 0; n < total_iterations; ++n) {
                outer_benchmark(sequence_outer[n % sequence_outer.size()]);

                if (seed == 0) {
                    std::shuffle(sequence_outer.begin(), sequence_outer.end(), randomizer);
                }
            }
            break;
        }

        case 2: {
            inner_loop_t inner_loop_wrapper {};
            benchmarks_t<constants_inner_t, inner_loop_t &> outer_benchmark {inner_loop_wrapper};

            for (std::size_t n = 0; n < total_iterations; ++n) {
                inner_loop_wrapper.n = sequence_final[n % sequence_final.size()];
                outer_benchmark(sequence_outer[n % sequence_outer.size()]);

                if (seed == 0) {
                    std::shuffle(sequence_outer.begin(), sequence_outer.end(), randomizer);
                    std::shuffle(sequence_final.begin(), sequence_final.end(), randomizer);
                }
            }
            break;
        }

        default: {
            inner_loop_t inner_loop_wrapper {};
            outer_loop_t outer_loop_wrapper {inner_loop_wrapper};
            benchmarks_t<constants_outer_t, outer_loop_t &> outer_benchmark {outer_loop_wrapper};

            for (std::size_t n = 0; n < total_iterations; ++n) {
                inner_loop_wrapper.n = sequence_final[n % sequence_final.size()];
                outer_loop_wrapper.n = sequence_inner[n % sequence_inner.size()];
                outer_benchmark(sequence_outer[n % sequence_outer.size()]);

                if (seed == 0) {
                    std::shuffle(sequence_outer.begin(), sequence_outer.end(), randomizer);
                    std::shuffle(sequence_inner.begin(), sequence_inner.end(), randomizer);
                    std::shuffle(sequence_final.begin(), sequence_final.end(), randomizer);
                }
            }
            break;
        }
    }

    return 0;
}
