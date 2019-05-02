/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __CHILD_H__
#define __CHILD_H__

#include "ClassBoilerPlate.h"

#include "lib/SharedMemory.h"

#include "Configuration.h"

#include <atomic>
#include <memory>
#include <functional>
#include <semaphore.h>
#include <signal.h>
#include <map>
#include <mutex>
#include <set>
#include <vector>

class Drivers;
class Sender;
class Source;
class OlySocket;

namespace lib
{
    class Waiter;
}

void handleException();

class Child
{
public:
    struct Config {
        std::set<CounterConfiguration> events;
        std::set<SpeConfiguration> spes;
    };

    static std::unique_ptr<Child> createLocal(Drivers & drivers, const Config & config);
    static std::unique_ptr<Child> createLive(Drivers & drivers, OlySocket & sock);

    // using one of user signals for interprocess communication based on Linux signals
    static const int SIG_LIVE_CAPTURE_STOPPED = SIGUSR1;

    ~Child();

    void run();
    void endSession();

private:

    friend void ::handleException();

    // Stuff that needs to be accessed in any child processes
    struct SharedData
    {
        sem_t startProfile;

        SharedData()
                : startProfile()
        {
            sem_init(&startProfile, 1, 0);
        }

        ~SharedData()
        {
            sem_destroy(&startProfile);
        }
    };

    static std::atomic<Child *> gSingleton;

    static Child * getSingleton();
    static void signalHandler(int signum);
    static void childSignalHandler(int signum);

    /**
     * Sleeps for a specified time but can be interrupted
     *
     * @param timeout_duration
     * @return true if slept for whole time
     */
    template<class Rep, class Period>
    bool sleep(const std::chrono::duration<Rep, Period>& timeout_duration);

    sem_t haltPipeline;
    sem_t senderThreadStarted;
    sem_t senderSem;
    std::unique_ptr<Source> primarySource;
    std::unique_ptr<Source> externalSource;
    std::unique_ptr<Source> userSpaceSource;
    std::unique_ptr<Source> maliHwSource;
    std::unique_ptr<Sender> sender;
    Drivers & drivers;
    OlySocket * socket;
    int numExceptions;
    std::atomic_flag sessionEnded;
    std::atomic_bool commandTerminated;
    int commandPid;

    Config config;
    shared_memory::unique_ptr<SharedData> sharedData;

    Child(Drivers & drivers, OlySocket * sock, const Config & config);
    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(Child)
    ;

    void cleanupException();
    void terminateCommand();
    void durationThreadEntryPoint(const lib::Waiter & waiter);
    void stopThreadEntryPoint();
    void senderThreadEntryPoint();
    void watchPidsThreadEntryPoint(std::set<int> &, const lib::Waiter & waiter);
};

#endif //__CHILD_H__
