/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#include "lib/Process.h"

#include "Logging.h"

#include <cerrno>
#include <cstdlib>

#include <boost/process.hpp>

#include <sys/prctl.h>

namespace bp = boost::process;

namespace gator::process {
    int system(const std::string & cmd)
    {
        // TODO: replace with Boost::Process once the dependency mechanism is agreed
        return std::system(cmd.c_str());
    }

    void set_parent_death_signal(int signal)
    {
        int result = ::prctl(PR_SET_PDEATHSIG, signal);
        if (result != 0) {
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            LOG_ERROR("Call to prctl(PR_SET_PDEATHSIG(%d) failed with errno %d. This is non-fatal but "
                      "may result in orphaned processes",
                      signal,
                      errno);
        }
    }

    int runCommandAndRedirectOutput(const std::string & cmdToExecWithArgs,
                                    const std::optional<std::string> & targetFile)
    {
        auto child = targetFile.has_value() ? bp::child(cmdToExecWithArgs, bp::std_out > targetFile.value())
                                            : bp::child(cmdToExecWithArgs);

        std::error_code ec;
        child.wait(ec);
        if (ec.value() != 0) {
            return -1;
        }
        return child.exit_code();
    }
}
