/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#ifndef INCLUDE_NON_ROOT_PROCESSSTATECHANGEHANDLER_H
#define INCLUDE_NON_ROOT_PROCESSSTATECHANGEHANDLER_H

#include "non_root/ProcessCounter.h"
#include "non_root/MixedFrameBuffer.h"
#include "non_root/PerCoreMixedFrameBuffer.h"

#include <map>
#include <string>
#include <list>

namespace non_root
{
    class ProcessStateChangeHandler
    {
    public:

        ProcessStateChangeHandler(Buffer & counterBuffer, Buffer & miscBuffer, PerCoreMixedFrameBuffer & switchBuffers, const std::map<NonRootCounter, int> & enabledCounters);

        void onNewProcess(unsigned long long timestampNS, unsigned long core, int ppid, int pid, int tid,
                          const std::string & comm, const std::string & exe);
        void onCommChange(unsigned long long timestampNS, unsigned long core, int tid, const std::string & comm);
        void onExeChange(unsigned long long timestampNS, unsigned long core, int pid, int tid, const std::string & exe);
        void onExitProcess(unsigned long long timestampNS, unsigned long core, int tid);
        void absoluteCounter(unsigned long long timestampNS, unsigned long core, int tid, AbsoluteProcessCounter id,
                             unsigned long long value);
        void deltaCounter(unsigned long long timestampNS, unsigned long core, int tid, DeltaProcessCounter id,
                          unsigned long long delta);
        void threadActivity(unsigned long long timestampNS, int tid, unsigned long utimeDeltaTicks,
                            unsigned long stimeDeltaTicks, unsigned long core);
        void idle(unsigned long long timestampNS, unsigned long core);

    private:

        typedef int cookie_type;

        static constexpr const cookie_type COOKIE_KERNEL = 0;
        static constexpr const cookie_type COOKIE_UNKNOWN = ~cookie_type(0);

        MixedFrameBuffer miscBuffer;
        std::map<std::string, cookie_type> cookies;
        Buffer & counterBuffer;
        PerCoreMixedFrameBuffer & switchBuffers;
        const std::map<NonRootCounter, int> & enabledCounters;
        int cookieCounter;

        cookie_type getCookie(unsigned long long timestampNS, unsigned long core, int pid, int tid, const std::string & exe,
                              const std::string & comm);

    };
}

#endif /* INCLUDE_NON_ROOT_PROCESSSTATECHANGEHANDLER_H */
