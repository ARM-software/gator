/* Copyright (C) 2013-2025 by Arm Limited. All rights reserved. */

#include "xml/EventsXML.h"

#include "Driver.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"
#include "lib/File.h"
#include "lib/Span.h"
#include "lib/String.h"
#include "mali_userspace/MaliDevice.h"
#include "xml/EventsXMLProcessor.h"
#include "xml/MxmlUtils.h"
#include "xml/PmuXML.h"

#include <cstdlib>
#include <memory>
#include <string_view>
#include <utility>

#include <mxml.h>

namespace events_xml {
    namespace {
#include "events_xml.h"
    }

    std::unique_ptr<mxml_node_t, void (*)(mxml_node_t *)> getStaticTree(lib::Span<const GatorCpu> clusters,
                                                                        lib::Span<const UncorePmu> uncores)
    {
        mxml_unique_ptr mainXml = makeMxmlUniquePtr(nullptr);

        // Load the provided or default events xml
        if (gSessionData.mEventsXMLPath != nullptr) {
            std::unique_ptr<FILE, int (*)(FILE *)> fl {lib::fopen_cloexec(gSessionData.mEventsXMLPath, "r"), fclose};
            if (fl != nullptr) {
                mainXml = makeMxmlUniquePtr(mxmlLoadFile(nullptr, fl.get(), MXML_NO_CALLBACK));
                if (mainXml == nullptr) {
                    LOG_ERROR("Unable to parse %s", gSessionData.mEventsXMLPath);
                    handleException();
                }
            }
        }
        if (mainXml == nullptr) {
            LOG_DEBUG("Unable to locate events.xml, using default");
            mainXml = makeMxmlUniquePtr(mxmlLoadString(nullptr, DEFAULT_EVENTS_XML.data(), MXML_NO_CALLBACK));
        }

        // Append additional events XML
        if (gSessionData.mEventsXMLAppend != nullptr) {
            std::unique_ptr<FILE, int (*)(FILE *)> fl {lib::fopen_cloexec(gSessionData.mEventsXMLAppend, "r"), fclose};
            if (fl == nullptr) {
                LOG_ERROR("Unable to open additional events XML %s", gSessionData.mEventsXMLAppend);
                handleException();
            }

            mxml_unique_ptr appendXml = makeMxmlUniquePtr(mxmlLoadFile(nullptr, fl.get(), MXML_NO_CALLBACK));
            if (appendXml == nullptr) {
                LOG_ERROR("Unable to parse %s", gSessionData.mEventsXMLAppend);
                handleException();
            }

            // do the merge
            mergeTrees(mainXml.get(), std::move(appendXml));
        }

        // inject additional counter sets
        processClusters(mainXml.get(), clusters, uncores);

        return mainXml;
    }

    std::unique_ptr<mxml_node_t, void (*)(mxml_node_t *)> getDynamicTree(lib::Span<const Driver * const> drivers,
                                                                         lib::Span<const GatorCpu> clusters,
                                                                         lib::Span<const UncorePmu> uncores)
    {
        auto xml = getStaticTree(clusters, uncores);
        // Add dynamic events from the drivers
        mxml_node_t * events = getEventsElement(xml.get());
        if (events == nullptr) {
            LOG_ERROR(
                "Unable to find <events> node in the events.xml, please ensure the first two lines of events XML are:\n"
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<events>");
            handleException();
        }

        for (const Driver * driver : drivers) {
            driver->writeEvents(events);
        }

        writeDDKToGPUTimelineEvents(events);

        return xml;
    }

    std::unique_ptr<char, void (*)(void *)> getDynamicXML(lib::Span<const Driver * const> drivers,
                                                          lib::Span<const GatorCpu> clusters,
                                                          lib::Span<const UncorePmu> uncores)
    {
        const auto xml = getDynamicTree(drivers, clusters, uncores);
        return {mxmlSaveAllocString(xml.get(), mxmlWhitespaceCB), &free};
    }

    void write(const char * path,
               lib::Span<const Driver * const> drivers,
               lib::Span<const GatorCpu> clusters,
               lib::Span<const UncorePmu> uncores)
    {
        // Set full path
        lib::printf_str_t<PATH_MAX> file {"%s/events.xml", path};

        if (writeToDisk(file, getDynamicXML(drivers, clusters, uncores).get()) < 0) {
            LOG_ERROR("Error writing %s\nPlease verify the path.", file.c_str());
            handleException();
        }
    }

    void writeDDKToGPUTimelineEvents(mxml_node_t * events)
    {

        // Add DDK version to GPU Timeline tag if it exists
        mxml_node_t * maliTimelineCategory =
            mxmlFindElement(events, events, "category", "name", "Mali Timeline", MXML_DESCEND);

        LOG_DEBUG("Looking for MaliTimeline category in events XML: %s",
                  maliTimelineCategory != nullptr ? "found" : "not found");

        if (maliTimelineCategory != nullptr) {
            // Grab "MaliTimeline_Perfetto" event
            mxml_node_t * perfettoEvent = mxmlFindElement(maliTimelineCategory,
                                                          maliTimelineCategory,
                                                          "event",
                                                          "counter",
                                                          "MaliTimeline_Perfetto",
                                                          MXML_DESCEND);

            LOG_DEBUG("Looking for MaliTimeline_Perfetto event in events XML: %s",
                      perfettoEvent != nullptr ? "found" : "not found");

            if (perfettoEvent != nullptr) {
                int ddkVersion = mali_userspace::MaliDevice::get_mali_ddk_version_from_device();

                if (ddkVersion != -1) { // -1 indicates no ddk version found.
                    std::ostringstream oss;
                    oss << ddkVersion;
                    mxmlElementSetAttr(perfettoEvent, "ddk_version", oss.str().c_str());

                    LOG_DEBUG("Set MaliTimeline_Perfetto event ddk_version attribute to %s", oss.str().c_str());
                }
                else {
                    LOG_DEBUG(
                        "Mali DDK version not found, not setting ddk_version attribute on MaliTimeline_Perfetto event");
                }
            }
        }
    }
}
