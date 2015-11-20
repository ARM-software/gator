/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef MIDGARDDRIVER_H
#define MIDGARDDRIVER_H

#include "Driver.h"

class MidgardDriver : public SimpleDriver {
	typedef SimpleDriver super;

public:
	MidgardDriver();
	~MidgardDriver();

	bool claimCounter(const Counter &counter) const;
	void resetCounters();
	void setupCounter(Counter &counter);

	bool start(const int midgardUds);

private:
	void query() const;

	mutable bool mQueried;

	// Intentionally unimplemented
	MidgardDriver(const MidgardDriver &);
	MidgardDriver &operator=(const MidgardDriver &);
};

#endif // MIDGARDDRIVER_H
