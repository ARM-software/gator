/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Child.h"

#include "lib/Assert.h"
#include "lib/Waiter.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sys/wait.h>

#include <algorithm>
#include <thread>

#include "CapturedXML.h"
#include "CounterXML.h"
#include "Command.h"
#include "ConfigurationXML.h"
#include "Driver.h"
#include "Drivers.h"
#include "PrimarySourceProvider.h"
#include "ExternalSource.h"
#include "ICpuInfo.h"
#include "LocalCapture.h"
#include "Logging.h"
#include "OlySocket.h"
#include "OlyUtility.h"
#include "Sender.h"
#include "SessionData.h"
#include "StreamlineSetup.h"
#include "UserSpaceSource.h"
#include "PolledDriver.h"
#include "EventsXML.h"
#include "lib/WaitForProcessPoller.h"
#include "mali_userspace/MaliHwCntrSource.h"

std::atomic<Child *> Child::gSingleton = ATOMIC_VAR_INIT(nullptr);

extern void cleanUp();

constexpr int exceptionExitCode = 1;
constexpr int secondExceptionExitCode = 2;
constexpr int secondSignalExitCode = 3;
constexpr int alarmExitCode = 4;
constexpr int noSingletonExitCode = 5;

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
        if (errorCodeForRemovingDir != 0)
            logg.logError("Could not remove incomplete APC directory.");
    }

    // don't call exit handlers / global destructors
    // because other threads may be still running
    _exit(exceptionExitCode);
}

std::unique_ptr<Child> Child::createLocal(Drivers & drivers, const Child::Config & config)
{
    return std::unique_ptr < Child > (new Child(drivers, nullptr, config));
}

std::unique_ptr<Child> Child::createLive(Drivers & drivers, OlySocket & sock)
{
    return std::unique_ptr < Child > (new Child(drivers, &sock, { }));
}

Child * Child::getSingleton()
{
    return gSingleton.load(std::memory_order_acquire);
}

void Child::signalHandler(int signum)
{
    static bool beenHere = false;

    Child * const singleton = getSingleton();
    if (singleton == nullptr) {
        exit(noSingletonExitCode);
    }

    if (beenHere || signum == SIGALRM) {
        logg.logError("Gator child is being forced to shut down: %s", strsignal(signum));
        if (!singleton->commandTerminated)
            logg.logError("Failed to terminate command (%d)", singleton->commandPid);
        if (signum == SIGALRM)
            exit(alarmExitCode);
        else
            exit(secondSignalExitCode);
    }
    else {
        beenHere = true;
        logg.logMessage("Gator child is shutting down: %s", strsignal(signum));

        singleton->endSession();
    }
}

Child::Child(Drivers & drivers, OlySocket * sock, const Child::Config & config)
        : haltPipeline(),
          senderThreadStarted(),
          senderSem(),
          primarySource(),
          externalSource(),
          userSpaceSource(),
          maliHwSource(),
          sender(),
          drivers(drivers),
          socket(sock),
          numExceptions(0),
          sessionEnded(),
          commandTerminated(true),
          commandPid(),
          config(config),
          sharedData(shared_memory::make_unique<SharedData>())
{
    // update singleton
    const Child * const prevSingleton = gSingleton.exchange(this, std::memory_order_acq_rel);
    runtime_assert(prevSingleton == nullptr, "Two Child instances active concurrently");

    // Set up different handlers for signals
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGABRT, signalHandler);
    signal(SIGALRM, signalHandler);
    // we will wait on children outside of signal handler
    signal(SIGCHLD, SIG_DFL);

    // Initialize semaphores
    sem_init(&senderThreadStarted, 0, 0);
    sem_init(&senderSem, 0, 0);

    sessionEnded.clear();

    gSessionData.mSessionIsActive = true;
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
    for (Driver *driver : drivers.getAll()) {
        driver->resetCounters();
    }

    // Set up counters using the associated driver's setup function
    for (int i = 0; i < MAX_PERFORMANCE_COUNTERS; i++) {
        Counter & counter = gSessionData.mCounters[i];
        if (counter.isEnabled()) {
            counter.getDriver()->setupCounter(counter);
        }
    }
    std::vector<CapturedSpe> capturedSpes;
    for (const auto & speConfig : speConfigs) {
        bool claimed = false;

        for (Driver * driver : drivers.getAll()) {
            auto && capturedSpe = driver->setupSpe(speConfig);
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
    if (socket) {
        // Respond to Streamline requests
        StreamlineSetup ss(socket, drivers, capturedSpes);
    }
    else {
        char* xmlString;
        if (gSessionData.mSessionXMLPath) {
            xmlString = readFromDisk(gSessionData.mSessionXMLPath);
            if (xmlString) {
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
        events_xml::write(gSessionData.mAPCDir, drivers.getAllConst(),
                          primarySourceProvider.getCpuInfo().getClusters());
    }

    std::set<int> appPids;
    std::thread commandThread { };
    bool enableOnCommandExec = false;
    if (!gSessionData.mCaptureCommand.empty()) {
        std::string command;
        for (auto const& cmd : gSessionData.mCaptureCommand) {
            command += " ";
            command += cmd;
        }
        logg.logWarning("Running command:%s", command.c_str());
        Command commandResult = runCommand(sharedData->startProfile, [this]() {
            commandTerminated = true;
            if (gSessionData.mStopOnExit)
            {
                logg.logMessage("Ending session because command exited");
                endSession();
            }
        });
        enableOnCommandExec = true;
        commandPid = commandResult.pid;
        commandTerminated = false;
        commandThread = std::move(commandResult.thread);
        appPids.insert(commandPid);
        logg.logMessage("Profiling pid: %d", commandPid);
    }

    if (gSessionData.mWaitForProcessCommand != nullptr) {
        logg.logMessage("Waiting for pids for command '%s'", gSessionData.mWaitForProcessCommand);

        WaitForProcessPoller poller { gSessionData.mWaitForProcessCommand };

        while (!poller.poll(appPids)) {
            usleep(1000);
        }

        logg.logMessage("Got pids for command '%s'", gSessionData.mWaitForProcessCommand);
    }

    // we only consider --pid for stop on exit if we weren't given an
    // app to run
    std::set<int> watchPids = appPids.empty() ? gSessionData.mPids : appPids;

    appPids.insert(gSessionData.mPids.begin(), gSessionData.mPids.end());

    // Set up the driver; must be done after gSessionData.mPerfCounterType[] is populated
    primarySource = primarySourceProvider.createPrimarySource(*this, senderSem, sharedData->startProfile, appPids,
                                                              drivers.getFtraceDriver(), enableOnCommandExec);
    if (primarySource == nullptr) {
        logg.logError("Failed to init primary capture source");
        handleException();
    }

    // set up stop thread early, so that ping commands get replied to, even if the
    // setup phase below takes a long time.
    std::thread stopThread { };
    if (socket) {
        stopThread = std::thread([this]() {stopThreadEntryPoint();});
    }

    if (primarySourceProvider.supportsMaliCapture() && primarySourceProvider.isCapturingMaliCounters()
            && !primarySourceProvider.supportsMaliCaptureSampleRate(gSessionData.mSampleRate)) {
        logg.logError("Mali counters are not supported with Sample Rate: %i.", gSessionData.mSampleRate);
        handleException();
    }

    // Initialize ftrace source before child as it's slow and dependens on nothing else
    // If initialized later, us gator with ftrace has time sync issues
    // Must be initialized before senderThread is started as senderThread checks externalSource
    externalSource.reset(new ExternalSource(*this, &senderSem, drivers));
    if (!externalSource->prepare()) {
        logg.logError("Unable to prepare external source for capture");
        handleException();
    }
    externalSource->start();

    // Must be after session XML is parsed
    if (!primarySource->prepare()) {
        logg.logError("%s", primarySourceProvider.getPrepareFailedMessage());
        handleException();
    }
    auto getMonotonicStarted = [&primarySourceProvider]() -> std::int64_t {return primarySourceProvider.getMonotonicStarted();};
    // initialize midgard hardware counters
    if (drivers.getMaliHwCntrs().countersEnabled()) {
        maliHwSource.reset(
                new mali_userspace::MaliHwCntrSource(*this, &senderSem, getMonotonicStarted, drivers.getMaliHwCntrs()));
        if (!maliHwSource->prepare()) {
            logg.logError("Unable to prepare midgard hardware counters source for capture");
            handleException();
        }
        maliHwSource->start();
    }

    // Sender thread shall be halted until it is signaled for one shot mode
    sem_init(&haltPipeline, 0, gSessionData.mOneShot ? 0 : 2);

    // Create the duration and sender threads
    lib::Waiter waiter;

    std::thread durationThread { };
    if (gSessionData.mDuration > 0) {
        durationThread = std::thread([&]() {durationThreadEntryPoint(waiter);});
    }

    std::thread senderThread { [this]() {senderThreadEntryPoint();} };

    std::thread watchPidsThread { };
    if (gSessionData.mStopOnExit && !watchPids.empty()) {
        watchPidsThread = std::thread([&]() {watchPidsThreadEntryPoint(watchPids, waiter);});
    }

    if (UserSpaceSource::shouldStart(drivers.getAllPolledConst())) {
        userSpaceSource.reset(new UserSpaceSource(*this, &senderSem, getMonotonicStarted, drivers.getAllPolled()));
        if (!userSpaceSource->prepare()) {
            logg.logError("Unable to prepare userspace source for capture");
            handleException();
        }
        userSpaceSource->start();
    }

    // Wait until thread has started
    sem_wait(&senderThreadStarted);

    // Start profiling
    primarySource->run();

    // wake all sleepers
    waiter.disable();

    // Wait for the other threads to exit
    if (userSpaceSource != NULL) {
        userSpaceSource->join();
    }
    if (maliHwSource != NULL) {
        maliHwSource->join();
    }
    externalSource->join();

    if (watchPidsThread.joinable()) {
        watchPidsThread.join();
    }
    senderThread.join();
    if (durationThread.joinable()) {
        durationThread.join();
    }

    // Shutting down the connection should break the stop thread which is stalling on the socket recv() function
    if (socket) {
        logg.logMessage("Waiting on stop thread");
        socket->shutdownConnection();
        stopThread.join();
    }

    // Write the captured xml file
    if (gSessionData.mLocalCapture) {
        auto & maliCntrDriver = drivers.getMaliHwCntrs();
        captured_xml::write(gSessionData.mAPCDir, capturedSpes, primarySourceProvider, maliCntrDriver.getDeviceGpuIds());
        counters_xml::write(gSessionData.mAPCDir, drivers.getAllConst(), primarySourceProvider.getCpuInfo());
    }

    logg.logMessage("Profiling ended.");

    userSpaceSource.reset();
    maliHwSource.reset();
    externalSource.reset();
    primarySource.reset();
    sender.reset();

    if (commandThread.joinable()) {
        logg.logMessage("Waiting for command");
        commandThread.join();
        logg.logMessage("Command finished");
    }
}

void Child::terminateCommand()
{
    if (!commandTerminated) {
        logg.logMessage("Terminating %d", commandPid);
        //Passing a negative commandPid will kill every process in the process group.
        //Needed so that all child processes are terminated when the parent is; cleaning up processes created by Streamline/gator.
        kill(-commandPid, SIGTERM);
    }
}

void Child::endSession()
{
    if (sessionEnded.test_and_set()) {
        return; // someone else is running or has ran this
    }

    // Safety net in case endSession does not complete within 5 seconds
    // Note this is unlikely to ever fire for a local capture
    // because main sends another signal after 1 second.
    // We use a separate thread here rather than ::alarm because other uses
    // of sleep interfere with SIGALARM
    std::thread { []() {::sleep(5); Child::signalHandler(SIGALRM);} }.detach();

    terminateCommand();

    gSessionData.mSessionIsActive = false;
    if (primarySource != nullptr) {
        primarySource->interrupt();
    }
    if (externalSource != nullptr) {
        externalSource->interrupt();
    }
    if (maliHwSource != nullptr) {
        maliHwSource->interrupt();
    }
    if (userSpaceSource != nullptr) {
        userSpaceSource->interrupt();
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

    terminateCommand();

    if (socket) {
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

void Child::durationThreadEntryPoint(const lib::Waiter & waiter)
{
    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-duration"), 0, 0, 0);

    sem_wait(&sharedData->startProfile);
    sem_post(&sharedData->startProfile); // post for anyone else waiting

    // Time out after duration seconds
    if (waiter.wait_for(std::chrono::seconds(gSessionData.mDuration))) {
        logg.logMessage("Duration expired.");
        endSession();
    }

    logg.logMessage("Exit duration thread");
}

void Child::stopThreadEntryPoint()
{
    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-stopper"), 0, 0, 0);
    while (gSessionData.mSessionIsActive) {
        // This thread will stall until the APC_STOP or PING command is received over the socket or the socket is disconnected
        unsigned char header[5];
        const int result = socket->receiveNBytes(reinterpret_cast<char *>(&header), sizeof(header));
        const char type = header[0];
        const int length = (header[1] << 0) | (header[2] << 8) | (header[3] << 16) | (header[4] << 24);
        if (result == -1) {
            logg.logMessage("Receive failed.");
            endSession();
        }
        else if (result > 0) {
            if ((type != COMMAND_APC_STOP) && (type != COMMAND_PING)) {
                logg.logMessage("INVESTIGATE: Received unknown command type %d", type);
            }
            else {
                // verify a length of zero
                if (length == 0) {
                    // inform the parent process that the capturing has been stopped by the host.
                    kill(getppid(), Child::SIG_LIVE_CAPTURE_STOPPED);

                    if (type == COMMAND_APC_STOP) {
                        logg.logMessage("Stop command received.");
                        endSession();
                    }
                    else {
                        // Ping is used to make sure gator is alive and requires an ACK as the response
                        logg.logMessage("Ping command received.");
                        sender->writeData(NULL, 0, ResponseType::ACK);
                    }
                }
                else {
                    logg.logMessage("INVESTIGATE: Received APC_STOP or PING command but with length = %d", length);
                }
            }
        }
    }

    logg.logMessage("Exit stop thread");
}

void Child::senderThreadEntryPoint()
{
    sem_post(&senderThreadStarted);
    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-sender"), 0, 0, 0);
    sem_wait(&haltPipeline);

    while ((!externalSource->isDone()) || ((maliHwSource != NULL) && (!maliHwSource->isDone()))
            || ((userSpaceSource != NULL) && (!userSpaceSource->isDone())) || (!primarySource->isDone())) {
        sem_wait(&senderSem);

        externalSource->write(sender.get());
        if (maliHwSource != NULL) {
            maliHwSource->write(sender.get());
        }
        if (userSpaceSource != NULL) {
            userSpaceSource->write(sender.get());
        }
        primarySource->write(sender.get());
    }

    // flush one more time to ensure any slop is cleared up
    {
        externalSource->write(sender.get());
        if (maliHwSource != NULL) {
            maliHwSource->write(sender.get());
        }
        if (userSpaceSource != NULL) {
            userSpaceSource->write(sender.get());
        }
        primarySource->write(sender.get());
    }

    // write end-of-capture sequence
    if (!gSessionData.mLocalCapture) {
        sender->writeData(nullptr, 0, ResponseType::APC_DATA);
    }

    logg.logMessage("Exit sender thread");
}

void Child::watchPidsThreadEntryPoint(std::set<int> & pids, const lib::Waiter & waiter)
{
    while (!pids.empty()) {
        if (!waiter.wait_for(std::chrono::seconds(1))) {
            logg.logMessage("Exit watch pids thread by request");
            return;
        }

        auto it = pids.begin();
        while (it != pids.end()) {
            if (kill(*it, 0) < 0) {
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
