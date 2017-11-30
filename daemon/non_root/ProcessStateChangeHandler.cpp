/* Copyright (c) 2017 by Arm Limited. All rights reserved. */

#include "non_root/ProcessStateChangeHandler.h"
#include "Buffer.h"
#include "Logging.h"

namespace non_root
{
    ProcessStateChangeHandler::ProcessStateChangeHandler(Buffer & counterBuffer_, Buffer & miscBuffer_, PerCoreMixedFrameBuffer & switchBuffers_,
                                                         const std::map<NonRootCounter, int> & enabledCounters_)
            : miscBuffer(miscBuffer_),
              cookies(),
              counterBuffer(counterBuffer_),
              switchBuffers(switchBuffers_),
              enabledCounters(enabledCounters_),
              cookieCounter(3)
    {
    }

    void ProcessStateChangeHandler::onNewProcess(unsigned long long timestampNS, unsigned long core, int ppid, int pid,
                                                 int tid, const std::string & comm, const std::string & exe)
    {
        (void) ppid;

        const cookie_type cookie = getCookie(timestampNS, core, pid, tid, exe, comm);

        miscBuffer.nameFrameThreadNameMessage(timestampNS, core, tid, comm);
        miscBuffer.activityFrameLinkMessage(timestampNS, cookie, pid, tid);
    }

    void ProcessStateChangeHandler::onCommChange(unsigned long long timestampNS, unsigned long core, int tid,
                                                 const std::string & comm)
    {
        miscBuffer.nameFrameThreadNameMessage(timestampNS, core, tid, comm);
    }

    void ProcessStateChangeHandler::onExeChange(unsigned long long timestampNS, unsigned long core, int pid, int tid,
                                                const std::string & exe)
    {
        const cookie_type cookie = getCookie(timestampNS, core, pid, tid, exe, exe);

        miscBuffer.activityFrameLinkMessage(timestampNS, cookie, pid, tid);
    }

    void ProcessStateChangeHandler::onExitProcess(unsigned long long timestampNS, unsigned long core, int tid)
    {
        switchBuffers[core].schedFrameThreadExitMessage(timestampNS, core, tid);
    }

    void ProcessStateChangeHandler::absoluteCounter(unsigned long long timestampNS, unsigned long core, int tid,
                                                    AbsoluteProcessCounter id, unsigned long long value)
    {
        const auto it = enabledCounters.find(NonRootCounter(id));

        if (it != enabledCounters.end()) {
            counterBuffer.threadCounterMessage(timestampNS, core, tid, it->second, value);
        }
    }

    void ProcessStateChangeHandler::deltaCounter(unsigned long long timestampNS, unsigned long core, int tid,
                                                 DeltaProcessCounter id, unsigned long long delta)
    {
        const auto it = enabledCounters.find(NonRootCounter(id));

        if (it != enabledCounters.end()) {
            counterBuffer.threadCounterMessage(timestampNS, core, tid, it->second, delta);
        }
    }

    void ProcessStateChangeHandler::threadActivity(unsigned long long timestampNS, int tid,
                                                   unsigned long utimeDeltaTicks, unsigned long stimeDeltaTicks,
                                                   unsigned long core)
    {
        (void) utimeDeltaTicks;
        (void) stimeDeltaTicks;

        // send fake activity switch event
        switchBuffers[core].schedFrameSwitchMessage(timestampNS, core, tid, 0);
    }

    ProcessStateChangeHandler::cookie_type ProcessStateChangeHandler::getCookie(unsigned long long timestampNS,
                                                                                unsigned long core, int pid, int tid,
                                                                                const std::string & exe,
                                                                                const std::string & comm)
    {
        if (pid == 0) {
            return COOKIE_KERNEL;
        }
        else if ((pid != tid) || (exe.empty() && comm.empty())) {
            return COOKIE_UNKNOWN;
        }
        else {
            const std::string & nameToUse = (!exe.empty() ? exe : comm); // assumes comm is the name of the exe, but the exe was deleted
            const auto it = cookies.find(nameToUse);

            if (it != cookies.end()) {
                return it->second;
            }
            else {
                const int newCookie = cookieCounter++;
                cookies[nameToUse] = newCookie;
                miscBuffer.nameFrameCookieNameMessage(timestampNS, core, newCookie, nameToUse);
                return newCookie;
            }
        }
    }

    void ProcessStateChangeHandler::idle(unsigned long long timestampNS, unsigned long core)
    {
        switchBuffers[core].schedFrameSwitchMessage(timestampNS, core, 0, 0);
    }
}
