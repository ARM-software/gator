/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef	__SENDER_H__
#define	__SENDER_H__

#include <stdio.h>
#include <pthread.h>
#include "OlySocket.h"

enum {
	RESPONSE_END = 0, // unused
	RESPONSE_XML = 1,
	RESPONSE_APC_DATA = 3,
	RESPONSE_ACK = 4,
	RESPONSE_NAK = 5,
	RESPONSE_ERROR = 0xFF
};

class Sender {
public:
	Sender(OlySocket* socket);
	~Sender();
	void writeData(const char* data, int length, int type);
	void createDataFile(char* apcDir);
private:
	OlySocket* dataSocket;
	FILE* dataFile;
	char* dataFileName;
	pthread_mutex_t sendMutex;
};

#endif 	//__SENDER_H__
