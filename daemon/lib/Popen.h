/* Copyright (C) 2018-2021 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_POPEN_H
#define INCLUDE_LIB_POPEN_H

#include "lib/Span.h"

#include <array>

namespace lib {
    struct PopenResult {
        /**
         * Process pid or negative errno if invalid
         */
        int pid;
        /**
         * stdin file descriptor
         */
        int in;
        /**
         * stdout file descriptor
         */
        int out;
        /**
         * stderr file descriptor
         */
        int err;

        /** Equality operator.
         *
         * @param other Instance to compare against
         * @return True if equal
         */
        constexpr bool operator==(const PopenResult & other) const
        {
            return pid == other.pid && in == other.in && out == other.out && err == other.err;
        }

        /** Inequality operator.
         *
         * @param other Instance to compare against
         * @return True if not equal
         */
        constexpr bool operator!=(const PopenResult & other) const { return !(*this == other); }
    };

    /**
     * Opens a command with execvp.
     *
     * Normal popen uses a hardcoded shell path that does not allow a binary to be used on both android and linux.
     *
     * @param command_and_args null terminated list of program + args
     * @return pid and file descriptors, if an error pid = -errno
     */
    PopenResult popen(lib::Span<const char * const> command_and_args);

    /** Helper trait for checking the argument to the variadic popen call */
    template<typename... T>
    using IsConstCharPointer = std::enable_if_t<std::conjunction_v<std::is_same<T, const char *>...>, int>;

    /**
     * Opens a command with execvp.
     *
     * @tparam T Must be `const char *`
     * @param str command list of program + args,( no need to include null termination)
     * @return pid and file descriptors, if an error pid = -errno
     */
    template<typename... T, IsConstCharPointer<T...> = 0>
    PopenResult popen(T... str)
    {
        std::array<const char *, sizeof...(str) + 1> const result {{str..., nullptr}};
        return popen(result);
    }

    /**
     * Waits for a command to exit
     *
     * @return the exit status from waitpid (positive)
     * or negative errno (result.pid) if result is invalid
     * or INT_MIN if child has already been waited
     */
    int pclose(const PopenResult & result);
}

#endif // INCLUDE_LIB_POPEN_H
