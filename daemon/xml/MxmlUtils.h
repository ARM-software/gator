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

const char * mxmlWhitespaceCB(mxml_node_t * node, int where);
void copyMxmlElementAttrs(mxml_node_t * dest, mxml_node_t * src);

#endif // MXML_UTILS_H
