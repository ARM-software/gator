/* Copyright (C) 2022 by Arm Limited. All rights reserved. */

#include <sys/wait.h>
#include <unistd.h>

namespace async::proc {
    constexpr bool w_if_continued(unsigned status)
    {
        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        return WIFCONTINUED(status);
    }

    constexpr bool w_if_exited(unsigned status)
    {
        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        return WIFEXITED(status);
    }

    constexpr bool w_if_signaled(unsigned status)
    {
        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        return WIFSIGNALED(status);
    }

    constexpr bool w_if_stopped(unsigned status)
    {
        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        return WIFSTOPPED(status);
    }

    constexpr unsigned w_exit_status(unsigned status)
    {
        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        return WEXITSTATUS(status);
    }

    constexpr unsigned w_stop_sig(unsigned status)
    {
        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        return WSTOPSIG(status);
    }

    constexpr unsigned w_term_sig(unsigned status)
    {
        //NOLINTNEXTLINE(hicpp-signed-bitwise)
        return WTERMSIG(status);
    }

}
