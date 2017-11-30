/**
 * Copyright (C) Arm Limited 2014-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FSDRIVER_H
#define FSDRIVER_H

#include "ClassBoilerPlate.h"
#include "PolledDriver.h"

class FSDriver : public PolledDriver
{
public:
    FSDriver();
    ~FSDriver();

    void readEvents(mxml_node_t * const xml);

    int writeCounters(mxml_node_t *root) const;

private:

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(FSDriver);
};

#endif // FSDRIVER_H
