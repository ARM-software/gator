/**
 * Copyright (C) ARM Limited 2010-2011. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef	__COLLECTOR_H__
#define	__COLLECTOR_H__

#include <stdio.h>

class Collector {
public:
	Collector();
	~Collector();
	void start();
	void stop();
	int collect(char* buffer);
	void enablePerfCounters();
	int getBufferSize() {return bufferSize;}
private:
	int bufferSize;
	int bufferFD;
	char * buffer;

	void checkVersion();
	void getCoreName();

	char* resolvePath(const char* file);
	int readIntDriver(const char* path, int* value);
	int writeDriver(const char* path, int value);
	int writeDriver(const char* path, const char* data);
	int writeReadDriver(const char* path, int* value);
};

#endif 	//__COLLECTOR_H__
