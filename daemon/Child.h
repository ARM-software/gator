/* Copyright (C) 2010-2025 by Arm Limited. All rights reserved. */

#ifndef __CHILD_H__
#define __CHILD_H__

#include "Configuration.h"
#include "Source.h"
#include "agents/agent_workers_process_holder.h"
#include "android/GpuTimelineLayerManager.h"
#include "capture/CaptureProcess.h"
#include "handleException.h"
#include "lib/AutoClosingFd.h"
#include "logging/suppliers.h"
#include "metrics/metric_group_set.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

class Drivers;
class Sender;
class OlySocket;
class Command;

namespace lib {
    class Waiter;
}

class Child : private agents::i_agent_worker_manager_callbacks_t {
public:
    struct Config {
        std::set<CounterConfiguration> events;
        std::set<SpeConfiguration> spes;
        metrics::metric_group_set_t metric_groups;
    };

    static std::unique_ptr<Child> createLocal(agents::i_agent_spawner_t & hi_priv_spawner,
                                              agents::i_agent_spawner_t & lo_priv_spawner,
                                              Drivers & drivers,
                                              const Config & config,
                                              capture::capture_process_event_listener_t & event_listener,
                                              const logging::log_access_ops_t & log_ops);
    static std::unique_ptr<Child> createLive(agents::i_agent_spawner_t & hi_priv_spawner,
                                             agents::i_agent_spawner_t & lo_priv_spawner,
                                             Drivers & drivers,
                                             OlySocket & sock,
                                             capture::capture_process_event_listener_t & event_listener,
                                             const logging::log_access_ops_t & log_ops);

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
    using GpuTimelineLayerManager = gator::android::GpuTimelineLayerManager;
    friend void ::handleException();

    static std::atomic<Child *> gSingleton;

    static Child * getSingleton();

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
    const logging::log_access_ops_t & log_ops;
    std::shared_ptr<Command> command {};
    agents::agent_worker_manager_holder_t agent_workers_process;

    Child(agents::i_agent_spawner_t & hi_priv_spawner,
          agents::i_agent_spawner_t & lo_priv_spawner,
          Drivers & drivers,
          OlySocket * sock,
          Config config,
          capture::capture_process_event_listener_t & event_listener,
          const logging::log_access_ops_t & log_ops);

    /**
     * Adds to sources if non empty
     * return true if not empty
     */
    template<typename S>
    bool addSource(std::shared_ptr<S> source);

    template<typename S, typename Callback>
    bool addSource(std::shared_ptr<S> source, Callback callback);

    /**
     * Send gator log and the end sequence of APC.
     * To be called at the end of capture or in case of
     * exception.
     */
    void sendGatorLog();
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
    void on_agent_thread_terminated() override;
    void on_terminal_signal(int signo) override;
};

#endif //__CHILD_H__
