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

class PrimarySourceProvider;
class Sender;
class Source;
class OlySocket;

class Child
{
public:

    static std::unique_ptr<Child> createLocal(PrimarySourceProvider & primarySourceProvider);
    static std::unique_ptr<Child> createLive(PrimarySourceProvider & primarySourceProvider, OlySocket & sock, int numConnections);

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
    int mNumConnections;

    Child(PrimarySourceProvider & primarySourceProvider);
    Child(PrimarySourceProvider & primarySourceProvider, OlySocket & sock, int numConnections);
    Child(bool local, PrimarySourceProvider & primarySourceProvider, OlySocket * sock, int numConnections);
    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(Child);

    void cleanupException();
    void * durationThreadEntryPoint();
    void * stopThreadEntryPoint();
    void * senderThreadEntryPoint();
};

#endif //__CHILD_H__
