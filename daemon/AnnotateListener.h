/**
 * Copyright (C) Arm Limited 2014-2016. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ANNOTATELISTENER_H
#define ANNOTATELISTENER_H

#include "ClassBoilerPlate.h"

struct AnnotateClient;
class OlyServerSocket;

class AnnotateListener
{
public:
    AnnotateListener();
    ~AnnotateListener();

    void setup();
#ifdef TCP_ANNOTATIONS
    int getSockFd();
#endif
    int getUdsFd();

#ifdef TCP_ANNOTATIONS
    void handleSock();
#endif
    void handleUds();
    void close();
    void signal();

private:
    AnnotateClient *mClients;
#ifdef TCP_ANNOTATIONS
    OlyServerSocket *mSock;
#endif
    OlyServerSocket *mUds;

    // Intentionally unimplemented
    CLASS_DELETE_COPY_MOVE(AnnotateListener);
};

#endif // ANNOTATELISTENER_H
