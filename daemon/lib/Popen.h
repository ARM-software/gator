/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LIB_POPEN_H
#define INCLUDE_LIB_POPEN_H

namespace lib
{
    struct PopenResult
    {
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
    };

    /**
     * Opens a command with execvp.
     *
     * Normal popen uses a hardcoded shell path that does not allow a binary to be used on both android and linux.
     *
     * @param command null terminated list of program + args
     * @return pid and file descriptors, if an error pid = -errno
     */
    PopenResult popen(const char * const command[]);

    /**
     * Waits for a command to exit
     *
     * @param command
     * @return the exit status from waitpid (positive)
     * or negative errno (result.pid) if result is invalid
     * or INT_MIN if child has already been waited
     */
    int pclose(const PopenResult & result);
}

#endif // INCLUDE_LIB_POPEN_H

