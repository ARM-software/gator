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

#include <atomic>
#include <memory>
#include <functional>
#include <semaphore.h>
#include <signal.h>
#include <map>
#include <mutex>
#include <set>

class PrimarySourceProvider;
class Sender;
class Source;
class OlySocket;

void handleException();

class Child
{
public:

    static std::unique_ptr<Child> createLocal(PrimarySourceProvider & primarySourceProvider);
    static std::unique_ptr<Child> createLive(PrimarySourceProvider & primarySourceProvider, OlySocket & sock);

    // using one of user signals for interprocess communication based on Linux signals
    static const int SIG_LIVE_CAPTURE_STOPPED = SIGUSR1;

    ~Child();

    void run();
    void endSession();
    void setEvents(const std::map<std::string, int> &eventsMap_);

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
    std::unique_ptr<Source> midgardHwSource;
    std::unique_ptr<Sender> sender;
    PrimarySourceProvider & primarySourceProvider;
    OlySocket * socket;
    int numExceptions;
    std::timed_mutex sleepMutex;
    std::atomic_flag sessionEnded;
    std::atomic_bool commandTerminated;
    int commandPid;

    std::map<std::string, int> eventsMap;
    std::unique_ptr<SharedData, std::function<void(SharedData *)>> sharedData;

    Child(PrimarySourceProvider & primarySourceProvider);
    Child(PrimarySourceProvider & primarySourceProvider, OlySocket & sock);
    Child(bool local, PrimarySourceProvider & primarySourceProvider, OlySocket * sock);
    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(Child)
    ;

    void cleanupException();
    void terminateCommand();
    void durationThreadEntryPoint();
    void stopThreadEntryPoint();
    void senderThreadEntryPoint();
    void watchPidsThreadEntryPoint(std::set<int> &);
};

#endif //__CHILD_H__
