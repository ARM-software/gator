/**
 * Copyright (C) ARM Limited 2010-2016. All rights reserved.
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
#include <semaphore.h>
#include <signal.h>

class PrimarySourceProvider;
class Sender;
class Source;
class OlySocket;

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

private:

    friend void ::handleException();

    static std::atomic<Child *> gSingleton;

    static Child * getSingleton();
    static void signalHandler(int signum);
    static void * durationThreadStaticEntryPoint(void *);
    static void * stopThreadStaticEntryPoint(void *);
    static void * senderThreadStaticEntryPoint(void *);

    sem_t haltPipeline;
    sem_t senderThreadStarted;
    sem_t startProfile;
    sem_t senderSem;
    std::unique_ptr<Source> primarySource;
    std::unique_ptr<Source> externalSource;
    std::unique_ptr<Source> userSpaceSource;
    std::unique_ptr<Source> midgardHwSource;
    std::unique_ptr<Sender> sender;
    PrimarySourceProvider & primarySourceProvider;
    OlySocket * socket;
    int numExceptions;

    Child(PrimarySourceProvider & primarySourceProvider);
    Child(PrimarySourceProvider & primarySourceProvider, OlySocket & sock);
    Child(bool local, PrimarySourceProvider & primarySourceProvider, OlySocket * sock);
    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(Child);

    void cleanupException();
    void * durationThreadEntryPoint();
    void * stopThreadEntryPoint();
    void * senderThreadEntryPoint();
};

#endif //__CHILD_H__
