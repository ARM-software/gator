/* Copyright (C) 2021-2025 by Arm Limited (or its affiliates). All rights reserved. */

#include "lib/Process.h"

#include "Logging.h"

#include <cerrno>
#include <cstdlib>
#include <optional>
#include <string>
#include <system_error>

#include <boost/version.hpp>

#if BOOST_VERSION >= (108600)
#include <boost/process/v1/child.hpp>
#include <boost/process/v1/io.hpp>
namespace boost_process = boost::process::v1;
#else
#include <boost/process/child.hpp>
#include <boost/process/io.hpp>
namespace boost_process = boost::process;
#endif

#include <sys/prctl.h>

namespace gator::process {
    int system(const std::string & process_with_args)
    {
        // TODO: replace with Boost::Process once the dependency mechanism is agreed
        return std::system(process_with_args.c_str());
    }

    void set_parent_death_signal(int signal)
    {
        int result = ::prctl(PR_SET_PDEATHSIG, signal);
        if (result != 0) {
            LOG_ERROR("Call to prctl(PR_SET_PDEATHSIG(%d) failed with errno %d. This is non-fatal but "
                      "may result in orphaned processes",
                      signal,
                      errno);
        }
    }

    int runCommandAndRedirectOutput(const std::string & cmdToExecWithArgs,
                                    const std::optional<std::string> & targetFile)
    {
        auto child = targetFile.has_value()
                       ? boost_process::child(cmdToExecWithArgs, boost_process::std_out > targetFile.value())
                       : boost_process::child(cmdToExecWithArgs);

        std::error_code ec;
        child.wait(ec);
        if (ec.value() != 0) {
            return -1;
        }
        return child.exit_code();
    }
}
