/* Copyright (C) 2021 by Arm Limited. All rights reserved. */

#pragma once

#include <optional>
#include <string>

namespace gator::process {

    int system(const std::string & process_with_args);

    /**
     * Set the signal that this process will receive when its parent dies.
     */
    void set_parent_death_signal(int signal);

    /**
     * Run command and redirect std::out to targetfile sepcified
     * @param cmdToExecWithArgs - cmd to be executed
     * @param targetfile - copy the std::out to targetfile path
     * @return process exit code
     */
    int runCommandAndRedirectOutput(const std::string & cmdToExecWithArgs,
                                    const std::optional<std::string> & targetfile);

}
