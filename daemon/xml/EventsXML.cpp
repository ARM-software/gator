/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "xml/EventsXML.h"
#include "xml/EventsXMLProcessor.h"

#include "CapturedXML.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"
#include "xml/PmuXML.h"
#include "Driver.h"
#include "lib/File.h"

namespace events_xml
{

    std::unique_ptr<mxml_node_t, void (*)(mxml_node_t *)> getTree(lib::Span<const GatorCpu> clusters)
    {
#include "events_xml.h" // defines and initializes char events_xml[] and int events_xml_len
        mxml_unique_ptr mainXml = makeMxmlUniquePtr(nullptr);

        // Avoid unused variable warning
        (void) events_xml_len;

        // Load the provided or default events xml
        if (gSessionData.mEventsXMLPath) {
            std::unique_ptr<FILE, int(*)(FILE*)> fl { lib::fopen_cloexec(gSessionData.mEventsXMLPath, "r"), fclose };
            if (fl != nullptr) {
                mainXml = makeMxmlUniquePtr(mxmlLoadFile(nullptr, fl.get(), MXML_NO_CALLBACK));
                if (mainXml == nullptr) {
                    logg.logError("Unable to parse %s", gSessionData.mEventsXMLPath);
                    handleException();
                }
            }
        }
        if (mainXml == nullptr) {
            logg.logMessage("Unable to locate events.xml, using default");
            mainXml = makeMxmlUniquePtr(mxmlLoadString(nullptr, reinterpret_cast<const char *>(events_xml), MXML_NO_CALLBACK));
        }

        // Append additional events XML
        if (gSessionData.mEventsXMLAppend) {
            std::unique_ptr<FILE, int(*)(FILE*)> fl { lib::fopen_cloexec(gSessionData.mEventsXMLAppend, "r"), fclose };
            if (fl == nullptr) {
                logg.logError("Unable to open additional events XML %s", gSessionData.mEventsXMLAppend);
                handleException();
            }

            mxml_unique_ptr appendXml = makeMxmlUniquePtr(mxmlLoadFile(nullptr, fl.get(), MXML_NO_CALLBACK));
            if (appendXml == nullptr) {
                logg.logError("Unable to parse %s", gSessionData.mEventsXMLAppend);
                handleException();
            }

            // do the merge
            mergeTrees(mainXml.get(), std::move(appendXml));
        }

        // inject addtional counter sets
        processClusters(mainXml.get(), clusters);

        return mainXml;
    }

    std::unique_ptr<char, void (*)(void*)> getXML(lib::Span<const Driver * const > drivers,
                                                  lib::Span<const GatorCpu> clusters)
    {
        const auto xml = getTree(clusters);

        // Add dynamic events from the drivers
        mxml_node_t * events = getEventsElement(xml.get());
        if (events == nullptr) {
            logg.logError("Unable to find <events> node in the events.xml, please ensure the first two lines of events XML are:\n"
                          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                          "<events>");
            handleException();
        }
        for (const Driver * driver : drivers) {
            driver->writeEvents(events);
        }

        return {mxmlSaveAllocString(xml.get(), mxmlWhitespaceCB), &free};
    }

    void write(const char *path, lib::Span<const Driver * const > drivers,
               lib::Span<const GatorCpu> clusters)
    {
        char file[PATH_MAX];

        // Set full path
        snprintf(file, PATH_MAX, "%s/events.xml", path);

        if (writeToDisk(file, getXML(drivers, clusters).get()) < 0) {
            logg.logError("Error writing %s\nPlease verify the path.", file);
            handleException();
        }
    }
}
