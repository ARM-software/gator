/**
 * Copyright (C) ARM Limited 2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef TTRACEDRIVER_H
#define TTRACEDRIVER_H

#include "mxml/mxml.h"

#include "Driver.h"

class TtraceDriver : public SimpleDriver {
public:
	TtraceDriver();
	~TtraceDriver();

	void readEvents(mxml_node_t *const xml);

	void start();
	void stop();

	bool isSupported() const { return mSupported; }

private:
	void setTtrace(const int flags);

	bool mSupported;

	// Intentionally unimplemented
	TtraceDriver(const TtraceDriver &);
	TtraceDriver &operator=(const TtraceDriver &);
};

#endif // TTRACEDRIVER_H
