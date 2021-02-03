/* Copyright (C) 2013-2020 by Arm Limited. All rights reserved. */

#include "xml/EventsXML.h"

#include "CapturedXML.h"
#include "Driver.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"
#include "lib/File.h"
#include "xml/EventsXMLProcessor.h"
#include "xml/PmuXML.h"

namespace events_xml {

    std::unique_ptr<mxml_node_t, void (*)(mxml_node_t *)> getStaticTree(lib::Span<const GatorCpu> clusters)
    {
#include "events_xml.h" // defines and initializes char events_xml[] and int events_xml_len
        mxml_unique_ptr mainXml = makeMxmlUniquePtr(nullptr);

        // Avoid unused variable warning
        (void) events_xml_len;

        // Load the provided or default events xml
        if (gSessionData.mEventsXMLPath != nullptr) {
            std::unique_ptr<FILE, int (*)(FILE *)> fl {lib::fopen_cloexec(gSessionData.mEventsXMLPath, "r"), fclose};
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
            mainXml = makeMxmlUniquePtr(
                mxmlLoadString(nullptr, reinterpret_cast<const char *>(events_xml), MXML_NO_CALLBACK));
        }

        // Append additional events XML
        if (gSessionData.mEventsXMLAppend != nullptr) {
            std::unique_ptr<FILE, int (*)(FILE *)> fl {lib::fopen_cloexec(gSessionData.mEventsXMLAppend, "r"), fclose};
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

        // inject additional counter sets
        processClusters(mainXml.get(), clusters);
        return mainXml;
    }

    static std::unique_ptr<mxml_node_t, void (*)(mxml_node_t *)> getDynamicTree(lib::Span<const Driver * const> drivers,
                                                                                lib::Span<const GatorCpu> clusters)
    {
        auto xml = getStaticTree(clusters);
        // Add dynamic events from the drivers
        mxml_node_t * events = getEventsElement(xml.get());
        if (events == nullptr) {
            logg.logError(
                "Unable to find <events> node in the events.xml, please ensure the first two lines of events XML are:\n"
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<events>");
            handleException();
        }
        for (const Driver * driver : drivers) {
            driver->writeEvents(events);
        }

        return xml;
    }

    std::unique_ptr<char, void (*)(void *)> getDynamicXML(lib::Span<const Driver * const> drivers,
                                                          lib::Span<const GatorCpu> clusters)
    {
        const auto xml = getDynamicTree(drivers, clusters);
        return {mxmlSaveAllocString(xml.get(), mxmlWhitespaceCB), &free};
    }

    std::map<std::string, EventCode> getCounterToEventMap(lib::Span<const Driver * const> drivers,
                                                          lib::Span<const GatorCpu> clusters)
    {
        std::map<std::string, EventCode> counterToEventMap {};

        auto xml = events_xml::getDynamicTree(drivers, clusters);

        // build map of counter->event
        mxml_node_t * node = xml.get();
        while (true) {
            node = mxmlFindElement(node, xml.get(), "event", nullptr, nullptr, MXML_DESCEND);
            if (node == nullptr) {
                break;
            }
            const char * counter = mxmlElementGetAttr(node, "counter");
            const char * event = mxmlElementGetAttr(node, "event");
            if (counter == nullptr) {
                continue;
            }

            if (event != nullptr) {
                const auto eventNo = strtoull(event, nullptr, 0);
                counterToEventMap[counter] = EventCode(eventNo);
            }
            else {
                counterToEventMap[counter] = EventCode();
            }
        }
        return counterToEventMap;
    }

    void write(const char * path, lib::Span<const Driver * const> drivers, lib::Span<const GatorCpu> clusters)
    {
        char file[PATH_MAX];

        // Set full path
        snprintf(file, PATH_MAX, "%s/events.xml", path);

        if (writeToDisk(file, getDynamicXML(drivers, clusters).get()) < 0) {
            logg.logError("Error writing %s\nPlease verify the path.", file);
            handleException();
        }
    }
}
