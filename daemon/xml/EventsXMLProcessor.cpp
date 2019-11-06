/* Copyright (c) 2019 by Arm Limited. All rights reserved. */

#include "xml/EventsXMLProcessor.h"
#include "xml/PmuXML.h"
#include "lib/Assert.h"
#include "Logging.h"

#include <map>
#include <string>

namespace events_xml
{
    namespace
    {
        static const char TAG_EVENTS[] = "events";
        static const char TAG_CATEGORY[] = "category";
        static const char TAG_COUNTER_SET[] = "counter_set";
        static const char TAG_EVENT[] = "event";

        static const char ATTR_COUNTER[] = "counter";
        static const char ATTR_COUNTER_SET[] = "counter_set";
        static const char ATTR_TITLE[] = "title";
        static const char ATTR_NAME[] = "name";

        static const char CLUSTER_VAR[] = "${cluster}";

        static const auto NOP_ATTR_MODIFICATION_FUNCTION = [](const char*, const char*, const char*, std::string&) -> bool { return false; };

        template<typename T>
        static void addAllIdToCounterSetMappings(lib::Span<const T> pmus,
                                                 std::map<std::string, std::pair<std::string, std::string>> & idToCounterSetAndName)
        {
            for (const T & pmu : pmus) {
                idToCounterSetAndName.emplace(pmu.getId(),
                                              std::pair<std::string, std::string> { pmu.getCounterSet(), pmu.getCoreName() });
            }
        }

        template<typename T>
        static void copyMxmlElementAttrs(mxml_node_t *dest, mxml_node_t *src, T attributeFilter)
        {
            if (dest == nullptr || mxmlGetType(dest) != MXML_ELEMENT || src == nullptr || mxmlGetType(src) != MXML_ELEMENT)
                return;


            const char * elementName = mxmlGetElement(src);

            std::string modifiedValue;
            const int numAttrs = mxmlElementGetAttrCount(src);
            for (int i = 0; i < numAttrs; ++i) {
                const char * name;
                const char * value = mxmlElementGetAttrByIndex(src, i, &name);

                if (!attributeFilter(elementName, name, value, modifiedValue)) {
                    mxmlElementSetAttr(dest, name, value);
                }
                else {
                    mxmlElementSetAttr(dest, name, modifiedValue.c_str());
                }
            }
        }

        template<typename T>
        static void copyMxmlChildElements(mxml_node_t *dest, mxml_node_t *src, T attributeFilter)
        {
            for (mxml_node_t * child = mxmlGetFirstChild(src); child != nullptr;
                    child = mxmlGetNextSibling(child)) {
                const char * childName = mxmlGetElement(child);
                if (childName == nullptr)
                    continue;

                mxml_node_t * newChild = mxmlNewElement(dest, childName);
                copyMxmlElementAttrs(newChild, child, attributeFilter);
                copyMxmlChildElements(newChild, child, attributeFilter);
            }
        }

        template<typename T>
        static void addAdditionalPmusCounterSets(mxml_node_t * xml, lib::Span<const T> clusters)
        {
            // build mapping from cluster id -> counter_set
            std::map<std::string, std::pair<std::string, std::string>> idToCounterSetAndName;
            addAllIdToCounterSetMappings(clusters, idToCounterSetAndName);

            // find all counter_set elements by name
            std::map<std::string, mxml_node_t*> counterSetNodes;
            for (mxml_node_t * node = mxmlFindElement(xml, xml, TAG_COUNTER_SET, nullptr, nullptr, MXML_DESCEND);
                 node != nullptr;
                 node = mxmlFindElement(node, xml, TAG_COUNTER_SET, nullptr, nullptr, MXML_DESCEND))
            {
                const char * const name = mxmlElementGetAttr(node, ATTR_NAME);
                if (name != nullptr) {
                    counterSetNodes.emplace(name, node);
                }
            }

            // find all category elements by name
            std::map<std::string, mxml_node_t*> categoryNodes;
            for (mxml_node_t * node = mxmlFindElement(xml, xml, TAG_CATEGORY, nullptr, nullptr, MXML_DESCEND);
                 node != nullptr;
                 node = mxmlFindElement(node, xml, TAG_CATEGORY, nullptr, nullptr, MXML_DESCEND))
            {
                const char * const name = mxmlElementGetAttr(node, ATTR_COUNTER_SET);
                if (name != nullptr) {
                    categoryNodes.emplace(name, node);
                }
            }

            // resolve counter set copies for PMUs
            for (const auto & pair : idToCounterSetAndName)
            {
                const std::string & id = pair.first;
                const std::pair<std::string, std::string> & counterSetAndCoreName = pair.second;
                const std::string & counterSet = counterSetAndCoreName.first;
                const std::string & coreName = counterSetAndCoreName.second;

                // check for counter set and category
                const std::string counterSetName = counterSet + "_cnt";
                const auto counterSetIt = counterSetNodes.find(counterSetName);
                const auto categoryIt = categoryNodes.find(counterSetName);

                if ((counterSetIt == counterSetNodes.end()) || (categoryIt == categoryNodes.end())) {
                    logg.logError("Missing category or counter set named '%s'", counterSetName.c_str());
                    handleException();
                }

                // no need to duplicate where the id == the counter_set
                if (id == counterSet) {
                    continue;
                }

                const std::string newCounterSetName = id + "_cnt";
                const std::string oldEventPrefix = counterSet + "_";
                const std::string newEventPrefix = id + "_";

                // clone the new counter_set element
                mxml_node_t * const counterSetNode = counterSetIt->second;
                mxml_node_t * newCounterSetNode = mxmlNewElement(mxmlGetParent(counterSetNode),
                                                                 TAG_COUNTER_SET);
                copyMxmlElementAttrs(newCounterSetNode, counterSetNode, NOP_ATTR_MODIFICATION_FUNCTION);
                mxmlElementSetAttr(newCounterSetNode, ATTR_NAME, newCounterSetName.c_str());

                // clone the new category element
                mxml_node_t * const categoryNode = categoryIt->second;
                mxml_node_t * newCategoryNode = mxmlNewElement(mxmlGetParent(categoryNode),
                                                               TAG_CATEGORY);
                copyMxmlElementAttrs(
                        newCategoryNode,
                        categoryNode,
                        [&coreName] (const char * /*elementName*/, const char * attrName, const char * attrValue, std::string & result) -> bool
                        {
                            if (attrValue == nullptr) {
                                return false;
                            }

                            if (strcmp(attrName, ATTR_NAME) != 0) {
                                return false;
                            }

                            // use the PMU's core name instead of the original one
                            result = coreName;
                            return true;
                        });
                copyMxmlChildElements(
                        newCategoryNode,
                        categoryNode,
                        [&oldEventPrefix, &newEventPrefix] (const char * elementName, const char * attrName, const char * attrValue, std::string & result) -> bool
                        {
                            if (attrValue == nullptr) {
                                return false;
                            }

                            if (strcmp(elementName, TAG_EVENT) != 0) {
                                return false;
                            }

                            if (strcmp(attrName, ATTR_COUNTER) != 0) {
                                return false;
                            }

                            if (strstr(attrValue, oldEventPrefix.c_str()) != attrValue) {
                                return false;
                            }

                            // change the counter prefix to match 'id'
                            result = newEventPrefix + (attrValue + oldEventPrefix.length());
                            return true;
                        });

                mxmlElementSetAttr(newCategoryNode, ATTR_COUNTER_SET, newCounterSetName.c_str());
            }
        }
    }

    bool mergeTrees(mxml_node_t * mainXml, mxml_unique_ptr appendXml)
    {
        runtime_assert(mainXml != nullptr, "mainXml must not be nullptr");
        runtime_assert(appendXml != nullptr, "appendXml must not be nullptr");

        mxml_node_t * const eventsNode = mxmlFindElement(mainXml, mainXml, TAG_EVENTS, nullptr, nullptr, MXML_DESCEND);
        if (eventsNode == nullptr) {
            logg.logError("Unable to find <events> node in the events.xml, please ensure the first two lines of events XML starts with:\n"
                          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                          "<events>");
            return false;
        }

        std::vector<mxml_node_t *> categories;
        std::vector<mxml_node_t *> events;
        std::vector<mxml_node_t *> counterSets;

        {
            // Make list of all categories in xml
            for (mxml_node_t * node = mxmlFindElement(mainXml, mainXml, TAG_CATEGORY, nullptr, nullptr, MXML_DESCEND);
                 node != nullptr;
                 node = mxmlFindElement(node, mainXml, TAG_CATEGORY, nullptr, nullptr, MXML_DESCEND))
            {
                categories.emplace_back(node);
            }

            // Make list of all events in xml
            for (mxml_node_t * node = mxmlFindElement(mainXml, mainXml, TAG_EVENT, nullptr, nullptr, MXML_DESCEND);
                 node != nullptr;
                 node = mxmlFindElement(node, mainXml, TAG_EVENT, nullptr, nullptr, MXML_DESCEND))
            {
                events.emplace_back(node);
            }

            // Make list of all counter_sets in xml
            for (mxml_node_t * node = mxmlFindElement(mainXml, mainXml, TAG_COUNTER_SET, nullptr, nullptr, MXML_DESCEND);
                 node != nullptr;
                 node = mxmlFindElement(node, mainXml, TAG_COUNTER_SET, nullptr, nullptr, MXML_DESCEND))
            {
                counterSets.emplace_back(node);
            }
        }

        // Handle counter_sets
        for (mxml_node_t * node = ((strcmp(mxmlGetElement(appendXml.get()), TAG_COUNTER_SET) == 0) ? appendXml.get()
                                                                                                   : mxmlFindElement(appendXml.get(), appendXml.get(), TAG_COUNTER_SET, nullptr, nullptr, MXML_DESCEND)),
                         * next = mxmlFindElement(node, appendXml.get(), TAG_COUNTER_SET, nullptr, nullptr, MXML_DESCEND);
             node != nullptr;
             node = next,
             next = mxmlFindElement(node, appendXml.get(), TAG_COUNTER_SET, nullptr, nullptr, MXML_DESCEND))
        {
            const char * const appendXmlNodeName = mxmlElementGetAttr(node, ATTR_NAME);
            if (appendXmlNodeName == nullptr) {
                logg.logError("Not all event XML counter_sets have the required name attribute");
                return false;
            }

            // Replace any duplicate counter_sets
            bool replaced = false;
            for (auto * counterSet : counterSets)
            {
                const char * const mainXmlNodeName = mxmlElementGetAttr(counterSet, ATTR_NAME);
                if (mainXmlNodeName == nullptr) {
                    logg.logError(
                            "Not all event XML nodes have the required title and name and parent name attributes");
                    return false;
                }

                if (strcmp(appendXmlNodeName, mainXmlNodeName) == 0) {
                    logg.logMessage("Replacing counter %s", appendXmlNodeName);
                    mxml_node_t * const parent = mxmlGetParent(counterSet);
                    mxmlDelete(counterSet);
                    mxmlAdd(parent, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, node);
                    replaced = true;
                    break;
                }
            }

            if (replaced) {
                continue;
            }

            // Add new counter_sets
            logg.logMessage("Appending counter_set %s", appendXmlNodeName);
            mxmlAdd(eventsNode, MXML_ADD_AFTER, mxmlGetLastChild(eventsNode), node);
        }

        // Handle events
        for (mxml_node_t * node = mxmlFindElement(appendXml.get(), appendXml.get(), TAG_EVENT, nullptr, nullptr, MXML_DESCEND),
                         * next = mxmlFindElement(node, appendXml.get(), TAG_EVENT, nullptr, nullptr, MXML_DESCEND);
                node != nullptr;
                node = next,
                next = mxmlFindElement(node, appendXml.get(), TAG_EVENT, nullptr, nullptr, MXML_DESCEND))
        {
            const char * const appendXmlNodeCategory = mxmlElementGetAttr(mxmlGetParent(node), ATTR_NAME);
            const char * const appendXmlNodeTitle = mxmlElementGetAttr(node, ATTR_TITLE);
            const char * const appendXmlNodeName = mxmlElementGetAttr(node, ATTR_NAME);

            if (appendXmlNodeCategory == nullptr || appendXmlNodeTitle == nullptr || appendXmlNodeName == nullptr) {
                logg.logError("Not all event XML nodes have the required title and name and parent name attributes");
                return false;
            }

            // Replace any duplicate events
            for (auto * event : events)
            {
                const char * const mainXmlNodeCategory = mxmlElementGetAttr(mxmlGetParent(event), ATTR_NAME);
                const char * const mainXmlNodeTitle = mxmlElementGetAttr(event, ATTR_TITLE);
                const char * const mainXmlNodeName = mxmlElementGetAttr(event, ATTR_NAME);

                if (mainXmlNodeCategory == nullptr || mainXmlNodeTitle == nullptr || mainXmlNodeName == nullptr) {
                    logg.logError("Not all event XML nodes have the required title and name and parent name attributes");
                    return false;
                }

                if ((strcmp(appendXmlNodeCategory, mainXmlNodeCategory) == 0)
                    && (strcmp(appendXmlNodeTitle, mainXmlNodeTitle) == 0)
                    && (strcmp(appendXmlNodeName, mainXmlNodeName) == 0))
                {
                    logg.logMessage("Replacing counter %s %s: %s", appendXmlNodeCategory, appendXmlNodeTitle, appendXmlNodeName);
                    mxml_node_t * const parent = mxmlGetParent(event);
                    mxmlDelete(event);
                    mxmlAdd(parent, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, node);
                    break;
                }
            }
        }

        // Handle categories
        for (mxml_node_t * node = strcmp(mxmlGetElement(appendXml.get()), TAG_CATEGORY) == 0 ? appendXml.get()
                                                                                             : mxmlFindElement(appendXml.get(), appendXml.get(), TAG_CATEGORY, nullptr, nullptr, MXML_DESCEND),
                         * next = mxmlFindElement(node, appendXml.get(), TAG_CATEGORY, nullptr, nullptr, MXML_DESCEND);
                node != nullptr;
                node = next,
                next = mxmlFindElement(node, appendXml.get(), TAG_CATEGORY, nullptr, nullptr, MXML_DESCEND))
        {
            // After replacing duplicate events, a category may be empty
            if (mxmlGetFirstChild(node) == nullptr) {
                continue;
            }

            const char * const appendXmlNodeName = mxmlElementGetAttr(node, ATTR_NAME);
            if (appendXmlNodeName == nullptr) {
                logg.logError("Not all event XML category nodes have the required name attribute");
                return false;
            }

            // Merge identically named categories
            bool merged = false;
            for (auto * category : categories)
            {
                const char * const mainXmlNodeName = mxmlElementGetAttr(category, ATTR_NAME);
                if (mainXmlNodeName == nullptr) {
                    logg.logError("Not all event XML category nodes have the required name attribute");
                    return false;
                }

                if (strcmp(appendXmlNodeName, mainXmlNodeName) == 0) {
                    logg.logMessage("Merging category %s", appendXmlNodeName);
                    for (mxml_node_t * child = mxmlGetFirstChild(node); child != nullptr; child = mxmlGetFirstChild(node))
                    {
                        mxmlAdd(category, MXML_ADD_AFTER, mxmlGetLastChild(category), child);
                    }
                    merged = true;
                    break;
                }
            }

            if (merged) {
                continue;
            }

            // Add new categories
            logg.logMessage("Appending category %s", appendXmlNodeName);
            mxmlAdd(eventsNode, MXML_ADD_AFTER, mxmlGetLastChild(eventsNode), node);
        }

        return true;
    }

    void processClusters(mxml_node_t * xml, lib::Span<const GatorCpu> clusters)
    {
        addAdditionalPmusCounterSets(xml, clusters);

        // Resolve ${cluster}
        for (mxml_node_t * node = mxmlFindElement(xml, xml, TAG_EVENT, nullptr, nullptr, MXML_DESCEND),
                         * next = mxmlFindElement(node, xml, TAG_EVENT, nullptr, nullptr, MXML_DESCEND);
                node != nullptr;
                node = next,
                next = mxmlFindElement(node, xml, TAG_EVENT, nullptr, nullptr, MXML_DESCEND))
        {
            const char * const counter = mxmlElementGetAttr(node, ATTR_COUNTER);

            if ((counter != nullptr) && (strncmp(counter, CLUSTER_VAR, sizeof(CLUSTER_VAR) - 1) == 0))
            {
                for (const GatorCpu & cluster : clusters) {
                    mxml_node_t *n = mxmlNewElement(mxmlGetParent(node), TAG_EVENT);
                    copyMxmlElementAttrs(n, node, NOP_ATTR_MODIFICATION_FUNCTION);
                    char buf[1 << 7];
                    snprintf(buf, sizeof(buf), "%s%s", cluster.getId(), counter + sizeof(CLUSTER_VAR) - 1);
                    mxmlElementSetAttr(n, ATTR_COUNTER, buf);
                }
                mxmlDelete(node);
            }
        }
    }

    mxml_node_t * getEventsElement(mxml_node_t * xml)
    {
        return mxmlFindElement(xml, xml, TAG_EVENTS, nullptr, nullptr, MXML_DESCEND);
    }
}

