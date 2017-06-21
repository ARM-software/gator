/**
 * Copyright (C) ARM Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "Child.h"

#include "lib/Assert.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/prctl.h>

#include "CapturedXML.h"
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
#include "mali_userspace/MaliHwCntrSource.h"

std::atomic<Child *> Child::gSingleton = ATOMIC_VAR_INIT(nullptr);

extern void cleanUp();

void handleException()
{
    Child * const singleton = Child::getSingleton();

    if (singleton != nullptr) {
        singleton->cleanupException();
    }

    exit(1);
}

std::unique_ptr<Child> Child::createLocal(PrimarySourceProvider & primarySourceProvider)
{
    return std::unique_ptr<Child>(new Child(primarySourceProvider));
}

std::unique_ptr<Child> Child::createLive(PrimarySourceProvider & primarySourceProvider, OlySocket & sock)
{
    return std::unique_ptr<Child>(new Child(primarySourceProvider, sock));
}

Child * Child::getSingleton()
{
    return gSingleton.load(std::memory_order_acquire);
}

void Child::signalHandler(int signum)
{
    static bool beenHere = false;

    if (beenHere == true) {
        logg.logMessage("Gator is being forced to shut down.");
        exit(1);
    }
    else {
        beenHere = true;
        logg.logMessage("Gator is shutting down.");

        Child * const singleton = getSingleton();
        if ((signum == SIGALRM) || (singleton == nullptr)) {
            exit(1);
        }
        else {
            singleton->endSession();
            alarm(5); // Safety net in case endSession does not complete within 5 seconds
        }
    }
}

void * Child::durationThreadStaticEntryPoint(void * thisPtr)
{
    Child * const localChild = reinterpret_cast<Child *>(thisPtr);

    runtime_assert(localChild != nullptr, "durationThreadStaticEntryPoint called with thisPtr == nullptr");

    return localChild->durationThreadEntryPoint();
}

void * Child::stopThreadStaticEntryPoint(void * thisPtr)
{
    Child * const localChild = reinterpret_cast<Child *>(thisPtr);

    runtime_assert(localChild != nullptr, "stopThreadStaticEntryPoint called with thisPtr == nullptr");

    return localChild->stopThreadEntryPoint();
}

void * Child::senderThreadStaticEntryPoint(void * thisPtr)
{
    Child * const localChild = reinterpret_cast<Child *>(thisPtr);

    runtime_assert(localChild != nullptr, "senderThreadStaticEntryPoint called with thisPtr == nullptr");

    return localChild->senderThreadEntryPoint();
}

Child::Child(bool local, PrimarySourceProvider & psp, OlySocket * sock)
        : haltPipeline(),
          senderThreadStarted(),
          startProfile(),
          senderSem(),
          primarySource(),
          externalSource(),
          userSpaceSource(),
          midgardHwSource(),
          sender(),
          primarySourceProvider(psp),
          socket(sock),
          numExceptions(0)
{
    // update singleton
    const Child * const prevSingleton = gSingleton.exchange(this, std::memory_order_acq_rel);
    runtime_assert(prevSingleton == nullptr, "Two Child instances active concurrently");

    // Set up different handlers for signals
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGABRT, signalHandler);
    signal(SIGALRM, signalHandler);

    // Initialize semaphores
    sem_init(&senderThreadStarted, 0, 0);
    sem_init(&startProfile, 0, 0);
    sem_init(&senderSem, 0, 0);

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

void Child::run()
{
    std::unique_ptr<LocalCapture> localCapture;
    pthread_t durationThreadID, stopThreadID, senderThreadID;

    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-child"), 0, 0, 0);

    // Disable line wrapping when generating xml files; carriage returns and indentation to be added manually
    mxmlSetWrapMargin(0);

    // Instantiate the Sender - must be done first, after which error messages can be sent
    sender.reset(new Sender(socket));

    // Populate gSessionData with the configuration
    {
        ConfigurationXML configuration;
    }

    // Set up the driver; must be done after gSessionData.mPerfCounterType[] is populated
    primarySource = primarySourceProvider.createPrimarySource(*this, senderSem, startProfile);
    if (primarySource == nullptr) {
        logg.logError("Failed to init primary capture source");
        handleException();
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
        xmlString = readFromDisk(gSessionData.mSessionXMLPath);
        if (xmlString == 0) {
            logg.logError("Unable to read session xml file: %s", gSessionData.mSessionXMLPath);
            handleException();
        }
        gSessionData.parseSessionXML(xmlString);
        localCapture.reset(new LocalCapture());
        localCapture->createAPCDirectory(gSessionData.mTargetPath);
        localCapture->copyImages(gSessionData.mImages);
        localCapture->write(xmlString);
        sender->createDataFile(gSessionData.mAPCDir);
        free(xmlString);
    }

    bool thread_creation_success = true;
    // set up stop thread early, so that ping commands get replied to, even if the
    // setup phase below takes a long time.
    if (socket && pthread_create(&stopThreadID, NULL, stopThreadStaticEntryPoint, this)) {
        thread_creation_success = false;
    }

    if (gSessionData.mPrimarySource->supportsMaliCapture()
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
    if (gSessionData.mDuration > 0 && pthread_create(&durationThreadID, NULL, durationThreadStaticEntryPoint, this)) {
        thread_creation_success = false;
    }
    else if (pthread_create(&senderThreadID, NULL, senderThreadStaticEntryPoint, this)) {
        thread_creation_success = false;
    }

    if (UserSpaceSource::shouldStart()) {
        userSpaceSource.reset(new UserSpaceSource(*this, &senderSem));
        if (!userSpaceSource->prepare()) {
            logg.logError("Unable to prepare userspace source for capture");
            handleException();
        }
        userSpaceSource->start();
    }

    if (gSessionData.mAllowCommands && (gSessionData.mCaptureCommand != NULL)) {
        pthread_t thread;
        if (pthread_create(&thread, NULL, commandThread, NULL)) {
            thread_creation_success = false;
        }
    }

    if (!thread_creation_success) {
        logg.logError("Failed to create gator threads");
        handleException();
    }

    // Wait until thread has started
    sem_wait(&senderThreadStarted);

    // Start profiling
    primarySource->run();

    // Wait for the other threads to exit
    if (userSpaceSource != NULL) {
        userSpaceSource->join();
    }
    if (midgardHwSource != NULL) {
        midgardHwSource->join();
    }
    externalSource->join();
    pthread_join(senderThreadID, NULL);

    // Shutting down the connection should break the stop thread which is stalling on the socket recv() function
    if (socket) {
        logg.logMessage("Waiting on stop thread");
        socket->shutdownConnection();
        pthread_join(stopThreadID, NULL);
    }

    // Write the captured xml file
    if (gSessionData.mLocalCapture) {
        CapturedXML capturedXML;
        capturedXML.write(gSessionData.mAPCDir);
    }

    logg.logMessage("Profiling ended.");

    userSpaceSource.reset();
    midgardHwSource.reset();
    externalSource.reset();
    primarySource.reset();
    sender.reset();
    localCapture.reset();
}

void Child::endSession()
{
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
        _exit(1);
    }

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

void * Child::durationThreadEntryPoint()
{
    prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(&"gatord-duration"), 0, 0, 0);
    sem_wait(&startProfile);
    if (gSessionData.mSessionIsActive) {
        // Time out after duration seconds
        // Add a second for host-side filtering
        sleep(gSessionData.mDuration + 1);
        if (gSessionData.mSessionIsActive) {
            logg.logMessage("Duration expired.");
            endSession();
        }
    }
    logg.logMessage("Exit duration thread");
    return nullptr;
}

void * Child::stopThreadEntryPoint()
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
    return nullptr;
}

void * Child::senderThreadEntryPoint()
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

    // write end-of-capture sequence
    if (!gSessionData.mLocalCapture) {
        sender->writeData(end_sequence, sizeof(end_sequence), RESPONSE_APC_DATA);
    }

    logg.logMessage("Exit sender thread");
    return nullptr;
}
