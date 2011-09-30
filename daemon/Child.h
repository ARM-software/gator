/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef	__CHILD_H__
#define	__CHILD_H__

#include <pthread.h>
#include "Fifo.h"
#include "OlySocket.h"

class Child {
public:
	Child(char* sessionXMLPath);
	Child(OlySocket* sock, int numConnections);
	~Child();
	void run();
	OlySocket *socket;
	void endSession();
	int numExceptions;
private:
	char* xmlString;
	char* sessionXMLPath;
	int numConnections;
	time_t timeStart;
	pthread_t durationThreadID, stopThreadID, senderThreadID;

	void initialization();
};

#endif 	//__CHILD_H__
