/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Child.h"

#include "lib/Assert.h"
#include "lib/SharedMemory.h"
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
#include "PrimarySourceProvider.h"
#include "ExternalSource.h"
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
#include "WaitForProcessPoller.h"
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

    // don't call exit handlers / global destructors
    // because other threads may be still running
    _exit(exceptionExitCode);
}

std::unique_ptr<Child> Child::createLocal(PrimarySourceProvider & primarySourceProvider)
{
    return std::unique_ptr < Child > (new Child(primarySourceProvider));
}

std::unique_ptr<Child> Child::createLive(PrimarySourceProvider & primarySourceProvider, OlySocket & sock)
{
    return std::unique_ptr < Child > (new Child(primarySourceProvider, sock));
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

Child::Child(bool local, PrimarySourceProvider & psp, OlySocket * sock)
        : haltPipeline(),
          senderThreadStarted(),
          senderSem(),
          primarySource(),
          externalSource(),
          userSpaceSource(),
          midgardHwSource(),
          sender(),
          primarySourceProvider(psp),
          socket(sock),
          numExceptions(0),
          sleepMutex(),
          sessionEnded(),
          commandTerminated(true),
          commandPid(),
          eventsMap(),
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

    // enable sleeping
    sleepMutex.lock();

    sessionEnded.clear();

    gSessionData.mSessionIsActive = true;
    gSessionData.mLocalCapture = local;
}

Child::Child(PrimarySourceProvider & psp)
        : Child(true, psp, nullptr)
{
}

Child::Child(PrimarySourceProvider & psp, OlySocket & sock)
        : Child(false, psp, &sock)
{
}

Child::~Child()
{
    // update singleton
    const Child * const prevSingleton = gSingleton.exchange(nullptr, std::memory_order_acq_rel);
    runtime_assert(prevSingleton == this, "Exchanged Child::gSingleton with something other than this");
}

void Child::setEvents(const std::map<std::string, int> &eventsMap_)
{
    eventsMap = eventsMap_;
}

template<class Rep, class Period>
bool Child::sleep(const std::chrono::duration<Rep, Period>& timeout_duration)
{
    if (sleepMutex.try_lock_for(timeout_duration)) {
        sleepMutex.unlock();
        return false;
    }
    else {
        return true;
    }
}

void Child::run()
{
    std::unique_ptr<LocalCapture> localCapture;

    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-child"), 0, 0, 0);

    // Disable line wrapping when generating xml files; carriage returns and indentation to be added manually
    mxmlSetWrapMargin(0);

    // Instantiate the Sender - must be done first, after which error messages can be sent
    sender.reset(new Sender(socket));
    // Populate gSessionData with the configuration

    if (eventsMap.empty()) {
        ConfigurationXML configuration;
    }

    int perfCounterCount = 0;
    if (!eventsMap.empty()) {
        // read attributes
        for (const auto& eventmap : eventsMap) {
            const std::string & counterName = eventmap.first;
            const int event = eventmap.second;
            const auto end = gSessionData.globalCounterToEventMap.end();
            const auto it = std::find_if(gSessionData.globalCounterToEventMap.begin(), end, [&counterName] (const std::pair<std::string, int> & pair) {
                        return strcasecmp(pair.first.c_str(), counterName.c_str()) == 0;
                    });
            const bool hasEventsXmlCounter = (it != end);
            const int counterEvent = (hasEventsXmlCounter ? it->second : -1);

            Counter & counter = gSessionData.mCounters[perfCounterCount];
            counter.clear();
            counter.setType(counterName.c_str());

            // if hasEventsXmlCounter, then then event is defined as a counter with 'counter'/'type' attribute
            // in events.xml. Use the specified event from events.xml (which may be -1 if not relevant)
            // overriding anything from user map. This is necessary for cycle counters for example where
            // they have a name "XXX_ccnt" but also often an event code. If not the event code -1 is used
            // which is incorrect.
            if (hasEventsXmlCounter) {
                counter.setEvent(counterEvent);
            }
            // the counter is not in events.xml. This usually means it is a PMU slot counter
            // the user specified the event code, use that
            else if (event > -1) {
                counter.setEvent(event);
            }
            // the counter is not in events.xml. This usually means it is a PMU slot counter, but since
            // the user has not specified an event code, this is probably incorrect.
            else if (strcasestr(counterName.c_str(), "_cnt")) {
                logg.logWarning("Counter '%s' does not have an event code specified, PMU slot counters require an event code", counterName.c_str());
            }
            else {
                logg.logWarning("Counter '%s' was not recognized", counterName.c_str());
            }

            // handle all other performance counters
            if (perfCounterCount >= MAX_PERFORMANCE_COUNTERS) {
                perfCounterCount++;
                break;
            }
            counter.setEnabled(true);

            // Associate a driver with each counter
            for (Driver *driver = Driver::getHead(); driver != NULL; driver = driver->getNext()) {
                if (driver->claimCounter(counter)) {
                    if (counter.getDriver() != NULL) {
                        logg.logError("More than one driver has claimed %s:%i", counter.getType(), counter.getEvent());
                        handleException();
                    }
                    counter.setDriver(driver);
                }
            }

            // If no driver is associated with the counter, disable it
            if (counter.getDriver() == NULL) {
                logg.logWarning("No driver has claimed %s:%i", counter.getType(), counter.getEvent());
                counter.setEnabled(false);
            }
            if (counter.isEnabled()) {
                // update counter index
                perfCounterCount++;
            }
        }
        gSessionData.mCcnDriver.validateCounters();
    }

    // Initialize all drivers
    for (Driver *driver = Driver::getHead(); driver != NULL; driver = driver->getNext()) {
        driver->resetCounters();
    }

    // Set up counters using the associated driver's setup function
    for (int i = 0; i < MAX_PERFORMANCE_COUNTERS; i++) {
        Counter & counter = gSessionData.mCounters[i];
        if (counter.isEnabled()) {
            counter.getDriver()->setupCounter(counter);
        }
    }

    // Start up and parse session xml
    if (socket) {
        // Respond to Streamline requests
        StreamlineSetup ss(socket);
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
        localCapture.reset(new LocalCapture());
        localCapture->createAPCDirectory(gSessionData.mTargetPath);
        localCapture->copyImages(gSessionData.mImages);
        sender->createDataFile(gSessionData.mAPCDir);
        // Write events XML
        EventsXML eventsXML;
        eventsXML.write(gSessionData.mAPCDir);
    }

    std::set<int> appPids;
    std::thread commandThread { };
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
            endSession();
        });
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
    primarySource = primarySourceProvider.createPrimarySource(*this, senderSem, sharedData->startProfile, appPids);
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

    if (gSessionData.mPrimarySource->supportsMaliCapture() && gSessionData.mPrimarySource->isCapturingMaliCounters()
            && !gSessionData.mPrimarySource->supportsMaliCaptureSampleRate(gSessionData.mSampleRate)) {
        logg.logError("Mali counters are not supported with Sample Rate: %i.", gSessionData.mSampleRate);
        handleException();
    }

    // Initialize ftrace source before child as it's slow and dependens on nothing else
    // If initialized later, us gator with ftrace has time sync issues
    // Must be initialized before senderThread is started as senderThread checks externalSource
    externalSource.reset(new ExternalSource(*this, &senderSem));
    if (!externalSource->prepare()) {
        logg.logError("Unable to prepare external source for capture");
        handleException();
    }
    externalSource->start();

    // Must be after session XML is parsed
    if (!primarySource->prepare()) {
        logg.logError("%s", gSessionData.mPrimarySource->getPrepareFailedMessage());
        handleException();
    }

    // initialize midgard hardware counters
    if (gSessionData.mMaliHwCntrs.countersEnabled()) {
        midgardHwSource.reset(new mali_userspace::MaliHwCntrSource(*this, &senderSem));
        if (!midgardHwSource->prepare()) {
            logg.logError("Unable to prepare midgard hardware counters source for capture");
            handleException();
        }
        midgardHwSource->start();
    }

    // Sender thread shall be halted until it is signaled for one shot mode
    sem_init(&haltPipeline, 0, gSessionData.mOneShot ? 0 : 2);

    // Create the duration and sender threads
    std::thread durationThread { };
    if (gSessionData.mDuration > 0) {
        durationThread = std::thread([this]() {durationThreadEntryPoint();});
    }

    std::thread senderThread { [this]() {senderThreadEntryPoint();} };

    std::thread watchPidsThread { };
    if (gSessionData.mStopOnExit && !watchPids.empty()) {
        watchPidsThread = std::thread([this, &watchPids]() {watchPidsThreadEntryPoint(watchPids);});
    }

    if (UserSpaceSource::shouldStart()) {
        userSpaceSource.reset(new UserSpaceSource(*this, &senderSem));
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
    sleepMutex.unlock();

    // Wait for the other threads to exit
    if (userSpaceSource != NULL) {
        userSpaceSource->join();
    }
    if (midgardHwSource != NULL) {
        midgardHwSource->join();
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
        CapturedXML capturedXML;
        capturedXML.write(gSessionData.mAPCDir);
        CounterXML counterXML;
        counterXML.write(gSessionData.mAPCDir);
    }

    logg.logMessage("Profiling ended.");

    userSpaceSource.reset();
    midgardHwSource.reset();
    externalSource.reset();
    primarySource.reset();
    sender.reset();
    localCapture.reset();

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
        kill(commandPid, SIGTERM);
    }
}

void Child::endSession()
{
    if (sessionEnded.test_and_set()) {
        return; // someone else is running or has ran this
    }

    // Safety net in case endSession does not complete within 5 seconds
    // Note this is unlikely to ever fire because main sends another signal
    // after 1 second
    alarm(5);

    gSessionData.mSessionIsActive = false;
    if (primarySource != nullptr) {
        primarySource->interrupt();
    }
    if (externalSource != nullptr) {
        externalSource->interrupt();
    }
    if (midgardHwSource != nullptr) {
        midgardHwSource->interrupt();
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
            sender->writeData(logg.getLastError(), strlen(logg.getLastError()), RESPONSE_ERROR, true);

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

void Child::durationThreadEntryPoint()
{
    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-duration"), 0, 0, 0);

    sem_wait(&sharedData->startProfile);
    sem_post(&sharedData->startProfile); // post for anyone else waiting

    // Time out after duration seconds
    // Add a second for host-side filtering
    if (sleep(std::chrono::seconds(gSessionData.mDuration + 1))) {
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
                        sender->writeData(NULL, 0, RESPONSE_ACK);
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
    char end_sequence[] = { RESPONSE_APC_DATA, 0, 0, 0, 0 };

    sem_post(&senderThreadStarted);
    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-sender"), 0, 0, 0);
    sem_wait(&haltPipeline);

    while ((!externalSource->isDone()) || ((midgardHwSource != NULL) && (!midgardHwSource->isDone()))
            || ((userSpaceSource != NULL) && (!userSpaceSource->isDone())) || (!primarySource->isDone())) {
        sem_wait(&senderSem);

        externalSource->write(sender.get());
        if (midgardHwSource != NULL) {
            midgardHwSource->write(sender.get());
        }
        if (userSpaceSource != NULL) {
            userSpaceSource->write(sender.get());
        }
        primarySource->write(sender.get());
    }

    // flush one more time to ensure any slop is cleared up
    {
        externalSource->write(sender.get());
        if (midgardHwSource != NULL) {
            midgardHwSource->write(sender.get());
        }
        if (userSpaceSource != NULL) {
            userSpaceSource->write(sender.get());
        }
        primarySource->write(sender.get());
    }

    // write end-of-capture sequence
    if (!gSessionData.mLocalCapture) {
        sender->writeData(end_sequence, sizeof(end_sequence), RESPONSE_APC_DATA);
    }

    logg.logMessage("Exit sender thread");
}

void Child::watchPidsThreadEntryPoint(std::set<int> & pids)
{
    while (!pids.empty()) {
        if (!sleep(std::chrono::seconds(1))) {
            break;
        }

        auto it = pids.begin();
        while (it != pids.end()) {

            if (kill(*it, 0) < 0) {
                it = pids.erase(it);
            }
            else {
                ++it;
            }
        }
    }
    endSession();
    logg.logMessage("Exit watch pids thread");
}
