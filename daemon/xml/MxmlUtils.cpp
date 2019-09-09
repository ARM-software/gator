/* Copyright (c) 2019 by Arm Limited. All rights reserved. */

#include "xml/MxmlUtils.h"

// mxml doesn't have a function to do this, so dip into its private API
// Copy all the attributes from src to dst
void copyMxmlElementAttrs(mxml_node_t *dest, mxml_node_t *src)
{
    if (dest == NULL || dest->type != MXML_ELEMENT || src == NULL || src->type != MXML_ELEMENT)
        return;

    int i;
    mxml_attr_t *attr;

    for (i = src->value.element.num_attrs, attr = src->value.element.attrs; i > 0; --i, ++attr) {
        mxmlElementSetAttr(dest, attr->name, attr->value);
    }
}

// whitespace callback utility function used with mini-xml
const char * mxmlWhitespaceCB(mxml_node_t *node, int loc)
{
    const char *name;

    name = mxmlGetElement(node);

    if (loc == MXML_WS_BEFORE_OPEN) {
        // Single indentation
        if (!strcmp(name, "target") || !strcmp(name, "counters"))
            return "\n  ";

        // Double indentation
        if (!strcmp(name, "counter"))
            return "\n    ";

        // Avoid a carriage return on the first line of the xml file
        if (!strncmp(name, "?xml", 4))
            return NULL;

        // Default - no indentation
        return "\n";
    }

    if (loc == MXML_WS_BEFORE_CLOSE) {
        // No indentation
        if (!strcmp(name, "captured"))
            return "\n";

        // Single indentation
        if (!strcmp(name, "counters"))
            return "\n  ";

        // Default - no carriage return
        return NULL;
    }

    return NULL;
}
