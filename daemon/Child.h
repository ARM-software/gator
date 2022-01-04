/* Copyright (C) 2010-2021 by Arm Limited. All rights reserved. */

#ifndef __CHILD_H__
#define __CHILD_H__

#include "Configuration.h"
#include "Source.h"
#include "lib/AutoClosingFd.h"
#include "logging/suppliers.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

#include <semaphore.h>

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

    static std::unique_ptr<Child> createLocal(Drivers & drivers,
                                              const Config & config,
                                              logging::last_log_error_supplier_t last_error_supplier,
                                              logging::log_setup_supplier_t log_setup_supplier);
    static std::unique_ptr<Child> createLive(Drivers & drivers,
                                             OlySocket & sock,
                                             logging::last_log_error_supplier_t last_error_supplier,
                                             logging::log_setup_supplier_t log_setup_supplier);

    ~Child();

    // Intentionally unimplemented
    Child(const Child &) = delete;
    Child & operator=(const Child &) = delete;
    Child(Child &&) = delete;
    Child & operator=(Child &&) = delete;

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
    std::vector<std::unique_ptr<Source>> sources {};
    std::unique_ptr<Sender> sender;
    Drivers & drivers;
    OlySocket * socket;
    int numExceptions;
    std::mutex sessionEndedMutex {};
    lib::AutoClosingFd sessionEndEventFd {};
    std::atomic_bool sessionEnded;
    std::atomic_int signalNumber {0};
    Config config;
    logging::last_log_error_supplier_t last_error_supplier;
    logging::log_setup_supplier_t log_setup_supplier;
    std::shared_ptr<Command> command {};

    Child(Drivers & drivers,
          OlySocket * sock,
          Config config,
          logging::last_log_error_supplier_t last_error_supplier,
          logging::log_setup_supplier_t log_setup_supplier);

    /**
     * Adds to sources if non empty
     * return true if not empty
     */
    bool addSource(std::unique_ptr<Source> source);

    void cleanupException();
    void durationThreadEntryPoint(const lib::Waiter & waitTillStart, const lib::Waiter & waitTillEnd);
    void stopThreadEntryPoint();
    void senderThreadEntryPoint();
    /**
     * Writes data to the sender.
     *
     * @return true if there will be more to send again on at least one source, false otherwise (EOF)
     */
    bool sendAllSources();
    void watchPidsThreadEntryPoint(std::set<int> &, const lib::Waiter & waiter);
    void doEndSession();
};

#endif //__CHILD_H__
