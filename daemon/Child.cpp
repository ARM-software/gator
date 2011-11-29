/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <unistd.h>
#include <sys/prctl.h>
#include "Logging.h"
#include "CapturedXML.h"
#include "SessionData.h"
#include "Child.h"
#include "LocalCapture.h"
#include "Collector.h"
#include "Sender.h"
#include "OlyUtility.h"
#include "StreamlineSetup.h"

static sem_t haltPipeline, senderThreadStarted, startProfile; // Shared by Child and spawned threads
static Fifo* collectorFifo = NULL;   // Shared by Child.cpp and spawned threads
static Sender* sender = NULL;        // Shared by Child.cpp and spawned threads
Collector* collector = NULL;         // shared by Child.cpp and ConfigurationXML.cpp
Child* child = NULL;                 // shared by Child.cpp and main.cpp

extern void cleanUp();
void handleException() {
	if (child && child->numExceptions++ > 0) {
		// it is possible one of the below functions itself can cause an exception, thus allow only one exception
		logg->logMessage("Received multiple exceptions, terminating the child");
		exit(1);
	}
	fprintf(stderr, "%s", logg->getLastError());

	if (child && child->socket) {
		if (sender) {
			// send the error, regardless of the command sent by Streamline
			sender->writeData(logg->getLastError(), strlen(logg->getLastError()), RESPONSE_ERROR);

			// cannot close the socket before Streamline issues the command, so wait for the command before exiting
			if (gSessionData.mWaitingOnCommand) {
				char discard;
				child->socket->receiveNBytes(&discard, 1);
			}

			// this indirectly calls close socket which will ensure the data has been sent
			delete sender;
		}
	}

	if (gSessionData.mLocalCapture)
		cleanUp();

	exit(1);
}

// CTRL C Signal Handler for child process
void child_handler(int signum) {
	static bool beenHere = false;
	if (beenHere == true) {
		logg->logMessage("Gator is being forced to shut down.");
		exit(1);
	}
	beenHere = true;
	logg->logMessage("Gator is shutting down.");
	if (signum == SIGALRM || !collector) {
		exit(1);
	} else {
		child->endSession();
		alarm(5); // Safety net in case endSession does not complete within 5 seconds
	}
}

void* durationThread(void* pVoid) {
	prctl(PR_SET_NAME, (unsigned int)&"gatord-duration", 0, 0, 0);
	sem_wait(&startProfile);
	if (gSessionData.mSessionIsActive) {
		// Time out after duration seconds
		// Add a second for host-side filtering
		sleep(gSessionData.mDuration + 1);
		if (gSessionData.mSessionIsActive) {
			logg->logMessage("Duration expired.");
			child->endSession();
		}
	}
	logg->logMessage("Exit duration thread");
	return 0;
}

void* stopThread(void* pVoid) {
	int length;
	char type;
	OlySocket* socket = child->socket;

	prctl(PR_SET_NAME, (unsigned int)&"gatord-stopper", 0, 0, 0);
	while (gSessionData.mSessionIsActive) {
		// This thread will stall until the APC_STOP or PING command is received over the socket or the socket is disconnected
		if (socket->receiveNBytes(&type, sizeof(type)) > 0) {
			if ((type != COMMAND_APC_STOP) && (type != COMMAND_PING)) {
				logg->logMessage("INVESTIGATE: Received unknown command type %d", type);
			} else {
				// verify a length of zero
				if (socket->receiveNBytes((char*)&length, sizeof(length)) < 0) {
					break;
				}

				if (length == 0) {
					if (type == COMMAND_APC_STOP) {
						logg->logMessage("Stop command received.");
						child->endSession();
					} else {
						// Ping is used to make sure gator is alive and requires an ACK as the response
						logg->logMessage("Ping command received.");
						sender->writeData(NULL, 0, RESPONSE_ACK);
					}
				} else {
					logg->logMessage("INVESTIGATE: Received stop command but with length = %d", length);
				}
			}
		}
	}

	logg->logMessage("Exit stop thread");
	return 0;
}

void* senderThread(void* pVoid) {
	int length;
	char* data;

	sem_post(&senderThreadStarted);
	prctl(PR_SET_NAME, (unsigned int)&"gatord-sender", 0, 0, 0);
	sem_wait(&haltPipeline);

	do {
		data = collectorFifo->read(&length);
		sender->writeData(data, length, RESPONSE_APC_DATA);
	} while (length > 0);
	logg->logMessage("Exit sender thread");
	return 0;
}

Child::Child(char* path) {
	initialization();
	sessionXMLPath = path;
}

Child::Child(OlySocket* sock, int conn) {
	initialization();
	socket = sock;
	numConnections = conn;
}

Child::~Child() {
}

void Child::initialization() {
	// Set up different handlers for signals
	gSessionData.mSessionIsActive = true;
	signal(SIGINT, child_handler);
	signal(SIGTERM, child_handler);
	signal(SIGABRT, child_handler);
	signal(SIGALRM, child_handler);
	socket = NULL;
	numExceptions = 0;
	numConnections = 0;
	sessionXMLPath = 0;

	// Initialize semaphores
	sem_init(&senderThreadStarted, 0, 0);
	sem_init(&startProfile, 0, 0);
}

void Child::endSession() {
	gSessionData.mSessionIsActive = false;
	collector->stop();
	sem_post(&haltPipeline);
}

void Child::run() {
	char* collectBuffer;
	int bytesCollected = 0;
	LocalCapture* localCapture = NULL;

	prctl(PR_SET_NAME, (unsigned int)&"gatord-child", 0, 0, 0);

	// Instantiate the Sender - must be done first, after which error messages can be sent
	sender = new Sender(socket);

	if (numConnections > 1) {
		logg->logError(__FILE__, __LINE__, "Session already in progress");
		handleException();
	}

	// Set up the driver
	collector = new Collector();

	// Start up and parse session xml
	if (socket) {
		// Respond to Streamline requests
		StreamlineSetup ss(socket);
	} else {
		xmlString = util->readFromDisk(sessionXMLPath);
		gSessionData.mLocalCapture = true;
		if (xmlString == 0) {
			logg->logError(__FILE__, __LINE__, "Unable to read session xml file: %s", sessionXMLPath);
			handleException();
		}
		gSessionData.parseSessionXML(xmlString);
		localCapture = new LocalCapture();
		localCapture->createAPCDirectory(gSessionData.target_path, gSessionData.title);
		localCapture->copyImages(gSessionData.images);
		localCapture->write(xmlString);
		sender->createDataFile(gSessionData.apcDir);
		delete xmlString;
	}

	// Create user-space buffers
	int fifoBufferSize = collector->getBufferSize();
	int numCollectorBuffers = (gSessionData.mTotalBufferSize * 1024 * 1024 + fifoBufferSize - 1) / fifoBufferSize;
	numCollectorBuffers = (numCollectorBuffers < 4) ? 4 : numCollectorBuffers;
	logg->logMessage("Created %d %d-byte collector buffers", numCollectorBuffers, fifoBufferSize);
	collectorFifo = new Fifo(numCollectorBuffers, fifoBufferSize);

	// Get the initial pointer to the collect buffer
	collectBuffer = collectorFifo->start();

	// Sender thread shall be halted until it is signaled for one shot mode
	sem_init(&haltPipeline, 0, gSessionData.mOneShot ? 0 : 2);

	// Create the duration, stop, and sender threads
	bool thread_creation_success = true;
	if (gSessionData.mDuration > 0 && pthread_create(&durationThreadID, NULL, durationThread, NULL))
		thread_creation_success = false;
	else if (socket && pthread_create(&stopThreadID, NULL, stopThread, NULL))
		thread_creation_success = false;
	else if (pthread_create(&senderThreadID, NULL, senderThread, NULL))
		thread_creation_success = false;
	if (!thread_creation_success) {
		logg->logError(__FILE__, __LINE__, "Failed to create gator threads");
		handleException();
	}

	// Wait until thread has started
	sem_wait(&senderThreadStarted);

	// Start profiling
	logg->logMessage("********** Profiling started **********");
	collector->start();
	sem_post(&startProfile);

	// Collect Data
	do {
		// This command will stall until data is received from the driver
		bytesCollected = collector->collect(collectBuffer);

		// In one shot mode, stop collection once all the buffers are filled
		if (gSessionData.mOneShot && gSessionData.mSessionIsActive) {
			// Depth minus 1 because write() has not yet been called
			if ((bytesCollected == -1) || (collectorFifo->numWriteToReadBuffersFilled() == collectorFifo->depth() - 1)) {
				logg->logMessage("One shot");
				endSession();
			}
		}
		collectBuffer = collectorFifo->write(bytesCollected);
	} while (bytesCollected > 0);
	logg->logMessage("Exit collect data loop");

	// Wait for the other threads to exit
	pthread_join(senderThreadID, NULL);

	// Shutting down the connection should break the stop thread which is stalling on the socket recv() function
	if (socket) {
		logg->logMessage("Waiting on stop thread");
		socket->shutdownConnection();
		pthread_join(stopThreadID, NULL);
	}

	// Write the captured xml file
	if (gSessionData.mLocalCapture) {
		CapturedXML capturedXML;
		capturedXML.write(gSessionData.apcDir);
	}

	logg->logMessage("Profiling ended.");

	delete collectorFifo;
	delete sender;
	delete collector;
	delete localCapture;
}
