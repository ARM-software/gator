/* Copyright (C) 2020-2023 by Arm Limited. All rights reserved. */
#pragma once

#include <cstdint>
#include <set>
#include <string>

namespace current_config_xml {

    /**
     * Generates the current config XML of gatord.
     * Not to be confused with configuration.xml. This XML is sent
     * directly to Streamline to inform it of gatord's current configuration
     * so it can determine whether gatord should be killed.
     *
     * @param gatorMainPid the pid of gator-main not child
     * @param waitForProcessCommand is the command used with -Q
     * @param pidsToCapture what PIDs have been specified to profile
     * @returns XML string
     */
    std::string generateCurrentConfigXML(std::int32_t gatorMainPid,
                                         std::uint32_t uid,
                                         bool isSystemWide,
                                         bool isWaitingOnCommand,
                                         const char * waitForProcessCommand,
                                         const char * captureWorkingDir,
                                         std::set<int> & pidsToCapture);
};
