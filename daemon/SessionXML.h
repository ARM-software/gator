/**
 * Copyright (C) Arm Limited 2010-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef SESSION_XML_H
#define SESSION_XML_H

#include "ClassBoilerPlate.h"
#include "mxml/mxml.h"

struct ImageLinkList;

struct ConfigParameters
{
    // buffer mode, "streaming", "low", "normal", "high" defines oneshot and buffer size
    char buffer_mode[64];
    // capture mode, "high", "normal", or "low"
    char sample_rate[64];
    // whether stack unwinding is performed
    bool call_stack_unwinding;
    int live_rate;

    ConfigParameters()
            : buffer_mode(),
              sample_rate(),
              call_stack_unwinding(false),
              live_rate(0)
    {
        buffer_mode[0] = '\0';
        sample_rate[0] = '\0';
    }
};

class SessionXML
{
public:
    SessionXML(const char *str);
    ~SessionXML();
    void parse();
    ConfigParameters parameters;
private:
    const char *mSessionXML;
    void sessionTag(mxml_node_t *tree, mxml_node_t *node);
    void sessionImage(mxml_node_t *node);

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(SessionXML)
    ;
};

#endif // SESSION_XML_H
