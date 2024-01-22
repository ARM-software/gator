/* Copyright (C) 2019-2023 by Arm Limited. All rights reserved. */

#include "xml/MxmlUtils.h"

#include <cstring>
#include <string>

#include <mxml.h>

// mxml doesn't have a function to do this, so dip into its private API
// Copy all the attributes from src to dst
void copyMxmlElementAttrs(mxml_node_t * dest, mxml_node_t * src)
{
    if (dest == nullptr || mxmlGetType(dest) != MXML_ELEMENT || src == nullptr || mxmlGetType(src) != MXML_ELEMENT) {
        return;
    }

    const int numAttrs = mxmlElementGetAttrCount(src);
    for (int i = 0; i < numAttrs; ++i) {
        const char * name = nullptr;
        const char * value = mxmlElementGetAttrByIndex(src, i, &name);
        mxmlElementSetAttr(dest, name, value);
    }
}

// whitespace callback utility function used with mini-xml
const char * mxmlWhitespaceCB(mxml_node_t * node, int loc)
{
    const char * name;

    name = mxmlGetElement(node);

    if (loc == MXML_WS_BEFORE_OPEN) {
        // Single indentation
        if ((strcmp(name, "target") == 0) || (strcmp(name, "counters") == 0)) {
            return "\n  ";
        }

        // Double indentation
        if (strcmp(name, "counter") == 0) {
            return "\n    ";
        }

        // Avoid a carriage return on the first line of the xml file
        if (strncmp(name, "?xml", 4) == 0) {
            return nullptr;
        }

        // Default - no indentation
        return "\n";
    }

    if (loc == MXML_WS_BEFORE_CLOSE) {
        // No indentation
        if (strcmp(name, "captured") == 0) {
            return "\n";
        }

        // Single indentation
        if (strcmp(name, "counters") == 0) {
            return "\n  ";
        }

        // Default - no carriage return
        return nullptr;
    }

    return nullptr;
}

std::string mxmlSaveAsStdString(mxml_node_t * node, mxml_save_cb_t whiteSpaceCB)
{
    std::string result;
    result.resize(8192);

    // Try writing to string data
    int length = mxmlSaveString(node, &result.front(), result.size(), whiteSpaceCB);

    if (length < static_cast<int>(result.size()) - 1) {
        // The node fits inside the buffer, shrink and return
        result.resize(length);
        return result;
    }

    // Node is too large so change the string size and return that.
    result.resize(length + 1);
    mxmlSaveString(node, &result.front(), result.size(), whiteSpaceCB);
    // mxmlSaveString will replace the last character will null terminator,
    // so we need to resize again
    result.resize(length);
    return result;
}
