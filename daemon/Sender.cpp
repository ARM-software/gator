/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

typedef unsigned long long uint64_t;
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include "Sender.h"
#include "Logging.h"
#include "SessionData.h"

extern void handleException();

Sender::Sender(OlySocket* socket) {
	dataFile = NULL;

	// Set up the socket connection
	if (socket) {
		char streamline[64] = {0};
		dataSocket = socket;

		// Receive magic sequence - can wait forever
		// Streamline will send data prior to the magic sequence for legacy support, which should be ignored for v4+
		while (strcmp("STREAMLINE", streamline) != 0) {
			if (dataSocket->receiveString(streamline, sizeof(streamline)) == -1) {
				logg->logError(__FILE__, __LINE__, "Socket disconnected");
				handleException();
			}
		}

		// Send magic sequence - must be done first, afterwhich error messages can be sent
		char magic[] = {'G', 'A', 'T', 'O', 'R', '\n'};
		dataSocket->send(magic, sizeof(magic));

		gSessionData.mWaitingOnCommand = true;
		logg->logMessage("Completed magic sequence");
	}
}

Sender::~Sender() {
	delete dataSocket;
	dataSocket = NULL;
	if (dataFile) {
		fclose(dataFile);
	}
}

void Sender::createDataFile(char* apcDir) {
	if (apcDir == NULL)
		return;

	dataFileName = (char*)malloc(strlen(apcDir) + 12);
	sprintf(dataFileName, "%s/0000000000", apcDir);
	dataFile = fopen(dataFileName, "wb");
	if (!dataFile) {
		logg->logError(__FILE__, __LINE__, "Failed to open binary file: %s", dataFileName);
		handleException();
	}
}

void Sender::writeData(const char* data, int length, int type) {
	if (length < 0 || (data == NULL && length > 0)) {
		return;
	}

	// Send data over the socket connection
	if (dataSocket) {
		// Start alarm
		alarm(5);

		// Send data over the socket, sending the type and size first
		logg->logMessage("Sending data with length %d", length);
		dataSocket->send((char*)&type, 1);
		dataSocket->send((char*)&length, sizeof(length));
		dataSocket->send((char*)data, length);

		// Stop alarm
		alarm(0);
	}

	// Write data to disk as long as it is not meta data
	if (dataFile && type == RESPONSE_APC_DATA) {
		logg->logMessage("Writing data with length %d", length);
		// Send data to the data file, storing the size first
		if ((fwrite((char*)&length, 1, sizeof(length), dataFile) != sizeof(length)) || (fwrite(data, 1, length, dataFile) != (unsigned int)length)) {
			logg->logError(__FILE__, __LINE__, "Failed writing binary file %s", dataFileName);
			handleException();
		}
	}
}
