/**
 * Copyright (C) Arm Limited 2013-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "EventsXML.h"

#include "CapturedXML.h"
#include "Logging.h"
#include "OlyUtility.h"
#include "SessionData.h"
#include "PmuXML.h"
#include "Driver.h"
#include "lib/File.h"

static const char TAG_EVENTS[] = "events";
static const char TAG_CATEGORY[] = "category";
static const char TAG_COUNTER_SET[] = "counter_set";
static const char TAG_EVENT[] = "event";

static const char ATTR_COUNTER[] = "counter";
static const char ATTR_TITLE[] = "title";
static const char ATTR_NAME[] = "name";

static const char CLUSTER_VAR[] = "${cluster}";

class XMLList
{
public:
    XMLList(XMLList * const prev, mxml_node_t * const node)
            : mPrev(prev),
              mNode(node)
    {
    }

    XMLList *getPrev()
    {
        return mPrev;
    }
    mxml_node_t *getNode() const
    {
        return mNode;
    }
    void setNode(mxml_node_t * const node)
    {
        mNode = node;
    }

    static void free(XMLList *list)
    {
        while (list != NULL) {
            XMLList *prev = list->getPrev();
            delete list;
            list = prev;
        }
    }

private:
    XMLList * const mPrev;
    mxml_node_t *mNode;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(XMLList);
};

namespace events_xml {

std::unique_ptr<mxml_node_t, void (*)(mxml_node_t *)> getTree(lib::Span<const GatorCpu> clusters)
{
#include "events_xml.h" // defines and initializes char events_xml[] and int events_xml_len
    char path[PATH_MAX];
    mxml_node_t *xml = NULL;
    FILE *fl;

    // Avoid unused variable warning
    (void) events_xml_len;

    // Load the provided or default events xml
    if (gSessionData.mEventsXMLPath) {
        strncpy(path, gSessionData.mEventsXMLPath, PATH_MAX);
        fl = lib::fopen_cloexec(path, "r");
        if (fl) {
            xml = mxmlLoadFile(NULL, fl, MXML_NO_CALLBACK);
            if (xml == NULL) {
                logg.logError("Unable to parse %s", gSessionData.mEventsXMLPath);
                handleException();
            }
            fclose(fl);
        }
    }
    if (xml == NULL) {
        logg.logMessage("Unable to locate events.xml, using default");
        xml = mxmlLoadString(NULL, reinterpret_cast<const char *>(events_xml), MXML_NO_CALLBACK);
    }

    // Append additional events XML
    if (gSessionData.mEventsXMLAppend) {
        fl = lib::fopen_cloexec(gSessionData.mEventsXMLAppend, "r");
        if (fl == NULL) {
            logg.logError("Unable to open additional events XML %s", gSessionData.mEventsXMLAppend);
            handleException();
        }
        mxml_node_t *append = mxmlLoadFile(NULL, fl, MXML_NO_CALLBACK);
        if (append == NULL) {
            logg.logError("Unable to parse %s", gSessionData.mEventsXMLAppend);
            handleException();
        }
        fclose(fl);

        mxml_node_t *events = mxmlFindElement(xml, xml, TAG_EVENTS, NULL, NULL, MXML_DESCEND);
        if (!events) {
            logg.logError("Unable to find <events> node in the events.xml, please ensure the first two lines of events XML starts with:\n"
                    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                    "<events>");
            handleException();
        }

        XMLList *categoryList = NULL;
        XMLList *eventList = NULL;
        XMLList *counterSetList = NULL;
        {
            // Make list of all categories in xml
            mxml_node_t *node = xml;
            while (true) {
                node = mxmlFindElement(node, xml, TAG_CATEGORY, NULL, NULL, MXML_DESCEND);
                if (node == NULL) {
                    break;
                }
                categoryList = new XMLList(categoryList, node);
            }

            // Make list of all events in xml
            node = xml;
            while (true) {
                node = mxmlFindElement(node, xml, TAG_EVENT, NULL, NULL, MXML_DESCEND);
                if (node == NULL) {
                    break;
                }
                eventList = new XMLList(eventList, node);
            }

            // Make list of all counter_sets in xml
            node = xml;
            while (true) {
                node = mxmlFindElement(node, xml, TAG_COUNTER_SET, NULL, NULL, MXML_DESCEND);
                if (node == NULL) {
                    break;
                }
                counterSetList = new XMLList(counterSetList, node);
            }
        }

        // Handle counter_sets
        for (mxml_node_t *node =
                strcmp(mxmlGetElement(append), TAG_COUNTER_SET) == 0 ?
                        append : mxmlFindElement(append, append, TAG_COUNTER_SET, NULL, NULL, MXML_DESCEND),
                *next = mxmlFindElement(node, append, TAG_COUNTER_SET, NULL, NULL, MXML_DESCEND); node != NULL;
                node = next, next = mxmlFindElement(node, append, TAG_COUNTER_SET, NULL, NULL, MXML_DESCEND)) {

            const char * const name = mxmlElementGetAttr(node, ATTR_NAME);
            if (name == NULL) {
                logg.logError("Not all event XML counter_sets have the required name attribute");
                handleException();
            }

            // Replace any duplicate counter_sets
            bool replaced = false;
            for (XMLList *counterSet = counterSetList; counterSet != NULL; counterSet = counterSet->getPrev()) {
                const char * const name2 = mxmlElementGetAttr(counterSet->getNode(), ATTR_NAME);
                if (name2 == NULL) {
                    logg.logError("Not all event XML nodes have the required title and name and parent name attributes");
                    handleException();
                }

                if (strcmp(name, name2) == 0) {
                    logg.logMessage("Replacing counter %s", name);
                    mxml_node_t *parent = mxmlGetParent(counterSet->getNode());
                    mxmlDelete(counterSet->getNode());
                    mxmlAdd(parent, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, node);
                    counterSet->setNode(node);
                    replaced = true;
                    break;
                }
            }

            if (replaced) {
                continue;
            }

            // Add new counter_sets
            logg.logMessage("Appending counter_set %s", name);
            mxmlAdd(events, MXML_ADD_AFTER, mxmlGetLastChild(events), node);
        }

        // Handle events
        for (mxml_node_t *node = mxmlFindElement(append, append, TAG_EVENT, NULL, NULL, MXML_DESCEND),
                *next = mxmlFindElement(node, append, TAG_EVENT, NULL, NULL, MXML_DESCEND); node != NULL;
                node = next, next = mxmlFindElement(node, append, TAG_EVENT, NULL, NULL, MXML_DESCEND)) {
            const char * const category = mxmlElementGetAttr(mxmlGetParent(node), ATTR_NAME);
            const char * const title = mxmlElementGetAttr(node, ATTR_TITLE);
            const char * const name = mxmlElementGetAttr(node, ATTR_NAME);
            if (category == NULL || title == NULL || name == NULL) {
                logg.logError("Not all event XML nodes have the required title and name and parent name attributes");
                handleException();
            }

            // Replace any duplicate events
            for (XMLList *event = eventList; event != NULL; event = event->getPrev()) {
                const char * const category2 = mxmlElementGetAttr(mxmlGetParent(event->getNode()), ATTR_NAME);
                const char * const title2 = mxmlElementGetAttr(event->getNode(), ATTR_TITLE);
                const char * const name2 = mxmlElementGetAttr(event->getNode(), ATTR_NAME);
                if (category2 == NULL || title2 == NULL || name2 == NULL) {
                    logg.logError("Not all event XML nodes have the required title and name and parent name attributes");
                    handleException();
                }

                if (strcmp(category, category2) == 0 && strcmp(title, title2) == 0 && strcmp(name, name2) == 0) {
                    logg.logMessage("Replacing counter %s %s: %s", category, title, name);
                    mxml_node_t *parent = mxmlGetParent(event->getNode());
                    mxmlDelete(event->getNode());
                    mxmlAdd(parent, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, node);
                    event->setNode(node);
                    break;
                }
            }
        }

        // Handle categories
        for (mxml_node_t *node =
                strcmp(mxmlGetElement(append), TAG_CATEGORY) == 0 ?
                        append : mxmlFindElement(append, append, TAG_CATEGORY, NULL, NULL, MXML_DESCEND),
                *next = mxmlFindElement(node, append, TAG_CATEGORY, NULL, NULL, MXML_DESCEND); node != NULL;
                node = next, next = mxmlFindElement(node, append, TAG_CATEGORY, NULL, NULL, MXML_DESCEND)) {
            // After replacing duplicate events, a category may be empty
            if (mxmlGetFirstChild(node) == NULL) {
                continue;
            }

            const char * const name = mxmlElementGetAttr(node, ATTR_NAME);
            if (name == NULL) {
                logg.logError("Not all event XML category nodes have the required name attribute");
                handleException();
            }

            // Merge identically named categories
            bool merged = false;
            for (XMLList *category = categoryList; category != NULL; category = category->getPrev()) {
                const char * const name2 = mxmlElementGetAttr(category->getNode(), ATTR_NAME);
                if (name2 == NULL) {
                    logg.logError("Not all event XML category nodes have the required name attribute");
                    handleException();
                }

                if (strcmp(name, name2) == 0) {
                    logg.logMessage("Merging category %s", name);
                    while (true) {
                        mxml_node_t *child = mxmlGetFirstChild(node);
                        if (child == NULL) {
                            break;
                        }
                        mxmlAdd(category->getNode(), MXML_ADD_AFTER, mxmlGetLastChild(category->getNode()), child);
                    }
                    merged = true;
                    break;
                }
            }

            if (merged) {
                continue;
            }

            // Add new categories
            logg.logMessage("Appending category %s", name);
            mxmlAdd(events, MXML_ADD_AFTER, mxmlGetLastChild(events), node);
        }

        XMLList::free(eventList);
        XMLList::free(categoryList);
        XMLList::free(counterSetList);

        mxmlDelete(append);
    }

    // Resolve ${cluster}
    for (mxml_node_t *node = mxmlFindElement(xml, xml, TAG_EVENT, NULL, NULL, MXML_DESCEND), *next = mxmlFindElement(
            node, xml, TAG_EVENT, NULL, NULL, MXML_DESCEND); node != NULL;
            node = next, next = mxmlFindElement(node, xml, TAG_EVENT, NULL, NULL, MXML_DESCEND)) {
        const char *counter = mxmlElementGetAttr(node, ATTR_COUNTER);
        if (counter != NULL && strncmp(counter, CLUSTER_VAR, sizeof(CLUSTER_VAR) - 1) == 0) {
            for (const GatorCpu & cluster : clusters) {
                mxml_node_t *n = mxmlNewElement(mxmlGetParent(node), TAG_EVENT);
                copyMxmlElementAttrs(n, node);
                char buf[1 << 7];
                snprintf(buf, sizeof(buf), "%s%s", cluster.getPmncName(),
                         counter + sizeof(CLUSTER_VAR) - 1);
                mxmlElementSetAttr(n, ATTR_COUNTER, buf);
            }
            mxmlDelete(node);
        }
    }

    return {xml, &mxmlDelete};
}

std::unique_ptr<char, void(*)(void*)> getXML(lib::Span<const Driver * const> drivers, lib::Span<const GatorCpu> clusters)
{
    const auto xml = getTree(clusters);

    // Add dynamic events from the drivers
    mxml_node_t *events = mxmlFindElement(xml.get(), xml.get(), TAG_EVENTS, NULL, NULL, MXML_DESCEND);
    if (!events) {
        logg.logError("Unable to find <events> node in the events.xml, please ensure the first two lines of events XML are:\n"
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<events>");
        handleException();
    }
    for (const Driver *driver : drivers) {
        driver->writeEvents(events);
    }

    return {mxmlSaveAllocString(xml.get(), mxmlWhitespaceCB), &free};
}

void write(const char *path, lib::Span<const Driver * const> drivers, lib::Span<const GatorCpu> clusters)
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
