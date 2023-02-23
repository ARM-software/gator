/* Copyright (C) 2010-2022 by Arm Limited. All rights reserved. */

#ifndef SESSION_XML_H
#define SESSION_XML_H

#include <mxml.h>

struct ImageLinkList;

struct ConfigParameters {
    // buffer mode, "streaming", "low", "normal", "high" defines oneshot and buffer size
    char buffer_mode[64];
    // capture mode, "high", "normal", or "low"
    char sample_rate[64];
    // whether stack unwinding is performed
    bool call_stack_unwinding;
    int live_rate;

    ConfigParameters() : buffer_mode(), sample_rate(), call_stack_unwinding(false), live_rate(0)
    {
        buffer_mode[0] = '\0';
        sample_rate[0] = '\0';
    }
};

class SessionXML {
public:
    ConfigParameters parameters {};

    SessionXML(const char * str);

    // Intentionally unimplemented
    SessionXML(const SessionXML &) = delete;
    SessionXML & operator=(const SessionXML &) = delete;
    SessionXML(SessionXML &&) = delete;
    SessionXML & operator=(SessionXML &&) = delete;

    void parse();

private:
    const char * mSessionXML;

    static void sessionImage(mxml_node_t * node);

    void sessionTag(mxml_node_t * tree, mxml_node_t * node);
};

#endif // SESSION_XML_H
