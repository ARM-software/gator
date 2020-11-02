/* Copyright (C) 2019-2020 by Arm Limited. All rights reserved. */

#ifndef MXML_UTILS_H
#define MXML_UTILS_H

#include "mxml/mxml.h"

#include <memory>

/** unique_ptr for mxml nodes */
using mxml_unique_ptr = std::unique_ptr<mxml_node_t, void (*)(mxml_node_t *)>;

/**
 * Make an `mxml_unique_ptr`
 */
inline mxml_unique_ptr makeMxmlUniquePtr(mxml_node_t * node)
{
    return {node, mxmlDelete};
}

const char * mxmlWhitespaceCB(mxml_node_t * node, int loc);
void copyMxmlElementAttrs(mxml_node_t * dest, mxml_node_t * src);

/**
 * Forward iterator that calls mxmlFindElement
 */
struct MxmlFindElementIterator {
    using value_type = mxml_node_t *;
    using difference_type = std::ptrdiff_t;
    using reference = value_type &;
    using pointer = value_type *;
    using iterator_category = std::input_iterator_tag;

    MxmlFindElementIterator & operator++()
    {
        node = mxmlFindElement(node, top, element, attr, value, descend);
        return *this;
    }

    value_type & operator*() { return node; }

    bool operator==(const MxmlFindElementIterator & that) { return node == that.node; }
    bool operator!=(const MxmlFindElementIterator & that) { return node != that.node; }

    mxml_node_t * node;
    mxml_node_t * top;
    const char * element;
    const char * attr;
    const char * value;
    int descend;
};

/**
 * View of all children of an element with a certain name
 */
struct MxmlChildElementsWithNameView {
    MxmlFindElementIterator begin()
    {
        return {.node = mxmlFindElement(parent, parent, name, nullptr, nullptr, MXML_DESCEND_FIRST),
                .top = parent,
                .element = name,
                .attr = nullptr,
                .value = nullptr,
                .descend = MXML_NO_DESCEND};
    }

    MxmlFindElementIterator end() { return {}; }

    mxml_node_t * parent;
    const char * name;
};

#endif // MXML_UTILS_H
