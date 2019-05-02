/* Copyright (c) 2018 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_LINUX_PROC_PROCESS_CHILDREN_H
#define INCLUDE_LINUX_PROC_PROCESS_CHILDREN_H

#include <set>

namespace lnx
{
    /**
     * Inherently racey function to collect child tids because threads can be created and destroyed while this is running
     *
     * @param tid
     * @return as many of the known child tids (including child processes)
     */
    std::set<int> getChildTids(int tid);
}

#endif
