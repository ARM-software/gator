/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include "CounterXML.h"

#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "PrimarySourceProvider.h"
#include "SessionData.h"
#include "Logging.h"
#include "OlyUtility.h"

CounterXML::CounterXML()
{
}

CounterXML::~CounterXML()
{
}

mxml_node_t* CounterXML::getTree()
{
    mxml_node_t *xml;
    mxml_node_t *counters;

    xml = mxmlNewXML("1.0");
    counters = mxmlNewElement(xml, "counters");
    int count = 0;
    for (Driver *driver = Driver::getHead(); driver != NULL; driver = driver->getNext()) {
        count += driver->writeCounters(counters);
    }

    if (count == 0) {
        logg.logError("No counters found, this could be because /dev/gator/events can not be read or because perf is not working correctly");
        handleException();
    }

    mxml_node_t *setup = mxmlNewElement(counters, "setup_warnings");
    mxmlNewText(setup, 0, logg.getSetup());

    // always send the cluster information; even on devices where not all the information is available.
    for (int cluster = 0; cluster < gSessionData.mSharedData->mClusterCount; ++cluster) {
        mxml_node_t *node = mxmlNewElement(counters, "cluster");
        mxmlElementSetAttrf(node, "id", "%i", cluster);
        mxmlElementSetAttr(node, "name", gSessionData.mSharedData->mClusters[cluster]->getPmncName());
    }
    for (int cpu = 0; cpu < gSessionData.mCores; ++cpu) {
        if (gSessionData.mSharedData->mClusterIds[cpu] >= 0) {
            mxml_node_t *node = mxmlNewElement(counters, "cpu");
            mxmlElementSetAttrf(node, "id", "%i", cpu);
            mxmlElementSetAttrf(node, "cluster", "%i", gSessionData.mSharedData->mClusterIds[cpu]);
        }
    }
    return xml;
}

char* CounterXML::getXML()
{
    char* xml_string;
    mxml_node_t *xml = getTree();
    xml_string = mxmlSaveAllocString(xml, mxmlWhitespaceCB);
    mxmlDelete(xml);
    return xml_string;
}

void CounterXML::write(const char* path)
{
    char file[PATH_MAX];

    // Set full path
    snprintf(file, PATH_MAX, "%s/counters.xml", path);

    char* xml = getXML();
    if (writeToDisk(file, xml) < 0) {
        logg.logError("Error writing %s\nPlease verify the path.", file);
        handleException();
    }

    free(xml);
}
