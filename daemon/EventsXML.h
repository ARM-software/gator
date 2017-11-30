/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef EVENTS_XML
#define EVENTS_XML

#include "ClassBoilerPlate.h"
#include "mxml/mxml.h"

class EventsXML
{
public:
    EventsXML()
    {
    }

    mxml_node_t *getTree();
    char *getXML();
    void write(const char* path);

private:

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(EventsXML);
};

#endif // EVENTS_XML
