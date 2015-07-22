/**
 * Copyright (C) ARM Limited 2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ATRACEDRIVER_H
#define ATRACEDRIVER_H

#include "mxml/mxml.h"

#include "Driver.h"

class AtraceDriver : public SimpleDriver {
public:
	AtraceDriver();
	~AtraceDriver();

	void readEvents(mxml_node_t *const xml);

	void start();
	void stop();

	bool isSupported() const { return mSupported; }

private:
	void setAtrace(const int flags);

	bool mSupported;
	char mNotifyPath[256];

	// Intentionally unimplemented
	AtraceDriver(const AtraceDriver &);
	AtraceDriver &operator=(const AtraceDriver &);
};

#endif // ATRACEDRIVER_H
