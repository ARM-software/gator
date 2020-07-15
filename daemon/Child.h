/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#ifndef __CHILD_H__
#define __CHILD_H__

#include "Configuration.h"
#include "Source.h"
#include "lib/AutoClosingFd.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <semaphore.h>
#include <set>
#include <vector>

class Drivers;
class Sender;
class OlySocket;
class Command;

namespace lib {
    class Waiter;
}

void handleException() __attribute__((noreturn));

class Child {
public:
    struct Config {
        std::set<CounterConfiguration> events;
        std::set<SpeConfiguration> spes;
    };

    static std::unique_ptr<Child> createLocal(Drivers & drivers, const Config & config);
    static std::unique_ptr<Child> createLive(Drivers & drivers, OlySocket & sock);

    ~Child();

    void run();

    void endSession(int signum = 0);

private:
    friend void ::handleException();

    static std::atomic<Child *> gSingleton;

    static Child * getSingleton();
    static void signalHandler(int signum);
    static void childSignalHandler(int signum);

    sem_t haltPipeline;
    sem_t senderSem;
    std::unique_ptr<Source> primarySource;
    std::vector<std::unique_ptr<Source>> otherSources {};
    std::unique_ptr<Sender> sender;
    Drivers & drivers;
    OlySocket * socket;
    int numExceptions;
    std::mutex sessionEndedMutex {};
    lib::AutoClosingFd sessionEndEventFd {};
    std::atomic_bool sessionEnded;
    std::atomic_int signalNumber {0};

    Config config;
    std::shared_ptr<Command> command {};

    Child(Drivers & drivers, OlySocket * sock, Config config);
    // Intentionally unimplemented
    Child(const Child &) = delete;
    Child & operator=(const Child &) = delete;
    Child(Child &&) = delete;
    Child & operator=(Child &&) = delete;

    /**
     * Prepares and if that was successful, starts and add to other sources
     * return true if prepare did
     */
    bool prepareAndStart(Source * source);

    void cleanupException();
    void durationThreadEntryPoint(const lib::Waiter & waitTillStart, const lib::Waiter & waitTillEnd);
    void stopThreadEntryPoint();
    void senderThreadEntryPoint();
    void watchPidsThreadEntryPoint(std::set<int> &, const lib::Waiter & waiter);
    void doEndSession();
};

#endif //__CHILD_H__
