/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef EXTERNALDRIVER_H
#define EXTERNALDRIVER_H

#include "Driver.h"

class ExternalDriver : public SimpleDriver {
public:
	ExternalDriver();

	bool claimCounter(const Counter &counter) const;
	void resetCounters();
	void setupCounter(Counter &counter);

	void start();

	void disconnect();

private:
	typedef SimpleDriver super;

	bool connect() const;
	void query() const;

	mutable int mUds;
	mutable bool mQueried;
	bool mStarted;

	// Intentionally unimplemented
	ExternalDriver(const ExternalDriver &);
	ExternalDriver &operator=(const ExternalDriver &);
};

#endif // EXTERNALDRIVER_H
