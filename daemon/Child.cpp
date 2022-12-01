/* Copyright (C) 2010-2022 by Arm Limited. All rights reserved. */

#include "Child.h"

#include "CapturedXML.h"
#include "ConfigurationXML.h"
#include "CounterXML.h"
#include "Driver.h"
#include "Drivers.h"
#include "ExitStatus.h"
#include "ExternalSource.h"
#include "ICpuInfo.h"
#include "LocalCapture.h"
#include "Logging.h"
#include "Monitor.h"
#include "OlySocket.h"
#include "OlyUtility.h"
#include "PolledDriver.h"
#include "PrimarySourceProvider.h"
#include "Sender.h"
#include "SessionData.h"
#include "StreamlineSetup.h"
#include "UserSpaceSource.h"
#include "agents/perfetto/perfetto_driver.h"
#include "agents/spawn_agent.h"
#include "armnn/ArmNNSource.h"
#include "capture/CaptureProcess.h"
#include "capture/Environment.h"
#include "lib/Assert.h"
#include "lib/FsUtils.h"
#include "lib/WaitForProcessPoller.h"
#include "lib/Waiter.h"
#include "lib/perfetto_utils.h"
#include "logging/global_log.h"
#include "mali_userspace/MaliHwCntrSource.h"
#include "xml/EventsXML.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <utility>

#include <boost/asio/detached.hpp>

#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

std::atomic<Child *> Child::gSingleton = ATOMIC_VAR_INIT(nullptr);

extern void cleanUp();

void handleException()
{
    Child * const singleton = Child::getSingleton();

    if (singleton != nullptr) {
        singleton->cleanupException();
    }

    //if gatord is being used for a local capture: remove the incomplete APC directory.
    if (gSessionData.mLocalCapture) {
        LOG_DEBUG("Cleaning incomplete APC directory.");
        int errorCodeForRemovingDir = local_capture::removeDirAndAllContents(gSessionData.mTargetPath);
        if (errorCodeForRemovingDir != 0) {
            LOG_ERROR("Could not remove incomplete APC directory.");
        }
    }

    // don't call exit handlers / global destructors
    // because other threads may be still running
    _exit(EXCEPTION_EXIT_CODE);
}

std::unique_ptr<Child> Child::createLocal(agents::i_agent_spawner_t & hi_priv_spawner,
                                          agents::i_agent_spawner_t & lo_priv_spawner,
                                          Drivers & drivers,
                                          const Child::Config & config,
                                          capture::capture_process_event_listener_t & event_listener,
                                          logging::last_log_error_supplier_t last_error_supplier,
                                          logging::log_setup_supplier_t log_setup_supplier)
{
    return std::unique_ptr<Child>(new Child(hi_priv_spawner,
                                            lo_priv_spawner,
                                            drivers,
                                            nullptr,
                                            config,
                                            event_listener,
                                            std::move(last_error_supplier),
                                            std::move(log_setup_supplier)));
}

std::unique_ptr<Child> Child::createLive(agents::i_agent_spawner_t & hi_priv_spawner,
                                         agents::i_agent_spawner_t & lo_priv_spawner,
                                         Drivers & drivers,
                                         OlySocket & sock,
                                         capture::capture_process_event_listener_t & event_listener,
                                         logging::last_log_error_supplier_t last_error_supplier,
                                         logging::log_setup_supplier_t log_setup_supplier)
{
    return std::unique_ptr<Child>(new Child(hi_priv_spawner,
                                            lo_priv_spawner,
                                            drivers,
                                            &sock,
                                            {},
                                            event_listener,
                                            std::move(last_error_supplier),
                                            std::move(log_setup_supplier)));
}

Child * Child::getSingleton()
{
    return gSingleton.load(std::memory_order_acquire);
}

void Child::signalHandler(int signum)
{
    Child * const singleton = getSingleton();
    if (singleton == nullptr) {
        // this should not be possible because we set the singleton before
        // installing the handlers
        exit(NO_SINGLETON_EXIT_CODE);
    }

    singleton->endSession(signum);
}

Child::Child(agents::i_agent_spawner_t & hi_priv_spawner,
             agents::i_agent_spawner_t & lo_priv_spawner,
             Drivers & drivers,
             OlySocket * sock,
             Child::Config config,
             capture::capture_process_event_listener_t & event_listener,
             logging::last_log_error_supplier_t last_error_supplier,
             logging::log_setup_supplier_t log_setup_supplier)
    : haltPipeline(),
      senderSem(),
      sender(),
      drivers(drivers),
      socket(sock),
      event_listener(event_listener),
      numExceptions(0),
      sessionEnded(),
      config(std::move(config)),
      last_error_supplier(std::move(last_error_supplier)),
      log_setup_supplier(std::move(log_setup_supplier)),
      agent_workers_process(*this, hi_priv_spawner, lo_priv_spawner)
{
    const int fd = eventfd(0, EFD_CLOEXEC);
    if (fd == -1) {
        LOG_ERROR("eventfd failed (%d) %s", errno, strerror(errno));
        handleException();
    }

    sessionEndEventFd = fd;

    // update singleton
    const Child * const prevSingleton = gSingleton.exchange(this, std::memory_order_acq_rel);
    runtime_assert(prevSingleton == nullptr, "Two Child instances active concurrently");

    // Initialize semaphores
    sem_init(&senderSem, 0, 0);

    sessionEnded = false;
}

Child::~Child()
{
    // update singleton
    const Child * const prevSingleton = gSingleton.exchange(nullptr, std::memory_order_acq_rel);
    runtime_assert(prevSingleton == this, "Exchanged Child::gSingleton with something other than this");
}

void Child::run()
{
    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-child"), 0, 0, 0);

    // TODO: better place for this
    agent_workers_process.start();

    // Disable line wrapping when generating xml files; carriage returns and indentation to be added manually
    mxmlSetWrapMargin(0);

    // Instantiate the Sender - must be done first, after which error messages can be sent
    sender = std::make_unique<Sender>(socket);

    auto & primarySourceProvider = drivers.getPrimarySourceProvider();
    // Populate gSessionData with the configuration

    std::set<SpeConfiguration> speConfigs = config.spes;
    std::set<CounterConfiguration> counterConfigs = config.events;
    bool countersAreDefaults = false;
    const auto checkError = [](const std::string & error) {
        if (!error.empty()) {
            LOG_ERROR("%s", error.c_str());
        }
    };

    // Only read the configuration.xml if no counters were already given (via cmdline) or the configuration.xml
    // was explicitly given. Given counters take priority.
    if ((config.events.empty() && config.spes.empty()) || gSessionData.mConfigurationXMLPath != nullptr) {
        auto && result = configuration_xml::getConfigurationXML(primarySourceProvider.getCpuInfo().getClusters());
        countersAreDefaults = result.isDefault;
        for (auto && counter : result.counterConfigurations) {
            if (config.events.count(counter) == 0) {
                checkError(configuration_xml::addCounterToSet(counterConfigs, std::move(counter)));
            }
            else {
                LOG_DEBUG("Overriding <counter> '%s' from configuration.xml", counter.counterName.c_str());
            }
        }
        for (auto && spe : result.speConfigurations) {
            if (config.spes.count(spe) == 0) {
                checkError(configuration_xml::addSpeToSet(speConfigs, std::move(spe)));
            }
            else {
                LOG_DEBUG("Overriding <spe> '%s' from configuration.xml", spe.id.c_str());
            }
        }
    }

    checkError(configuration_xml::setCounters(counterConfigs, !countersAreDefaults, drivers));

    // Initialize all drivers and register their constants with the global constant list
    for (Driver * driver : drivers.getAll()) {
        driver->resetCounters();

        driver->insertConstants(gSessionData.mConstants);
    }

    // Set up counters using the associated driver's setup function
    for (auto & counter : gSessionData.mCounters) {
        if (counter.isEnabled()) {
            counter.getDriver()->setupCounter(counter);
        }
    }

    std::vector<CapturedSpe> capturedSpes;
    for (const auto & speConfig : speConfigs) {
        bool claimed = false;

        for (Driver * driver : drivers.getAll()) {
            auto && capturedSpe = driver->setupSpe(gSessionData.mSpeSampleRate, speConfig);
            if (capturedSpe) {
                capturedSpes.push_back(std::move(*capturedSpe));
                claimed = true;
                break;
            }
        }

        if (!claimed) {
            LOG_WARNING("No driver claimed %s", speConfig.id.c_str());
        }
    }

    // Start up and parse session xml
    if (socket != nullptr) {
        // Respond to Streamline requests
        StreamlineSetup ss(*socket, drivers, capturedSpes, log_setup_supplier);
    }
    else {
        char * xmlString;
        if (gSessionData.mSessionXMLPath != nullptr) {
            xmlString = readFromDisk(gSessionData.mSessionXMLPath);
            if (xmlString != nullptr) {
                gSessionData.parseSessionXML(xmlString);
            }
            else {
                LOG_WARNING("Unable to read session xml(%s) , using default values", gSessionData.mSessionXMLPath);
            }
            free(xmlString);
        }

        local_capture::createAPCDirectory(gSessionData.mTargetPath);
        local_capture::copyImages(gSessionData.mImages);
        sender->createDataFile(gSessionData.mAPCDir);
        // Write events XML
        events_xml::write(gSessionData.mAPCDir,
                          drivers.getAllConst(),
                          primarySourceProvider.getCpuInfo().getClusters(),
                          primarySourceProvider.getDetectedUncorePmus());
    }

    // set up stop thread early, so that ping commands get replied to, even if the
    // setup phase below takes a long time.
    std::thread stopThread {[this]() { stopThreadEntryPoint(); }};

    // tell the controller that we're ready for the app to start
    auto execTargetCallback = [this]() {
        LOG_DEBUG("Received exec_target callback");
        if (!event_listener.waiting_for_target()) {
            handleException();
        }
    };

    lib::Waiter waitTillStart;
    lib::Waiter waitForExternalSourceAgent;
    lib::Waiter waitForPerfettoAgent;

    auto startedCallback = [&]() {
        LOG_DEBUG("Received start capture callback");
        waitTillStart.disable();
    };

    bool enablePerfettoAgent = drivers.getPerfettoDriver().perfettoEnabled();

    // Initialize ftrace source before child as it's slow and depends on nothing else
    // If initialized later, us gator with ftrace has time sync issues
    // Must be initialized before senderThread is started as senderThread checks externalSource
    if (!addSource(createExternalSource(senderSem, drivers),
                   [this, &waitForExternalSourceAgent, &waitForPerfettoAgent, enablePerfettoAgent](auto & source) {
                       this->agent_workers_process.async_add_external_source(
                           source,
                           [&waitForExternalSourceAgent](bool success) {
                               waitForExternalSourceAgent.disable();
                               if (!success) {
                                   handleException();
                               }
                               else {
                                   LOG_DEBUG("Started ext_source agent");
                               }
                           });
#if defined(ANDROID) || defined(__ANDROID__)
                       if (enablePerfettoAgent) {
                           this->agent_workers_process.async_add_perfetto_source(
                               source,
                               [&waitForPerfettoAgent](bool success) {
                                   waitForPerfettoAgent.disable();
                                   if (!success) {
                                       LOG_ERROR("Failed to start perfetto agent");
                                       handleException();
                                   }
                                   else {
                                       LOG_DEBUG("Started perfetto agent");
                                   }
                               });
                       }
                       else {
                           waitForPerfettoAgent.disable();
                       }
#else
                       (void) enablePerfettoAgent;
                       waitForPerfettoAgent.disable();
#endif
                   })) {
        LOG_ERROR("Unable to prepare external source for capture");
        handleException();
    }

    // wait for the ext agent to start
    if (!sessionEnded) {
        LOG_DEBUG("Waiting for agents to start");
        waitForExternalSourceAgent.wait();
        waitForPerfettoAgent.wait();
        LOG_DEBUG("Waiting for agents complete");
    }

    // create the primary source last as it will launch the process, which may lead to a race receiving external messages
    auto newPrimarySource = primarySourceProvider.createPrimarySource(
        senderSem,
        *sender,
        [this]() -> bool { return sessionEnded; },
        execTargetCallback,
        startedCallback,
        gSessionData.mPids,
        drivers.getFtraceDriver(),
        !gSessionData.mCaptureCommand.empty(),
        agent_workers_process);
    if (newPrimarySource == nullptr) {
        LOG_ERROR("%s", primarySourceProvider.getPrepareFailedMessage());
        handleException();
    }

    auto & primarySource = *newPrimarySource;
    addSource(std::move(newPrimarySource));

    // initialize midgard hardware counters
    if (drivers.getMaliHwCntrs().countersEnabled()) {
        if (!addSource(mali_userspace::createMaliHwCntrSource(senderSem, drivers.getMaliHwCntrs()))) {
            LOG_ERROR("Unable to prepare midgard hardware counters source for capture");
            handleException();
        }
    }

    // Sender thread shall be halted until it is signaled for one shot mode
    sem_init(&haltPipeline, 0, gSessionData.mOneShot ? 0 : 2);

    // Create the duration and sender threads
    lib::Waiter waitTillEnd;

    std::thread durationThread {};
    if (gSessionData.mDuration > 0) {
        durationThread = std::thread([&]() { durationThreadEntryPoint(waitTillStart, waitTillEnd); });
    }

    if (shouldStartUserSpaceSource(drivers.getAllPolledConst())) {
        if (!addSource(createUserSpaceSource(senderSem, drivers.getAllPolled()))) {
            LOG_ERROR("Unable to prepare userspace source for capture");
            handleException();
        }
    }

    if (!addSource(armnn::createSource(drivers.getArmnnDriver().getCaptureController(), senderSem))) {
        LOG_ERROR("Unable to prepare ArmNN source for capture");
        handleException();
    }

    // do this last so that monotonic start is close to start of profiling
    auto monotonicStart = primarySource.sendSummary();
    if (!monotonicStart) {
        LOG_ERROR("Failed to send summary");
        handleException();
    }

    // Start profiling
    std::vector<std::thread> sourceThreads {};
    for (auto & source : sources) {
        sourceThreads.emplace_back(&Source::run, source.get(), *monotonicStart, [this]() { endSession(); });
    }

    // must start sender thread after we've added all sources
    senderThreadEntryPoint();

    // wake all sleepers
    waitTillEnd.disable();

    // Wait for the other threads to exit
    for (auto & thread : sourceThreads) {
        thread.join();
    }

    if (durationThread.joinable()) {
        durationThread.join();
    }

    stopThread.join();

    // Write the captured xml file
    if (gSessionData.mLocalCapture) {
        auto & maliCntrDriver = drivers.getMaliHwCntrs();
        captured_xml::write(gSessionData.mAPCDir,
                            capturedSpes,
                            primarySourceProvider,
                            maliCntrDriver.getDeviceGpuIds());
        counters_xml::write(gSessionData.mAPCDir,
                            primarySourceProvider.supportsMultiEbs(),
                            drivers.getAllConst(),
                            primarySourceProvider.getCpuInfo(),
                            log_setup_supplier);
    }

    LOG_DEBUG("Profiling ended.");

    // must happen before sources is cleared
    agent_workers_process.join();

    sources.clear();
    sender.reset();
}

template<typename S>
bool Child::addSource(std::unique_ptr<S> source)
{
    return addSource(std::move(source), [](auto const & /*source*/) {});
}

template<typename S, typename Callback>
bool Child::addSource(std::unique_ptr<S> source, Callback callback)
{
    if (!source) {
        return false;
    }
    std::lock_guard<std::mutex> lock {sessionEndedMutex};
    if (!sessionEnded) {
        callback(*source);
        sources.push_back(std::move(source));
    }
    return true;
}

void Child::endSession(int signum)
{
    signalNumber = signum;
    std::uint64_t value = 1;
    if (::write(*sessionEndEventFd, &value, sizeof(value)) != sizeof(value)) {
        if (signum != 0) {
            // we're in a signal handler so it's not safe to log
            // and if this has failed something has gone really wrong
            _exit(SIGNAL_FAILED_EXIT_CODE);
        }
        LOG_ERROR("write failed (%d) %s", errno, strerror(errno));
        handleException();
    }
}

void Child::doEndSession()
{
    std::lock_guard<std::mutex> lock {sessionEndedMutex};

    sessionEnded = true;

    for (auto & source : sources) {
        source->interrupt();
    }
    sem_post(&haltPipeline);
}

void Child::cleanupException()
{
    if (numExceptions++ > 0) {
        // it is possible one of the below functions itself can cause an exception, thus allow only one exception
        LOG_DEBUG("Received multiple exceptions, terminating the child");

        // Something is really wrong, exit immediately
        _exit(SECOND_EXCEPTION_EXIT_CODE);
    }

    if (socket != nullptr) {
        if (sender) {
            // send the error, regardless of the command sent by Streamline
            auto last_error = last_error_supplier();
            sender->writeData(last_error.data(), last_error.size(), ResponseType::ERROR, true);

            // cannot close the socket before Streamline issues the command, so wait for the command before exiting
            if (gSessionData.mWaitingOnCommand) {
                char discard;
                socket->receiveNBytes(&discard, 1);
            }

            // Ensure all data is flushed
            socket->shutdownConnection();

            // this indirectly calls close socket which will ensure the data has been sent
            sender.reset();
        }
    }
}

void Child::durationThreadEntryPoint(const lib::Waiter & waitTillStart, const lib::Waiter & waitTillEnd)
{
    if (sessionEnded) {
        return;
    }
    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-duration"), 0, 0, 0);

    waitTillStart.wait();

    // Time out after duration seconds
    if (waitTillEnd.wait_for(std::chrono::seconds(gSessionData.mDuration))) {
        LOG_DEBUG("Duration expired.");
        endSession();
    }

    LOG_DEBUG("Exit duration thread");
}

namespace {
    class StreamlineCommandHandler : public IStreamlineCommandHandler {
    public:
        explicit StreamlineCommandHandler(Sender & sender) : sender(sender) {}

        State handleRequest(char * /* unused */) override
        {
            LOG_DEBUG("INVESTIGATE: Received unknown command type COMMAND_REQUEST_XML");
            return State::PROCESS_COMMANDS;
        }
        State handleDeliver(char *) override
        {
            LOG_DEBUG("INVESTIGATE: Received unknown command type COMMAND_DELIVER_XML");
            return State::PROCESS_COMMANDS;
        }
        State handleApcStart() override
        {
            LOG_DEBUG("INVESTIGATE: Received unknown command type COMMAND_APC_START");
            return State::PROCESS_COMMANDS;
        }
        State handleApcStop() override
        {
            LOG_DEBUG("Stop command received.");
            return State::EXIT_APC_STOP;
        }
        State handleDisconnect() override
        {
            LOG_DEBUG("INVESTIGATE: Received unknown command type COMMAND_DISCONNECT");
            return State::PROCESS_COMMANDS;
        }
        State handlePing() override
        {
            // Ping is used to make sure gator is alive and requires an ACK as the response
            LOG_DEBUG("Ping command received.");
            sender.writeData(nullptr, 0, ResponseType::ACK);
            return State::PROCESS_COMMANDS;
        }
        State handleExit() override
        {
            LOG_DEBUG("INVESTIGATE: Received unknown command type COMMAND_EXIT");
            return State::EXIT_OK;
        }
        State handleRequestCurrentConfig() override
        {
            LOG_DEBUG("INVESTIGATE: Received unknown command type COMMAND_REQUEST_CURRENT_CONFIG");
            return State::PROCESS_COMMANDS;
        }

    private:
        Sender & sender;
    };
}

void Child::stopThreadEntryPoint()
{
    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-stopper"), 0, 0, 0);
    Monitor monitor {};
    if (!monitor.init()) {
        LOG_ERROR("Monitor::init() failed: %d, (%s)", errno, strerror(errno));
        handleException();
    }
    if (!monitor.add(*sessionEndEventFd)) {
        LOG_ERROR("Monitor::add(sessionEndEventFd=%d) failed: %d, (%s)", *sessionEndEventFd, errno, strerror(errno));
        handleException();
    }
    if ((socket != nullptr) && !monitor.add(socket->getFd())) {
        LOG_ERROR("Monitor::add(socket=%d) failed: %d, (%s)", socket->getFd(), errno, strerror(errno));
        handleException();
    }

    StreamlineCommandHandler commandHandler {*sender};

    while (true) {
        struct epoll_event ee;
        const int ready = monitor.wait(&ee, 1, -1);
        if (ready < 0) {
            LOG_ERROR("Monitor::wait failed");
            handleException();
        }
        if (ready == 0) {
            continue;
        }

        if (ee.data.fd == *sessionEndEventFd) {
            if (signalNumber != 0) {
                LOG_DEBUG("Gator child is shutting down due to signal: %s", strsignal(signalNumber));
            }
            break;
        }

        assert(ee.data.fd == socket->getFd());

        // This thread will stall until the APC_STOP or PING command is received over the socket or the socket is disconnected
        const auto result = streamlineSetupCommandIteration(*socket, commandHandler, [](bool) -> void {});
        if (result != IStreamlineCommandHandler::State::PROCESS_COMMANDS) {
            break;
        }
    }

    doEndSession();

    LOG_DEBUG("Exit stop thread");
}

bool Child::sendAllSources()
{
    bool done = true;
    for (auto & source : sources) {
        // bitwise &, no short circuit
        done &= source->write(*sender);
    }
    return !done;
}

void Child::senderThreadEntryPoint()
{
    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-sender"), 0, 0, 0);
    sem_wait(&haltPipeline);

    do {
        if (sem_wait(&senderSem) != 0) {
            LOG_ERROR("wait failed: %d, (%s)", errno, strerror(errno));
        }
    } while (sendAllSources());

    // write end-of-capture sequence
    if (!gSessionData.mLocalCapture) {
        sender->writeData(nullptr, 0, ResponseType::APC_DATA);
    }

    LOG_DEBUG("Exit sender thread");
}

void Child::watchPidsThreadEntryPoint(std::set<int> & pids, const lib::Waiter & waiter)
{
    // rename thread
    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>("gatord-pidwatcher"), 0, 0, 0);

    while (!pids.empty()) {
        if (!waiter.wait_for(std::chrono::seconds(1))) {
            LOG_DEBUG("Exit watch pids thread by request");
            return;
        }

        const auto & alivePids = lib::getNumericalDirectoryEntries<int>("/proc");
        auto it = pids.begin();
        while (it != pids.end()) {
            if (alivePids.count(*it) == 0) {
                LOG_DEBUG("pid %d exited", *it);
                it = pids.erase(it);
            }
            else {
                ++it;
            }
        }
    }
    LOG_DEBUG("Ending session because all watched processes have exited");
    endSession();
    LOG_DEBUG("Exit watch pids thread");
}

void Child::on_terminal_signal(int signo)
{
    endSession(signo);
}

void Child::on_agent_thread_terminated()
{
    endSession();
}
