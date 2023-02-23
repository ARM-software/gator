/* Copyright (C) 2010-2022 by Arm Limited. All rights reserved. */

#ifndef __CHILD_H__
#define __CHILD_H__

#include "Configuration.h"
#include "Source.h"
#include "agents/agent_workers_process.h"
#include "capture/CaptureProcess.h"
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

    static std::unique_ptr<Child> createLocal(agents::i_agent_spawner_t & hi_priv_spawner,
                                              agents::i_agent_spawner_t & lo_priv_spawner,
                                              Drivers & drivers,
                                              const Config & config,
                                              capture::capture_process_event_listener_t & event_listener,
                                              logging::last_log_error_supplier_t last_error_supplier,
                                              logging::log_setup_supplier_t log_setup_supplier);
    static std::unique_ptr<Child> createLive(agents::i_agent_spawner_t & hi_priv_spawner,
                                             agents::i_agent_spawner_t & lo_priv_spawner,
                                             Drivers & drivers,
                                             OlySocket & sock,
                                             capture::capture_process_event_listener_t & event_listener,
                                             logging::last_log_error_supplier_t last_error_supplier,
                                             logging::log_setup_supplier_t log_setup_supplier);

    ~Child();

    // Intentionally unimplemented
    Child(const Child &) = delete;
    Child & operator=(const Child &) = delete;
    Child(Child &&) = delete;
    Child & operator=(Child &&) = delete;

    /**
     * @brief Runs the capture process
     */
    void run();

    void endSession(int signum = 0);

private:
    friend void ::handleException();
    friend class agents::agent_workers_process_manager_t<Child>;

    static std::atomic<Child *> gSingleton;

    static Child * getSingleton();
    static void signalHandler(int signum);
    static void childSignalHandler(int signum);

    sem_t haltPipeline;
    sem_t senderSem;
    std::vector<std::shared_ptr<Source>> sources {};
    std::unique_ptr<Sender> sender;
    Drivers & drivers;
    OlySocket * socket;
    capture::capture_process_event_listener_t & event_listener;
    int numExceptions;
    std::mutex sessionEndedMutex {};
    lib::AutoClosingFd sessionEndEventFd {};
    std::atomic_bool sessionEnded;
    std::atomic_int signalNumber {0};
    Config config;
    logging::last_log_error_supplier_t last_error_supplier;
    logging::log_setup_supplier_t log_setup_supplier;
    std::shared_ptr<Command> command {};
    agents::agent_workers_process_t<Child> agent_workers_process;

    Child(agents::i_agent_spawner_t & hi_priv_spawner,
          agents::i_agent_spawner_t & lo_priv_spawner,
          Drivers & drivers,
          OlySocket * sock,
          Config config,
          capture::capture_process_event_listener_t & event_listener,
          logging::last_log_error_supplier_t last_error_supplier,
          logging::log_setup_supplier_t log_setup_supplier);

    /**
     * Adds to sources if non empty
     * return true if not empty
     */
    template<typename S>
    bool addSource(std::shared_ptr<S> source);

    template<typename S, typename Callback>
    bool addSource(std::shared_ptr<S> source, Callback callback);

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

    // for agent_workers_process_t
    void on_terminal_signal(int signo);
    void on_agent_thread_terminated();
};

#endif //__CHILD_H__
