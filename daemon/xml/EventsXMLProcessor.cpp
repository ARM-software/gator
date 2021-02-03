/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#include "xml/EventsXMLProcessor.h"

#include "Logging.h"
#include "lib/Assert.h"
#include "xml/PmuXML.h"

#include <map>
#include <string>
#include <utility>

namespace events_xml {
    namespace {
        const char TAG_EVENTS[] = "events";
        const char TAG_CATEGORY[] = "category";
        const char TAG_COUNTER_SET[] = "counter_set";
        const char TAG_EVENT[] = "event";
        const char TAG_SPE[] = "spe";

        const char ATTR_CLASS[] = "class";
        const char ATTR_COUNT[] = "count";
        const char ATTR_COUNTER[] = "counter";
        const char ATTR_COUNTER_SET[] = "counter_set";
        const char ATTR_DESCRIPTION[] = "description";
        const char ATTR_ID[] = "id";
        const char ATTR_MULTIPLIER[] = "multiplier";
        const char ATTR_NAME[] = "name";
        const char ATTR_TITLE[] = "title";
        const char ATTR_UNITS[] = "units";
        const char ATTR_EVENT[] = "event";

        const char CLUSTER_VAR[] = "${cluster}";

        const std::map<Event::Class, std::string> classToStringMap = {{Event::Class::DELTA, "delta"},
                                                                      {Event::Class::INCIDENT, "incident"},
                                                                      {Event::Class::ABSOLUTE, "absolute"},
                                                                      {Event::Class::ACTIVITY, "activity"}};

        const auto NOP_ATTR_MODIFICATION_FUNCTION =
            [](const char * /*unused*/, const char * /*unused*/, const char * /*unused*/, std::string &
               /*unused*/) -> bool { return false; };

        template<typename T>
        static void addAllIdToCounterSetMappings(
            lib::Span<const T> pmus,
            std::map<std::string, std::pair<std::string, std::string>> & idToCounterSetAndName)
        {
            for (const T & pmu : pmus) {
                idToCounterSetAndName.emplace(
                    pmu.getId(),
                    std::pair<std::string, std::string> {pmu.getCounterSet(), pmu.getCoreName()});
            }
        }

        template<typename T>
        static void copyMxmlElementAttrs(mxml_node_t * dest, mxml_node_t * src, T attributeFilter)
        {
            if (dest == nullptr || mxmlGetType(dest) != MXML_ELEMENT || src == nullptr ||
                mxmlGetType(src) != MXML_ELEMENT) {
                return;
            }

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
        static void copyMxmlChildElements(mxml_node_t * dest, mxml_node_t * src, T attributeFilter)
        {
            for (mxml_node_t * child = mxmlGetFirstChild(src); child != nullptr; child = mxmlGetNextSibling(child)) {
                const char * childName = mxmlGetElement(child);
                if (childName == nullptr) {
                    continue;
                }

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
            std::map<std::string, mxml_node_t *> counterSetNodes;
            for (mxml_node_t * node = mxmlFindElement(xml, xml, TAG_COUNTER_SET, nullptr, nullptr, MXML_DESCEND);
                 node != nullptr;
                 node = mxmlFindElement(node, xml, TAG_COUNTER_SET, nullptr, nullptr, MXML_DESCEND)) {
                const char * const name = mxmlElementGetAttr(node, ATTR_NAME);
                if (name != nullptr) {
                    counterSetNodes.emplace(name, node);
                }
            }

            // find all category elements by name
            std::map<std::string, mxml_node_t *> categoryNodes;
            for (mxml_node_t * node = mxmlFindElement(xml, xml, TAG_CATEGORY, nullptr, nullptr, MXML_DESCEND);
                 node != nullptr;
                 node = mxmlFindElement(node, xml, TAG_CATEGORY, nullptr, nullptr, MXML_DESCEND)) {
                const char * const name = mxmlElementGetAttr(node, ATTR_COUNTER_SET);
                if (name != nullptr) {
                    categoryNodes.emplace(name, node);
                }
            }

            // resolve counter set copies for PMUs
            for (const auto & pair : idToCounterSetAndName) {
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
                mxml_node_t * newCounterSetNode = mxmlNewElement(mxmlGetParent(counterSetNode), TAG_COUNTER_SET);
                copyMxmlElementAttrs(newCounterSetNode, counterSetNode, NOP_ATTR_MODIFICATION_FUNCTION);
                mxmlElementSetAttr(newCounterSetNode, ATTR_NAME, newCounterSetName.c_str());

                // clone the new category element
                mxml_node_t * const categoryNode = categoryIt->second;
                mxml_node_t * newCategoryNode = mxmlNewElement(mxmlGetParent(categoryNode), TAG_CATEGORY);
                copyMxmlElementAttrs(newCategoryNode,
                                     categoryNode,
                                     [&coreName](const char * /*elementName*/,
                                                 const char * attrName,
                                                 const char * attrValue,
                                                 std::string & result) -> bool {
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
                copyMxmlChildElements(newCategoryNode,
                                      categoryNode,
                                      [&oldEventPrefix, &newEventPrefix](const char * elementName,
                                                                         const char * attrName,
                                                                         const char * attrValue,
                                                                         std::string & result) -> bool {
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

    static bool mergeSpes(mxml_node_t * mainParent, mxml_node_t * appendParent)
    {
        // Make list of all existing spes in main xml before we add new ones
        std::map<std::string, mxml_node_t *> existingSpesById {};

        for (mxml_node_t * node : MxmlChildElementsWithNameView {mainParent, TAG_SPE}) {
            const char * const id = mxmlElementGetAttr(node, ATTR_ID);
            if (id == nullptr) {
                logg.logError("Not all event XML <spe> nodes have the required 'id' attribute");
                return false;
            }
            existingSpesById[id] = node;
        }

        auto view = MxmlChildElementsWithNameView {appendParent, TAG_SPE};
        for (auto nodeIt = view.begin(); nodeIt != view.end();) {
            const auto node = *nodeIt;
            // get next before invalidating current iterator by moving node to main
            ++nodeIt;

            const char * const id = mxmlElementGetAttr(node, ATTR_ID);
            if (id == nullptr) {
                logg.logError("Not all appended event XML <spe> have the required 'id' attribute");
                return false;
            }

            // Replace any duplicate <spe>s
            const auto existingSpe = existingSpesById.find(id);
            if (existingSpe != existingSpesById.end()) {
                logg.logMessage("Replacing old <spe id=\"%s\">", id);
                // counter set can be inside categories
                mxmlDelete(existingSpe->second);
                existingSpesById.erase(existingSpe);
            }

            // Add new spe
            logg.logMessage("Appending <spe id=\"%s\">", id);
            mxmlAdd(mainParent, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, node);
        }
        return true;
    }

    /**
     * parents can be <events> or <category>
     */
    static bool mergeCounterSets(mxml_node_t * mainParent, mxml_node_t * appendParent)
    {
        // Make list of all existing counters set in main xml before we add new ones
        std::map<std::string, mxml_node_t *> existingCounterSetsByName {};

        for (mxml_node_t * node : MxmlChildElementsWithNameView {mainParent, TAG_COUNTER_SET}) {
            const char * const name = mxmlElementGetAttr(node, ATTR_NAME);
            if (name == nullptr) {
                logg.logError("Not all event XML counter_set nodes have the required name attribute");
                return false;
            }
            existingCounterSetsByName[name] = node;
        }

        auto view = MxmlChildElementsWithNameView {appendParent, TAG_COUNTER_SET};
        for (auto nodeIt = view.begin(); nodeIt != view.end();) {
            const auto node = *nodeIt;
            // get next before invalidating current iterator by moving node to main
            ++nodeIt;

            const char * const name = mxmlElementGetAttr(node, ATTR_NAME);
            if (name == nullptr) {
                logg.logError("Not all appended event XML counter_sets have the required name attribute");
                return false;
            }

            // Replace any duplicate counter_sets
            auto existingCounterSet = existingCounterSetsByName.find(name);
            if (existingCounterSet != existingCounterSetsByName.end()) {
                logg.logMessage("Replacing counter set %s", name);
                mxmlDelete(existingCounterSet->second);
                existingCounterSetsByName.erase(existingCounterSet);
            }

            // Add new counter_set
            logg.logMessage("Appending counter_set %s", name);
            mxmlAdd(mainParent, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, node);
        }
        return true;
    }

    static bool mergeEvents(mxml_node_t * mainParent, mxml_node_t * appendParent)
    {
        using TitleAndName = std::pair<std::string, std::string>;

        std::map<TitleAndName, mxml_node_t *> existingEventsByTitleAndName;
        for (mxml_node_t * node : MxmlChildElementsWithNameView {mainParent, TAG_EVENT}) {
            const char * const title = mxmlElementGetAttr(node, ATTR_TITLE);
            const char * const name = mxmlElementGetAttr(node, ATTR_NAME);

            if (title == nullptr || name == nullptr) {
                logg.logError("Not all event XML nodes have the required title and name");
                return false;
            }

            existingEventsByTitleAndName[{title, name}] = node;
        }

        auto view = MxmlChildElementsWithNameView {appendParent, TAG_EVENT};
        for (auto nodeIt = view.begin(); nodeIt != view.end();) {
            const auto node = *nodeIt;
            // get next before invalidating current iterator by moving node to main
            ++nodeIt;

            const char * const title = mxmlElementGetAttr(node, ATTR_TITLE);
            const char * const name = mxmlElementGetAttr(node, ATTR_NAME);

            if (title == nullptr || name == nullptr) {
                logg.logError("Not all appended event XML nodes have the required title and name");
                return false;
            }

            // Remove any duplicate events
            auto existingEvent = existingEventsByTitleAndName.find({title, name});
            if (existingEvent != existingEventsByTitleAndName.end()) {
                logg.logMessage("Replacing event %s: %s", title, name);
                mxmlDelete(existingEvent->second);
                existingEventsByTitleAndName.erase(existingEvent);
            }

            mxmlAdd(mainParent, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, node);
        }

        return true;
    }

    static bool mergeCategories(mxml_node_t * mainParent, mxml_node_t * appendParent)
    {
        // Make list of all existing categories in main xml before we add new ones
        std::map<std::string, mxml_node_t *> existingCategoriesByName {};
        for (mxml_node_t * node : MxmlChildElementsWithNameView {mainParent, TAG_CATEGORY}) {
            const char * const name = mxmlElementGetAttr(node, ATTR_NAME);
            if (name == nullptr) {
                logg.logError("Not all event XML category nodes have the required name attribute");
                return false;
            }
            existingCategoriesByName[name] = node;
        }

        for (mxml_node_t * node : MxmlChildElementsWithNameView {appendParent, TAG_CATEGORY}) {
            const char * const name = mxmlElementGetAttr(node, ATTR_NAME);
            if (name == nullptr) {
                logg.logError("Not all appended event XML category nodes have the required name attribute");
                return false;
            }

            auto existingCategory = existingCategoriesByName.find(name);
            if (existingCategory != existingCategoriesByName.end()) {
                // Merge identically named categories
                logg.logMessage("Merging category %s", name);
                if (!mergeEvents(existingCategory->second, node) || !mergeCounterSets(existingCategory->second, node)) {
                    return false;
                }
            }
            else {
                // Add new category
                logg.logMessage("Appending category %s", name);
                mxmlAdd(mainParent, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, node);
            }
        }

        return true;
    }

    bool mergeTrees(mxml_node_t * mainXml, mxml_unique_ptr appendXml)
    {
        runtime_assert(mainXml != nullptr, "mainXml must not be nullptr");
        runtime_assert(appendXml != nullptr, "appendXml must not be nullptr");

        mxml_node_t * const mainEventsNode = getEventsElement(mainXml);
        if (mainEventsNode == nullptr) {
            logg.logError(
                "Unable to find <events> node in the events.xml, please ensure the first two lines of events XML "
                "starts with:\n"
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<events>");
            return false;
        }

        mxml_node_t * const appendEventsNode = getEventsElement(appendXml.get());
        if (appendEventsNode == nullptr) {
            logg.logError("Unable to find <events> node in the appended events.xml, please ensure the first two lines "
                          "of events XML "
                          "starts with:\n"
                          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                          "<events>");
            return false;
        }

        return mergeCounterSets(mainEventsNode, appendEventsNode) &&
               mergeCategories(mainEventsNode, appendEventsNode) && mergeSpes(mainEventsNode, appendEventsNode);
    }

    void processClusters(mxml_node_t * xml, lib::Span<const GatorCpu> clusters)
    {
        addAdditionalPmusCounterSets(xml, clusters);

        // Resolve ${cluster}
        for (mxml_node_t *node = mxmlFindElement(xml, xml, TAG_EVENT, nullptr, nullptr, MXML_DESCEND),
                         *next = mxmlFindElement(node, xml, TAG_EVENT, nullptr, nullptr, MXML_DESCEND);
             node != nullptr;
             node = next, next = mxmlFindElement(node, xml, TAG_EVENT, nullptr, nullptr, MXML_DESCEND)) {
            const char * const counter = mxmlElementGetAttr(node, ATTR_COUNTER);

            if ((counter != nullptr) && (strncmp(counter, CLUSTER_VAR, sizeof(CLUSTER_VAR) - 1) == 0)) {
                for (const GatorCpu & cluster : clusters) {
                    mxml_node_t * n = mxmlNewElement(mxmlGetParent(node), TAG_EVENT);
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
        return mxmlFindElement(xml, xml, TAG_EVENTS, nullptr, nullptr, MXML_DESCEND_FIRST);
    }

    std::pair<mxml_unique_ptr, mxml_unique_ptr> createCategoryAndCounterSetNodes(const Category & category)
    {
        // Create the category node
        mxml_unique_ptr categoryNode {makeMxmlUniquePtr(mxmlNewElement(MXML_NO_PARENT, TAG_CATEGORY))};

        // Create the counter set node
        mxml_unique_ptr counterSetNode {makeMxmlUniquePtr(nullptr)};
        if (category.counterSet) {
            const CounterSet & counterSet = category.counterSet.get();

            counterSetNode.reset(mxmlNewElement(MXML_NO_PARENT, TAG_COUNTER_SET));
            mxmlElementSetAttrf(counterSetNode.get(), ATTR_COUNT, "%i", counterSet.count);
            mxmlElementSetAttr(counterSetNode.get(), ATTR_NAME, counterSet.name.c_str());

            // Add the counter_set attr to the category node
            mxmlElementSetAttr(categoryNode.get(), ATTR_COUNTER_SET, counterSet.name.c_str());
        }

        // Populate the category node
        mxmlElementSetAttr(categoryNode.get(), ATTR_NAME, category.name.c_str());
        for (auto event : category.events) {
            mxml_node_t * eventNode {mxmlNewElement(categoryNode.get(), TAG_EVENT)};

            if (event.eventNumber.isValid()) {
                mxmlElementSetAttrf(eventNode, ATTR_EVENT, "0x%" PRIxEventCode, event.eventNumber.asU64());
            }
            if (event.counter) {
                mxmlElementSetAttr(eventNode, ATTR_COUNTER, event.counter.get().c_str());
            }
            mxmlElementSetAttr(eventNode, ATTR_TITLE, event.title.c_str());
            mxmlElementSetAttr(eventNode, ATTR_NAME, event.name.c_str());
            mxmlElementSetAttr(eventNode, ATTR_DESCRIPTION, event.description.c_str());
            mxmlElementSetAttr(eventNode, ATTR_UNITS, event.units.c_str());
            mxmlElementSetAttrf(eventNode, ATTR_MULTIPLIER, "%f", event.multiplier);
            mxmlElementSetAttr(eventNode, ATTR_CLASS, classToStringMap.at(event.clazz).c_str());
        }

        return std::make_pair(std::move(categoryNode), std::move(counterSetNode));
    }
}
