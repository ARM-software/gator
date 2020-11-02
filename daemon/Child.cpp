/* Copyright (C) 2010-2020 by Arm Limited. All rights reserved. */

#include "Child.h"

#include "CapturedXML.h"
#include "Command.h"
#include "ConfigurationXML.h"
#include "CounterXML.h"
#include "Driver.h"
#include "Drivers.h"
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
#include "armnn/ArmNNSource.h"
#include "lib/Assert.h"
#include "lib/FsUtils.h"
#include "lib/WaitForProcessPoller.h"
#include "lib/Waiter.h"
#include "mali_userspace/MaliHwCntrSource.h"
#include "xml/EventsXML.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>

std::atomic<Child *> Child::gSingleton = ATOMIC_VAR_INIT(nullptr);

extern void cleanUp();

constexpr int exceptionExitCode = 1;
constexpr int secondExceptionExitCode = 2;
// constexpr int secondSignalExitCode = 3; no longer used
// constexpr int alarmExitCode = 4; no longer used
constexpr int noSingletonExitCode = 5;
constexpr int signalFailedExitCode = 6;

void handleException()
{
    Child * const singleton = Child::getSingleton();

    if (singleton != nullptr) {
        singleton->cleanupException();
    }

    //if gatord is being used for a local capture: remove the incomplete APC directory.
    if (gSessionData.mLocalCapture) {
        logg.logMessage("Cleaning incomplete APC directory.");
        int errorCodeForRemovingDir = local_capture::removeDirAndAllContents(gSessionData.mTargetPath);
        if (errorCodeForRemovingDir != 0) {
            logg.logError("Could not remove incomplete APC directory.");
        }
    }

    // don't call exit handlers / global destructors
    // because other threads may be still running
    _exit(exceptionExitCode);
}

std::unique_ptr<Child> Child::createLocal(Drivers & drivers, const Child::Config & config)
{
    return std::unique_ptr<Child>(new Child(drivers, nullptr, config));
}

std::unique_ptr<Child> Child::createLive(Drivers & drivers, OlySocket & sock)
{
    return std::unique_ptr<Child>(new Child(drivers, &sock, {}));
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
        exit(noSingletonExitCode);
    }

    singleton->endSession(signum);
}

Child::Child(Drivers & drivers, OlySocket * sock, Child::Config config)
    : haltPipeline(),
      senderSem(),
      sender(),
      drivers(drivers),
      socket(sock),
      numExceptions(0),
      sessionEnded(),
      config(std::move(config))
{
    const int fd = eventfd(0, EFD_CLOEXEC);
    if (fd == -1) {
        logg.logError("eventfd failed (%d) %s", errno, strerror(errno));
        handleException();
    }

    sessionEndEventFd = fd;

    // update singleton
    const Child * const prevSingleton = gSingleton.exchange(this, std::memory_order_acq_rel);
    runtime_assert(prevSingleton == nullptr, "Two Child instances active concurrently");

    // Set up different handlers for signals
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGABRT, signalHandler);
    // we will wait on children outside of signal handler
    signal(SIGCHLD, SIG_DFL);

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

    // Disable line wrapping when generating xml files; carriage returns and indentation to be added manually
    mxmlSetWrapMargin(0);

    // Instantiate the Sender - must be done first, after which error messages can be sent
    sender.reset(new Sender(socket));

    auto & primarySourceProvider = drivers.getPrimarySourceProvider();
    // Populate gSessionData with the configuration

    std::set<SpeConfiguration> speConfigs = config.spes;
    std::set<CounterConfiguration> counterConfigs = config.events;
    bool countersAreDefaults = false;
    const auto checkError = [](const std::string & error) {
        if (!error.empty()) {
            logg.logError("%s", error.c_str());
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
                logg.logMessage("Overriding <counter> '%s' from configuration.xml", counter.counterName.c_str());
            }
        }
        for (auto && spe : result.speConfigurations) {
            if (config.spes.count(spe) == 0) {
                checkError(configuration_xml::addSpeToSet(speConfigs, std::move(spe)));
            }
            else {
                logg.logMessage("Overriding <spe> '%s' from configuration.xml", spe.id.c_str());
            }
        }
    }

    checkError(configuration_xml::setCounters(counterConfigs, !countersAreDefaults, drivers));

    // Initialize all drivers
    for (Driver * driver : drivers.getAll()) {
        driver->resetCounters();
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
                capturedSpes.push_back(std::move(capturedSpe.get()));
                claimed = true;
                break;
            }
        }

        if (!claimed) {
            logg.logWarning("No driver claimed %s", speConfig.id.c_str());
        }
    }

    // Start up and parse session xml
    if (socket != nullptr) {
        // Respond to Streamline requests
        StreamlineSetup ss(*socket, drivers, capturedSpes);
    }
    else {
        char * xmlString;
        if (gSessionData.mSessionXMLPath != nullptr) {
            xmlString = readFromDisk(gSessionData.mSessionXMLPath);
            if (xmlString != nullptr) {
                gSessionData.parseSessionXML(xmlString);
            }
            else {
                logg.logWarning("Unable to read session xml(%s) , using default values", gSessionData.mSessionXMLPath);
            }
            free(xmlString);
        }

        local_capture::createAPCDirectory(gSessionData.mTargetPath);
        local_capture::copyImages(gSessionData.mImages);
        sender->createDataFile(gSessionData.mAPCDir);
        // Write events XML
        events_xml::write(gSessionData.mAPCDir,
                          drivers.getAllConst(),
                          primarySourceProvider.getCpuInfo().getClusters());
    }

    std::set<int> appPids;
    bool enableOnCommandExec = false;
    if (!gSessionData.mCaptureCommand.empty()) {
        std::string captureCommand;
        for (auto const & cmd : gSessionData.mCaptureCommand) {
            captureCommand += " ";
            captureCommand += cmd;
        }
        logg.logWarning("Running command:%s", captureCommand.c_str());

        // This is set before any threads are started so it doesn't need
        // to be protected by a mutex
        command = std::make_shared<Command>(Command::run([this]() {
            if (gSessionData.mStopOnExit) {
                logg.logMessage("Ending session because command exited");
                endSession();
            }
        }));

        enableOnCommandExec = true;

        appPids.insert(command->getPid());
        logg.logMessage("Profiling pid: %d", command->getPid());
    }

    // set up stop thread early, so that ping commands get replied to, even if the
    // setup phase below takes a long time.
    std::thread stopThread {[this]() { stopThreadEntryPoint(); }};

    if (gSessionData.mWaitForProcessCommand != nullptr) {
        logg.logMessage("Waiting for pids for command '%s'", gSessionData.mWaitForProcessCommand);

        WaitForProcessPoller poller {gSessionData.mWaitForProcessCommand};

        while ((!poller.poll(appPids)) && !sessionEnded) {
            usleep(1000);
        }

        logg.logMessage("Got pids for command '%s'", gSessionData.mWaitForProcessCommand);
    }

    // we only consider --pid for stop on exit if we weren't given an
    // app to run
    std::set<int> watchPids = appPids.empty() ? gSessionData.mPids : appPids;

    appPids.insert(gSessionData.mPids.begin(), gSessionData.mPids.end());

    lib::Waiter waitTillStart;

    auto startedCallback = [&]() {
        waitTillStart.disable();
        if (command) {
            command->start();
        }
    };

    auto newPrimarySource = primarySourceProvider.createPrimarySource(senderSem,
                                                                      startedCallback,
                                                                      appPids,
                                                                      drivers.getFtraceDriver(),
                                                                      enableOnCommandExec);
    if (newPrimarySource == nullptr) {
        logg.logError("%s", primarySourceProvider.getPrepareFailedMessage());
        handleException();
    }

    auto & primarySource = *newPrimarySource;
    addSource(std::move(newPrimarySource));

    // Initialize ftrace source before child as it's slow and depends on nothing else
    // If initialized later, us gator with ftrace has time sync issues
    // Must be initialized before senderThread is started as senderThread checks externalSource
    if (!addSource(createExternalSource(senderSem, drivers))) {
        logg.logError("Unable to prepare external source for capture");
        handleException();
    }

    // initialize midgard hardware counters
    if (drivers.getMaliHwCntrs().countersEnabled()) {
        if (!addSource(mali_userspace::createMaliHwCntrSource(senderSem, drivers.getMaliHwCntrs()))) {
            logg.logError("Unable to prepare midgard hardware counters source for capture");
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

    std::thread watchPidsThread {};
    if (gSessionData.mStopOnExit && !watchPids.empty()) {
        watchPidsThread = std::thread([&]() { watchPidsThreadEntryPoint(watchPids, waitTillEnd); });
    }

    if (shouldStartUserSpaceSource(drivers.getAllPolledConst())) {
        if (!addSource(createUserSpaceSource(senderSem, drivers.getAllPolled()))) {
            logg.logError("Unable to prepare userspace source for capture");
            handleException();
        }
    }

    if (!addSource(armnn::createSource(drivers.getArmnnDriver().getCaptureController(), senderSem))) {
        logg.logError("Unable to prepare ArmNN source for capture");
        handleException();
    }

    // do this last so that monotonic start is close to start of profiling
    auto monotonicStart = primarySource.sendSummary();
    if (!monotonicStart) {
        logg.logError("Failed to send summary");
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

    if (watchPidsThread.joinable()) {
        watchPidsThread.join();
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
                            primarySourceProvider.getCpuInfo());
    }

    logg.logMessage("Profiling ended.");

    sources.clear();
    sender.reset();

    if (command) {
        logg.logMessage("Waiting for command (PID: %d)", command->getPid());
        command->join();
        logg.logMessage("Command finished");
    }
}

bool Child::addSource(std::unique_ptr<Source> source)
{
    if (!source) {
        return false;
    }
    std::lock_guard<std::mutex> lock {sessionEndedMutex};
    if (!sessionEnded) {
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
            _exit(signalFailedExitCode);
        }
        logg.logError("write failed (%d) %s", errno, strerror(errno));
        handleException();
    }
}

void Child::doEndSession()
{
    std::lock_guard<std::mutex> lock {sessionEndedMutex};

    sessionEnded = true;

    if (command) {
        command->cancel();
    }

    for (auto & source : sources) {
        source->interrupt();
    }
    sem_post(&haltPipeline);
}

void Child::cleanupException()
{
    if (numExceptions++ > 0) {
        // it is possible one of the below functions itself can cause an exception, thus allow only one exception
        logg.logMessage("Received multiple exceptions, terminating the child");

        // Something is really wrong, exit immediately
        _exit(secondExceptionExitCode);
    }

    if (command) {
        command->cancel();
    }

    if (socket != nullptr) {
        if (sender) {
            // send the error, regardless of the command sent by Streamline
            sender->writeData(logg.getLastError(), strlen(logg.getLastError()), ResponseType::ERROR, true);

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
    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-duration"), 0, 0, 0);

    waitTillStart.wait();

    // Time out after duration seconds
    if (waitTillEnd.wait_for(std::chrono::seconds(gSessionData.mDuration))) {
        logg.logMessage("Duration expired.");
        endSession();
    }

    logg.logMessage("Exit duration thread");
}

namespace {
    class StreamlineCommandHandler : public IStreamlineCommandHandler {
    public:
        StreamlineCommandHandler(Sender & sender) : sender(sender) {}

        State handleRequest(char *) override
        {
            logg.logMessage("INVESTIGATE: Received unknown command type COMMAND_REQUEST_XML");
            return State::PROCESS_COMMANDS;
        }
        State handleDeliver(char *) override
        {
            logg.logMessage("INVESTIGATE: Received unknown command type COMMAND_DELIVER_XML");
            return State::PROCESS_COMMANDS;
        }
        State handleApcStart() override
        {
            logg.logMessage("INVESTIGATE: Received unknown command type COMMAND_APC_START");
            return State::PROCESS_COMMANDS;
        }
        State handleApcStop() override
        {
            logg.logMessage("Stop command received.");
            return State::EXIT_APC_STOP;
        }
        State handleDisconnect() override
        {
            logg.logMessage("INVESTIGATE: Received unknown command type COMMAND_DISCONNECT");
            return State::PROCESS_COMMANDS;
        }
        State handlePing() override
        {
            // Ping is used to make sure gator is alive and requires an ACK as the response
            logg.logMessage("Ping command received.");
            sender.writeData(nullptr, 0, ResponseType::ACK);
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
        logg.logError("Monitor::init() failed: %d, (%s)", errno, strerror(errno));
        handleException();
    }
    if (!monitor.add(*sessionEndEventFd)) {
        logg.logError("Monitor::add(sessionEndEventFd=%d) failed: %d, (%s)",
                      *sessionEndEventFd,
                      errno,
                      strerror(errno));
        handleException();
    }
    if ((socket != nullptr) && !monitor.add(socket->getFd())) {
        logg.logError("Monitor::add(socket=%d) failed: %d, (%s)", socket->getFd(), errno, strerror(errno));
        handleException();
    }

    StreamlineCommandHandler commandHandler {*sender};

    while (true) {
        struct epoll_event ee;
        const int ready = monitor.wait(&ee, 1, -1);
        if (ready < 0) {
            logg.logError("Monitor::wait failed");
            handleException();
        }
        if (ready == 0) {
            continue;
        }

        if (ee.data.fd == *sessionEndEventFd) {
            if (signalNumber != 0) {
                logg.logMessage("Gator child is shutting down due to signal: %s", strsignal(signalNumber));
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

    logg.logMessage("Exit stop thread");
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
            logg.logError("wait failed: %d, (%s)", errno, strerror(errno));
        }
    } while (sendAllSources());

    // write end-of-capture sequence
    if (!gSessionData.mLocalCapture) {
        sender->writeData(nullptr, 0, ResponseType::APC_DATA);
    }

    logg.logMessage("Exit sender thread");
}

void Child::watchPidsThreadEntryPoint(std::set<int> & pids, const lib::Waiter & waiter)
{
    // rename thread
    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>("gatord-pidwatcher"), 0, 0, 0);

    while (!pids.empty()) {
        if (!waiter.wait_for(std::chrono::seconds(1))) {
            logg.logMessage("Exit watch pids thread by request");
            return;
        }

        const auto & alivePids = lib::getNumericalDirectoryEntries<int>("/proc");
        auto it = pids.begin();
        while (it != pids.end()) {
            if (alivePids.count(*it) == 0) {
                logg.logMessage("pid %d exited", *it);
                it = pids.erase(it);
            }
            else {
                ++it;
            }
        }
    }
    logg.logMessage("Ending session because all watched processes have exited");
    endSession();
    logg.logMessage("Exit watch pids thread");
}
